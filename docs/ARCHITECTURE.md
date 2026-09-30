# Architettura di Timeyum

Timeyum riproduce il look del Timing Shift Box ARRI, cioè una cinepresa a pellicola con l'otturatore sfasato rispetto alla griffa. Il motore è una libreria C++17 senza dipendenze (`core/`), usata da un wrapper SmartFX per After Effects (`ae/`) e da una CLI (`tools/timeyum_cli.cpp`). Gira solo su CPU, con thread propri.

## Modello

Nella cinepresa la griffa tira giù la pellicola mentre l'otturatore è chiuso. Con lo sfasamento l'esposizione avviene mentre la pellicola scorre: ogni punto luminoso disegna una scia lungo il trascinamento, e la scia che esce dal fotogramma finisce sul fotogramma vicino, quindi rientra dal bordo opposto. Il motore calcola questa esposizione come convoluzione circolare dell'immagine con un kernel lineare orientato.

`Kernel.cpp` costruisce il kernel come elenco di tap (spostamento in pixel lungo la direzione, peso). Il profilo Camera simula un ciclo di 360 gradi: la pellicola riposa per (360 − pull-down) gradi e poi avanza di un passo in `pull-down` gradi con accelerazione regolabile (`claw_ease`); l'otturatore è aperto per `shutter_angle` gradi a partire da `timing_shift`. Dalla distribuzione degli spostamenti durante l'apertura si ricavano sia la quota di luce che resta nitida sia la forma della scia. Gli altri profili (Fade, Exponential, Curve) sono artistici: peso in funzione della posizione lungo la scia, normalizzato alla quota `smear`. I ghost sono tap aggiuntivi a distanza fissa. La somma dei pesi è la quota di esposizione che lascia l'immagine di base, così in modalità Exposure la luminosità media si conserva.

## Render

`Timeyum.cpp` converte il fotogramma in float lineare (`ColorSpace.cpp`: sRGB, gamma 2.4, LogC3, S-Log3), isola le alte luci con soglia e ginocchio morbido, e convolve i piani con il kernel tramite FFT 2D. Il dominio della FFT dipende dal bordo: con Wrap è esattamente il fotogramma (più l'interlinea `frame_gap`), con Extend, Mirror e Black è il fotogramma più un margine pari alla scia, arrotondato a una dimensione fattorizzabile in 2, 3 e 5. Due piani reali con lo stesso kernel viaggiano in un'unica FFT complessa. La cromatica laterale usa kernel di lunghezza diversa per canale. Dopo la convoluzione vengono applicati guadagno, breakup (variazione d'intensità tra le strisce), saturazione e tinta della sola scia, il blend (Exposure, Add, Screen, Lighten), lo spostamento verticale del fotogramma con barra d'interlinea, il tremolio del gate e l'opacità finale.

Lo shake (`Noise.cpp`) è rumore di valore deterministico in funzione di tempo e seed, quindi lo stesso fotogramma esce identico a ogni render. `Fft.cpp` è una FFT Stockham con butterfly 2, 3, 4, 5 e generica fino a 7, e Bluestein per le dimensioni con fattori primi più grandi.

## Wrapper After Effects

`TimeyumAE.cpp` implementa SmartFX (`PreRender`, `SmartRender`) a 8, 16 e 32 bit, con i flag per il multi-frame rendering. La scia si avvolge sull'intero livello, quindi ogni render richiede il livello intero. I parametri hanno indici nell'ordine del pannello (`TimeyumParams.h`) e ID su disco stabili, sempre aggiunti in coda. Il colore di AE è premoltiplicato: per gli spazi non lineari il wrapper lo demoltiplica, decodifica, rimoltiplica e fa girare il motore in luce lineare, poi ripercorre la strada inversa. La scala di risoluzione (metà, terzo) riporta in pixel i parametri espressi in pixel. I flag di `GlobalSetup` e del PiPL vengono da `TimeyumFlags.h`, con `static_assert` contro i flag dell'SDK. Il PiPL si genera con PiPLtool in build oppure si passa un `.rc` pre-generato con `TIMEYUM_PIPL_RC`.

## Verifica

`tests/test_fft.cpp` confronta la FFT con la DFT diretta su 28 dimensioni. `tests/compare_reference.py` confronta la CLI con il motore NumPy di riferimento (`tests/reference/`) su 23 configurazioni e due risoluzioni, una con dimensioni che passano da Bluestein: errore relativo massimo 7e-5, in genere 1e-7. `tests/test_ae_wrapper.cpp` compila il wrapper contro un finto SDK (`tests/ae_mock/`) e controlla ordine e ID dei parametri, l'uguaglianza tra i default del pannello e quelli del motore, la logica di abilitazione e la conversione pixel a 8, 16 e 32 bit. Il wrapper non è mai stato compilato contro l'SDK Adobe né eseguito in After Effects.

## Limiti

Il `.aex` si costruisce solo su Windows con MSVC. Non c'è una curva disegnabile nel pannello di AE (il profilo Curve esiste nel motore e nella CLI). Il kernel è lo stesso per tutto il fotogramma, senza dipendenza dal campo. In 4K un fotogramma richiede circa 2 s su 4 thread.
