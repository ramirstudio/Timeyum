# Timeyum

Effetto After Effects che riproduce il look del Timing Shift Box ARRI: scie di esposizione che rientrano dal bordo opposto del fotogramma, con modello fisico dello sfasamento otturatore, ghost, spostamento del fotogramma e shake. L'architettura è in `docs/ARCHITECTURE.md`.

## Compilare il plugin (Windows)

Servono Visual Studio 2022 con gli strumenti C++, CMake 3.20 e l'After Effects SDK (gratuito, dal sito Adobe Developer). `AE_SDK_DIR` è la cartella `Examples` dell'SDK, quella che contiene `Headers`, `Util` e `Resources`.

```
cmake -S . -B build -DAE_SDK_DIR="C:/AfterEffectsSDK/Examples"
cmake --build build --config Release
```

Il risultato è `build/Release/Timeyum.aex`. Copialo in `C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore` e riavvia After Effects: l'effetto è in Effetti e predefiniti > Timeyum > Timeyum. Se PiPLtool produce una risorsa vuota, genera a mano `TimeyumPiPL.rc` e passalo con `-DTIMEYUM_PIPL_RC=percorso`. Se la compilazione si ferma su un `static_assert` dei flag, correggi i numeri in `ae/TimeyumFlags.h`.

Il wrapper non è stato compilato contro l'SDK né provato in After Effects: aspettati qualche correzione ai nomi delle macro e delle funzioni, e se ci sono errori mandameli.

## CLI e test (qualsiasi sistema)

```
cmake -S . -B build && cmake --build build
build/timeyum_cli in.ppm out.ppm profile=camera length=1 timing_shift=100 chroma=0.05
build/timeyum_cli --list
cd build && ctest
```

La CLI legge e scrive `.ppm`, `.pam` (8 bit, anche con alfa), `.pfm` e `.tyf` (float). I file a 8 bit sono trattati come sRGB, quelli float come lineari, salvo `space=`. `frame=` e `fps=` servono per lo shake. Le percentuali sono frazioni (`length=0.6` è il 60% dell'altezza), gli angoli in gradi, 90 è una scia verso l'alto. `ctest` esegue i tre test: FFT, confronto con il riferimento NumPy (serve `numpy`) e wrapper contro l'SDK simulato.

`aftereffects/` contiene una versione precedente fatta con script ed espressioni, che non richiede compilazione.
