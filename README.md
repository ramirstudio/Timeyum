# Timeyum Timeshift

Effetto di rendering per Cinema 4D che riproduce il look dell'ARRI Timing Shift Box: la cinepresa con l'otturatore sfasato rispetto alla griffa espone la pellicola mentre scorre, e ogni zona luminosa lascia una scia lungo la direzione di trascinamento. La scia che supera il bordo del fotogramma rientra dal lato opposto (wrap-around), perché finisce sul fotogramma vicino. È lo stesso comportamento della macro "Timeshift Effect" per DaVinci Resolve, con in più il modello fisico dello sfasamento, i ghost, il roll del fotogramma e la versione con shake.

Il plugin è un VideoPost in Python: compare in Impostazioni di rendering > Effetti come "Timeyum Timeshift", lavora sull'immagine finale RGBA dopo il render e ogni parametro si imposta dal pannello dell'effetto.

## Installazione

Copia la cartella `timeyum` nella cartella `plugins` delle preferenze di Cinema 4D (Modifica > Preferenze > pulsante "Apri cartella preferenze"), oppure aggiungi il percorso di questo repository in Preferenze > Plugin. Riavvia Cinema 4D.

Il calcolo usa NumPy, che Cinema 4D non include. Se manca, l'effetto compare come "Timeyum Timeshift (NumPy missing)" e lascia l'immagine invariata. Per installarlo nel Python di Cinema 4D:

```
Windows
"C:\Program Files\Maxon Cinema 4D 2025\c4dpy.exe" -m pip install numpy

macOS
"/Applications/Maxon Cinema 4D 2025/c4dpy.app/Contents/MacOS/c4dpy" -m pip install numpy
```

Adatta il percorso alla tua versione. Al primo avvio c4dpy chiede il login Maxon; se risponde che pip non esiste, lancia prima `c4dpy -m ensurepip`. In alternativa puoi installare NumPy dentro `timeyum/lib` con un Python di sistema della stessa versione di quello di Cinema 4D (la leggi nella Console Python con `import sys; print(sys.version)`):

```
python3.11 -m pip install --target "<percorso>/timeyum/lib" numpy
```

L'ID del plugin (1000007) appartiene alla fascia di test riservata da Maxon. Per uso personale va bene; prima di distribuirlo richiedine uno gratuito su developers.maxon.net e sostituiscilo in `timeyum.pyp`.

## Uso

Impostazioni di rendering > Effetti > Timeyum Timeshift, poi render nel Visualizzatore immagini. Alla prima immagine la console scrive risoluzione e tempo per fotogramma: se quella riga non compare, l'effetto non è stato eseguito. I valori di default danno la scia verso l'alto con wrap-around simile al fiore e al teschio degli esempi. Un 1080p richiede circa mezzo secondo per fotogramma, un 4K circa tre.

