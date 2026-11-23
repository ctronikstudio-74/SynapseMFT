@echo off
echo This script builds the earlier draft library, not LDP-1.
echo The supported protocol build is in README.md.
echo.

if not exist build mkdir build
cd build

echo Configuring CMake...
cmake .. -G "Visual Studio 17 2022" -A x64

if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed!
    pause
    exit /b 1
)

echo Building...
cmake --build . --config Release

if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo Build successful!
echo Output: build\Release\ldp.lib (or build\Release\ldp.dll)
pause

