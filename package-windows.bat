@echo off
setlocal
title VanishingPoint - Windows Release Package

echo Building and packaging VanishingPoint...
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package-windows.ps1"
set "packageExit=%errorlevel%"
echo.
if "%packageExit%"=="0" (
    echo Packaging completed.
    echo Output folder: "%~dp0dist"
) else (
    echo Packaging failed. Exit code: %packageExit%
    echo Check the messages above and logs in "%~dp0build\package-release".
)
echo.
pause
exit /b %packageExit%
