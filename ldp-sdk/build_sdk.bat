@echo off
setlocal
rem Builds the SDK and runs the ten-minute loopback check. See README.md.
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
cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -S "%~dp0." -B "%~dp0build" || exit /b 1
cmake --build "%~dp0build" --target ldp_try ldp_bench ldp_c ldp_c_tests || exit /b 1
"%~dp0build\ldp-try.exe" || exit /b 1
"%~dp0build\ldp_c_tests.exe" || exit /b 1
echo SDK: %~dp0build\ldp-try.exe, %~dp0build\ldp-bench.exe and %~dp0build\ldp_c.dll
exit /b 0
