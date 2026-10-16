@echo off
setlocal EnableExtensions
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
if not defined QTDIR set "QTDIR=D:\Qt\6.10.3\msvc2022_64"
set "BUILD=%ROOT%\br"
if not defined ISCC set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 7\ISCC.exe"
if not exist "%QTDIR%\plugins\imageformats\qwebp.dll" (
    echo The WebP plugin is missing from "%QTDIR%". Install the Qt Image Formats module.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%V"
if not defined VSDIR (
    echo Visual Studio with the C++ x64 tools was not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cmake -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%QTDIR:\=/%" || exit /b 1
cmake --build "%BUILD%" --target FlyGlow || exit /b 1

set "VERSION="
for /f "tokens=2 delims==" %%V in ('findstr /b /c:"CMAKE_PROJECT_VERSION:STATIC=" "%BUILD%\CMakeCache.txt"') do set "VERSION=%%V"
if not defined VERSION (
    echo Could not read the project version from "%BUILD%\CMakeCache.txt".
    exit /b 1
)
set "DIST=%ROOT%\dist\%VERSION%"
set "STAGE=%DIST%\FlyGlow"
if not exist "%DIST%" mkdir "%DIST%" || exit /b 1
set "ZIP=%DIST%\FlyGlow-%VERSION%-portable-win64.zip"
set "SETUP=FlyGlow-%VERSION%-setup-win64"

if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%STAGE%" (
    echo Could not clear "%STAGE%". Is FlyGlow still running from there?
    exit /b 1
)
mkdir "%STAGE%" || exit /b 1
copy /y "%BUILD%\FlyGlow.exe" "%STAGE%\" >nul || exit /b 1
"%QTDIR%\bin\windeployqt.exe" --release --no-compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --no-translations "%STAGE%\FlyGlow.exe" || exit /b 1
mkdir "%STAGE%\translations" || exit /b 1
for %%L in (en ru) do copy /y "%QTDIR%\translations\qtbase_%%L.qm" "%STAGE%\translations\" >nul || exit /b 1

set "CRT=%VCToolsRedistDir%x64\Microsoft.VC143.CRT"
if not exist "%CRT%\msvcp140.dll" (
    echo VC++ runtime DLLs not found in "%CRT%".
    exit /b 1
)
copy /y "%CRT%\msvcp140*.dll" "%STAGE%\" >nul || exit /b 1
copy /y "%CRT%\vcruntime140*.dll" "%STAGE%\" >nul || exit /b 1

type nul > "%STAGE%\portable.txt"
if exist "%ZIP%" del /f /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%DIST%" FlyGlow || exit /b 1

if not exist "%ISCC%" (
    echo Inno Setup 7 compiler not found at "%ISCC%". Set ISCC to its path.
    exit /b 1
)
"%ISCC%" -q "-dAppVersion=%VERSION%" "-dStageDir=%STAGE%" "-dOutputDir=%DIST%" "%~dp0flyglow.iss" || exit /b 1

echo.
for %%F in ("%ZIP%" "%DIST%\%SETUP%.exe") do echo %%~nxF  %%~zF bytes
