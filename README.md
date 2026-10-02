# Timeyum

Effetto After Effects che riproduce il look del Timing Shift Box ARRI: scie di esposizione che rientrano dal bordo opposto del fotogramma, con modello fisico dello sfasamento otturatore, ghost, spostamento del fotogramma e shake. L'architettura è in `docs/ARCHITECTURE.md`.

## Compilare il plugin (Windows)

Servono Visual Studio 2022 con gli strumenti C++, CMake 3.20 e l'After Effects SDK (gratuito, dal sito Adobe Developer). `AE_SDK_DIR` è la cartella `Examples` dell'SDK, quella che contiene `Headers`, `Util` e `Resources`.

```
cmake -S . -B build -DAE_SDK_DIR="C:/AfterEffectsSDK/Examples"
cmake --build build --config Release
```

Il risultato è `build/Release/Timeyum.aex`. Copialo in `C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore` e riavvia After Effects: l'effetto è in Effetti e predefiniti > Timeyum > Timeyum. Il banner in cima al pannello si disattiva con `-DTIMEYUM_BANNER=OFF`. Se PiPLtool produce una risorsa vuota, genera a mano `TimeyumPiPL.rc` e passalo con `-DTIMEYUM_PIPL_RC=percorso`. Se la compilazione si ferma su un `static_assert` dei flag, correggi i numeri in `ae/TimeyumFlags.h`.

Il wrapper non è stato compilato contro l'SDK né provato in After Effects: aspettati qualche correzione ai nomi delle macro e delle funzioni, e se ci sono errori mandameli.

## Warp

Nel gruppo Warp, spuntando Enable, le scie smettono di essere uniformi: cambiano lunghezza e si deformano da punto a punto, in modo morbido e continuo nel tempo. Tre cose le guidano.

Il flow è un campo che evolve da solo (Flow Speed, Flow Scale, Flow Detail, Drift): Flow Length varia la lunghezza delle scie, Flow Wave le fa ondeggiare come un fluido. Se l'onda è troppo grande rispetto alla scala, il motore la appiattisce per non piegare l'immagine su se stessa; per più ondulazione alza Flow Scale.

La reazione al video fa dipendere il campo da ciò che succede nel clip. Brightness Response lo lega alla luminosità, Motion Response al movimento tra un fotogramma e l'altro, Inertia e History Frames gli danno memoria, così la risposta sale e scende lentamente invece di scattare. Length Reaction allunga le scie dove il video reagisce, Wave Reaction limita l'onda alle zone che reagiscono. Con History Frames a 0 non viene letto nessun fotogramma passato.

Il Pull Point è interattivo: lo trascini nel viewer di composizione, o lo colleghi a un null con un'espressione, e intorno a lui l'immagine si rigonfia (Pull Strength positivo) o si pizzica (negativo) e le scie si allungano (Pull Length). Auto Target Strength tira nello stesso modo verso la zona più luminosa del video, che viene seguita da sola. Base Follow decide quanto lo spostamento coinvolge anche l'immagine nitida oltre alle scie.

View mostra la mappa delle lunghezze, la reazione del video o lo spostamento al posto del risultato, utile per tarare i controlli. Length Steps controlla il costo: 3 è veloce, 6 è il default, 12 è il più liscio.

Provato solo con il motore e un SDK simulato. In After Effects controlla che il Pull Point parta al centro del livello, che a metà risoluzione resti nello stesso punto dell'immagine, e che con Motion Response alto le scie reagiscano a un clip con qualcosa che si muove.

## CLI e test (qualsiasi sistema)

```
cmake -S . -B build && cmake --build build
build/timeyum_cli in.ppm out.ppm profile=camera length=1 timing_shift=100 chroma=0.05
build/timeyum_cli --list
cd build && ctest
```

La CLI legge e scrive `.ppm`, `.pam` (8 bit, anche con alfa), `.pfm` e `.tyf` (float). I file a 8 bit sono trattati come sRGB, quelli float come lineari, salvo `space=`. `frame=` e `fps=` servono per lo shake. Le percentuali sono frazioni (`length=0.6` è il 60% dell'altezza), gli angoli in gradi, 90 è una scia verso l'alto. Per il warp servono i fotogrammi vicini: `build/timeyum_cli --seq in_%04d.ppm out_%04d.ppm 1 48 warp=1 fps=24` li legge da sé, oppure per un fotogramma solo `prev=f0011.ppm,f0010.ppm`. `warp_view=reaction` mostra la mappa di reazione. `ctest` esegue i quattro test: FFT, proprietà del warp, confronto con il riferimento NumPy (serve `numpy`) e wrapper contro l'SDK simulato.

`aftereffects/` contiene una versione precedente fatta con script ed espressioni, che non richiede compilazione.