I parametri si animano con i keyframe come quelli degli altri effetti di render (questo non l'ho potuto provare dentro C4D). Lo shake dipende solo dal tempo del documento e dal seed, quindi lo stesso fotogramma esce identico a ogni render.

## Parametri

Corrispondenza con la macro DaVinci: Blur Strength è Length, Blur Angle è Angle, Symmetric Blur è Symmetric, Blur Behavior è Edge Behavior, Opacity è Opacity, Input Color Space/Gamma è Output > Input Space, la versione "with Shake" è il gruppo Shake. Cleanup Amount l'ho interpretato come un leggero ammorbidimento perpendicolare alla scia, che pulisce le striature troppo dure (Exposure > Cleanup, in pixel). In Timeyum l'angolo 90° manda la scia verso l'alto e 0° verso destra.

Streak Model sceglie come si distribuisce la luce lungo la scia. Fade è una scia che si attenua verso la punta (Falloff e Falloff Curve nel gruppo Profile), Exponential decade in modo esponenziale (Decay), Custom Curve usa la curva disegnata nel gruppo Profile (a sinistra l'inizio della scia, a destra la punta). Camera (Timing Shift) simula la cinepresa: nel gruppo Camera imposti lo sfasamento dell'otturatore in gradi, l'angolo otturatore, la durata del trascinamento e quanto la griffa accelera e rallenta (Claw Ease). Da questi valori il plugin ricava sia la quota di luce che resta nitida sia la forma della scia; con Length al 100% la pellicola scorre di un fotogramma intero. Timing Shift 0° equivale a nessun effetto, 60-120° danno la scia classica, 180° con otturatore a 180° espone tutto il trascinamento e l'immagine nitida scompare.

Nel gruppo Streak, Length è la lunghezza della scia in percentuale dell'altezza del fotogramma, Back Length aggiunge una scia nella direzione opposta, Start Offset stacca l'inizio della scia dall'oggetto, Edge Behavior decide cosa succede al bordo (Wrap-Around come la pellicola, Extend, Mirror, Black) e Frame Line Gap inserisce l'interlinea nera tra i fotogrammi nel wrap (nel Super 35 4-perf è circa l'1,8%).

Nel gruppo Exposure, Smear Amount è la parte dell'esposizione che finisce nella scia. Con Blend su Exposure la luce viene spostata e non aggiunta, quindi la luminosità media resta quella del render; Add, Screen e Lighten sommano la scia all'immagine. Smear Gain moltiplica la scia, Threshold e Knee limitano la scia alle alte luci (valori lineari, 1.0 è il bianco).

Ghost aggiunge copie spostate dell'immagine lungo la scia (numero, distanza, intensità, decadimento tra una copia e l'altra, lunghezza della micro-scia di ciascuna). Frame Roll sposta verticalmente l'intero fotogramma come un proiettore fuori quadro, con la barra dell'interlinea regolabile e sfumabile. Color tinge e desatura solo la scia, Chromatic Spread allunga il rosso e accorcia il blu (o viceversa), Breakup varia l'intensità tra una striscia e l'altra.

Shake genera un jitter deterministico: Frequency è il numero di nuovi valori al secondo (mettila uguale agli fps per un valore diverso a ogni fotogramma), Smoothness passa da salti netti a variazioni morbide, Seed cambia la sequenza. Ogni voce "Jitter" è l'ampiezza della variazione di quel parametro; Weave X/Y fa tremare l'intero fotogramma di qualche pixel come il gate di una cinepresa.

In Output, Input Space va lasciato su Linear per i render di Cinema 4D (con OCIO o con Linear Workflow il buffer è lineare); sRGB serve se il buffer è già in spazio display. Smear Alpha estende la scia anche al canale alfa, così resta visibile quando componi il render su un altro sfondo. Apply At: lascia Render End; Frame End è un tentativo per motori esterni che scrivono l'immagine più tardi.

## Motori di rendering esterni e CLI

Il plugin dichiara di essere compatibile con ogni motore. Con Standard e Physical l'effetto lavora sul buffer finale. Con Redshift, Octane o Arnold dipende da quando il motore scrive l'immagine nel buffer di Cinema 4D: se il render esce invariato, prova Apply At > Frame End, altrimenti usa lo strumento da riga di comando sulla sequenza già renderizzata. Usa lo stesso motore di calcolo e gli stessi parametri (percentuali come frazioni, angoli in gradi):

```
pip install numpy pillow
python tools/timeyum_cli.py "render/beauty_*.png" -o out --set profile=camera --set length=1 --set shake=1 --fps 25
python tools/timeyum_cli.py --list x -o x
```

Per EXR e PNG a 16 bit serve anche `imageio` con un backend EXR. I file a 8 e 16 bit vengono trattati come sRGB, quelli float come lineari, salvo `--colorspace`.

## Stato

Il motore (`timeyum_core.py`) è testato su tutte le combinazioni di modalità, bordi, spazi colore e casi limite, e in modalità Exposure con wrap-around conserva la luminosità media dell'immagine. Il codice di integrazione con Cinema 4D è stato eseguito contro una simulazione dell'API ma non dentro Cinema 4D: se qualcosa non va al primo avvio, la Console Python (Estensioni > Console) riporta l'errore completo con il prefisso `[Timeyum]`. Se la scia va verso il basso invece che verso l'alto, il buffer della tua versione ha l'origine in basso: basta Angle a -90°.
