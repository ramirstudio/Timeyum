# Timeyum Timeshift per After Effects

Script ScriptUI che costruisce sul livello selezionato il look del Timing Shift Box ARRI. Non è un plugin compilato: crea un rig di livelli ed espressioni pilotato da un null "TY Controls", con controlli standard che si possono animare con i keyframe.

## Installazione

File > Scripts > Install ScriptUI Panel, scegli `Timeyum.jsx`, riavvia After Effects e aprilo da Window > Timeyum.jsx. In alternativa File > Scripts > Run Script File. Serve il permesso "Allow Scripts to Write Files and Access Network" in Preferences > Scripting & Expressions.

## Uso

Seleziona un livello, scegli il numero di copie (32 va bene per il 1080p, 48-64 per scie molto lunghe) e premi Applica. Con "Precomponi prima il livello" il wrap-around avviene sul quadro della composizione, cosa che serve quando il livello è trasformato o più piccolo del quadro. I parametri stanno tra gli effetti del livello "TY Controls". I livelli "TY Copy" e "TY Ghost" sono nascosti (shy); il livello originale resta la base e non va toccato l'opacità, che viene pilotata dal rig.

Per un risultato corretto attiva Linear Blending nelle impostazioni del progetto (File > Project Settings > Color) e lavora con un progetto a 16 o 32 bit. Le copie sono sommate in modalità Add.

## Come funziona

Ogni copia è il livello con Motion Tile spostato lungo la direzione della scia, così il bordo rientra dal lato opposto come sulla pellicola, più un Directional Blur che riempie lo spazio tra una copia e la successiva. L'opacità di ogni copia segue il profilo scelto e la somma è normalizzata: in modalità Exposure l'opacità della base scende della stessa quota, quindi la luminosità media non cambia. Il profilo Camera usa lo stesso modello dell'altra versione: sfasamento dell'otturatore, angolo otturatore, angolo di pull-down e accelerazione della griffa.

Controlli: Opacity, Profile (Fade, Exponential, Camera), Length (100% = un fotogramma), Angle (90 = verso l'alto), Symmetric, Smear, Falloff, Decay, Timing Shift, Shutter Angle, Pulldown Angle, Claw Ease, Blend (Exposure o Add), Ghost (attivo, offset, forza, decadimento), Roll, e il gruppo Shake (ampiezza, frequenza, morbidezza, seed, jitter di lunghezza, quantità di scia, angolo, fase, roll, weave X/Y in pixel). Lo shake è deterministico: stesso tempo e stesso seed danno lo stesso fotogramma.

Non ci sono, rispetto alla versione C4D, soglia sulle alte luci, tinta e aberrazione cromatica della scia, back length e barra dell'interlinea.

## Verifica

Le espressioni sono testate con `node tests/ae_expr_test.js` contro una composizione simulata: conservazione dell'energia in tutti i profili, direzione della scia, determinismo dello shake. Lo script non è mai stato eseguito dentro After Effects. Se dà errore, l'alert riporta il messaggio e la riga. Due punti da controllare al primo uso: con un angolo diagonale (per esempio 45°) il Directional Blur deve essere allineato alla scia, altrimenti in `exprBlurDir` togli il segno meno; e se AE è localizzato, gli effetti Motion Tile e Directional Blur vengono cercati per matchName, quindi non dovrebbe cambiare nulla.
