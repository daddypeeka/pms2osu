@echo off
rem pms2osu-v2 - one click build (expects w64devkit at ..\.tools\w64devkit)
setlocal
set W64=%~dp0..\.tools\w64devkit\w64devkit\bin
if not exist "%W64%\make.exe" (
    echo w64devkit not found at %W64%
    pause
    exit /b 1
)
set PATH=%W64%;%PATH%
"%W64%\make" -f Makefile %*
if errorlevel 1 (
    echo.
    echo Build failed.
    pause
)
endlocal