@echo off
setlocal
rem Builds the agentic event bus and its tests. See .claude/specs/agentic-event-bus.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere not found.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo Visual C++ tools not found.
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -S "%~dp0." -B "%~dp0build" || exit /b 1
cmake --build "%~dp0build" --target synapse_bus_tests || exit /b 1
"%~dp0build\synapse_bus_tests.exe" || exit /b 1
exit /b 0
