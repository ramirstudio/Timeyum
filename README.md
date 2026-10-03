# Timeyum

Timeyum is an After Effects effect that reproduces the look of a film camera whose shutter is out of phase with the pull-down claw, the fault an ARRI Timing Shift Box creates on purpose. Bright areas leave streaks along the direction the film travels, and what leaves the frame comes back from the opposite edge.

It has a physical camera model (timing shift, shutter angle, pull-down angle, claw ease), three artistic streak profiles, edge behaviours, ghost echoes, frame roll with a frame line, chromatic spread, a deterministic shake, and a control input (a matte, alpha channel or depth map decides where the effect shows, where streaks come from and how long they are), and a Warp mode in which the streaks vary across the frame, flow, and react to the brightness and the motion of the video, with an interactive pull point. It runs on the CPU, at 8, 16 and 32 bits per channel, with multi-frame rendering.

The user documentation is `docs/USER_GUIDE.md`, installation is in `docs/INSTALL.txt`, and the design of the engine is in `docs/ARCHITECTURE.md`. The licence is in `LICENSE`: the software is proprietary and all rights are reserved.

## Layout

- `core/` is the engine, a dependency-free C++17 library that builds on any platform.
- `ae/` is the SmartFX wrapper for After Effects, with the PiPL and the banner.
- `tools/timeyum_cli.cpp` applies the effect to image sequences from the command line.
- `tests/` has the FFT test, the warp tests, the wrapper test against a mock SDK and the comparison with a NumPy reference engine.
- `gumroad/` has the product page text and images.

## Building the plug-in (Windows)

You need Visual Studio 2022 or later with the "Desktop development with C++" workload, CMake 3.20 or later (the one in Visual Studio is fine) and the After Effects SDK from Adobe, extracted for example to `C:\SDK\AfterEffectsSDK_26.5_win`. `AE_SDK_DIR` is the SDK's `Examples` folder, the one that contains `Headers`, `Util` and `Resources`.

Run `package.bat` from an "x64 Native Tools Command Prompt for VS":

```
package.bat "C:\SDK\AfterEffectsSDK_26.5_win\AfterEffectsSDK_26.5_win\Examples"
```

It configures and builds a Release `Timeyum.aex` and assembles `dist\Timeyum-1.0.0-win64.zip`, the file that is sold, together with its SHA-256 hash. To build by hand:

```
cmake -S . -B build -A x64 -DAE_SDK_DIR="C:/SDK/AfterEffectsSDK_26.5_win/AfterEffectsSDK_26.5_win/Examples"
cmake --build build --config Release --target timeyum_ae
```

The result is `build\Release\Timeyum.aex`. If PiPLtool produces an empty resource, generate `TimeyumPiPL.rc` by hand and pass it with `-DTIMEYUM_PIPL_RC=path`. If the build stops on a `static_assert` about flags, fix the numbers in `ae/TimeyumFlags.h`. The banner at the top of the panel can be switched off with `-DTIMEYUM_BANNER=OFF`. The picture is `ae/TimeyumBanner.webp`; `tools/make_banner.py` turns it into the embedded `TimeyumBanner.bin`.

## Command line tool and tests (any system)

```
cmake -S . -B build && cmake --build build
build/timeyum_cli in.ppm out.ppm profile=camera length=1 timing_shift=100 chroma=0.05
build/timeyum_cli --seq in_%04d.ppm out_%04d.ppm 1 48 warp=1 fps=24
build/timeyum_cli in.ppm out.ppm control_file=matte.ppm control=luma control_matte=1 control_emit=1
build/timeyum_cli --list
cd build && ctest
```

The tool reads and writes `.ppm`, `.pam` (8 bit, with alpha), `.pfm` and `.tyf` (float). 8 bit files are treated as sRGB and float files as linear unless `space=` is given. Percentages are fractions (`length=0.6` is 60% of the frame height), angles are in degrees, and 90 is a streak going up. `--seq` reads the neighbouring frames for the warp by itself; for a single frame use `prev=f0011.ppm,f0010.ppm`. `warp_view=reaction` shows the reaction map. `control_file=` is the picture of the control input (it may contain `%04d` with `--seq`), `control=` picks the channel (off, luma, alpha, red, green, blue, depth) and `control_view=1` shows the mapped control; the picture is read as it is, with no colour conversion. `ctest` runs five tests: FFT, warp properties, the control input, the comparison with the NumPy reference (needs `numpy`) and the wrapper against the mock SDK.
