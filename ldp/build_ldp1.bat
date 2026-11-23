@echo off
setlocal
rem Builds the supported LDP-1 library and session test. See README.md.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere not found. From a VS x64 prompt run the cmake commands in README.md.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo Visual C++ tools not found.
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cmake -G "NMake Makefiles" -S "%~dp0tests" -B "%~dp0build" || exit /b 1
cmake --build "%~dp0build" --target ldp1 ldp1_tests || exit /b 1
"%~dp0build\ldp1_tests.exe" || exit /b 1
echo Library: %~dp0build\ldp1.lib
exit /b 0
