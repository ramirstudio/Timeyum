# Third-party components

Timeyum contains no third-party code. The FFT, the noise functions, the colour transfer functions and everything else in `core/` are written for this project, and the plug-in is linked against the static Microsoft C++ runtime, so the package has no DLLs besides `Timeyum.aex`.

The Adobe After Effects SDK is needed only to build the plug-in and is not redistributed. The colour transfer functions follow the public specifications of ARRI LogC3 (EI 800) and Sony S-Log3. "ARRI" and "After Effects" are trademarks of their respective owners; Timeyum is not affiliated with them.
