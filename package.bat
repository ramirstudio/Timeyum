@echo off
rem Builds Timeyum and assembles the zip that is sold, in dist\.
rem Run it from "x64 Native Tools Command Prompt for VS", in the project folder:
rem   package.bat "C:\SDK\AfterEffectsSDK_26.5_win\AfterEffectsSDK_26.5_win\Examples"
rem The argument (the SDK's Examples folder) can be left out if AE_SDK_DIR is already set.
rem To force a generator:  set GENERATOR=Visual Studio 18 2026

setlocal EnableDelayedExpansion
cd /d "%~dp0"
set VERSION=1.0.0

if not "%~1"=="" set AE_SDK_DIR=%~1
if "%AE_SDK_DIR%"=="" (
    echo Pass the After Effects SDK "Examples" folder as the first argument.
    exit /b 1
)
set AE_SDK_DIR=%AE_SDK_DIR:\=/%
if not exist "%AE_SDK_DIR%/Headers/AE_Effect.h" (
    echo AE_Effect.h not found in %AE_SDK_DIR%/Headers
    exit /b 1
)
if not exist docs\USER_GUIDE.pdf (
    echo docs\USER_GUIDE.pdf is missing.
    exit /b 1
)

echo Configuring...
if defined GENERATOR (
    cmake -S . -B build-release -G "%GENERATOR%" -A x64 -DAE_SDK_DIR="%AE_SDK_DIR%" -DTIMEYUM_BANNER=ON
) else (
    cmake -S . -B build-release -A x64 -DAE_SDK_DIR="%AE_SDK_DIR%" -DTIMEYUM_BANNER=ON
)
if errorlevel 1 exit /b 1

echo Building...
cmake --build build-release --config Release --target timeyum_ae
if errorlevel 1 exit /b 1

set AEX=build-release\Release\Timeyum.aex
if not exist "%AEX%" (
    echo %AEX% not found
    exit /b 1
)
rem The banner picture alone is about 1.1 MB: a much smaller file means it was not embedded.
for %%F in ("%AEX%") do set SIZE=%%~zF
if !SIZE! LSS 1000000 (
    echo Warning: %AEX% is only !SIZE! bytes, the banner picture is probably not embedded.
)

set OUT=dist\Timeyum-%VERSION%-win64
if exist dist rmdir /s /q dist
mkdir "%OUT%"
copy /y "%AEX%" "%OUT%\" >nul
copy /y docs\INSTALL.txt "%OUT%\" >nul
copy /y docs\USER_GUIDE.pdf "%OUT%\" >nul
copy /y LICENSE "%OUT%\LICENSE.txt" >nul
copy /y CHANGELOG.md "%OUT%\CHANGELOG.txt" >nul

powershell -NoProfile -Command "Compress-Archive -Path 'dist\Timeyum-%VERSION%-win64' -DestinationPath 'dist\Timeyum-%VERSION%-win64.zip' -Force"
if errorlevel 1 exit /b 1
certutil -hashfile "dist\Timeyum-%VERSION%-win64.zip" SHA256 > "dist\Timeyum-%VERSION%-win64.zip.sha256.txt"

echo.
echo Done: dist\Timeyum-%VERSION%-win64.zip
dir "%OUT%"
type "dist\Timeyum-%VERSION%-win64.zip.sha256.txt"
endlocal
