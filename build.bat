@echo off
setlocal DisableDelayedExpansion
title DOLBUTO Build
set "taskPowerShell=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "taskPowerShell=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%taskPowerShell%" (
  echo Windows PowerShell is required to prepare the build tools.
  exit /b 1
)
if /i "%~1"=="--check" goto check
if /i "%~1"=="--no-download" goto no_download
if not "%~1"=="" (
  echo Usage: build.bat [--check ^| --no-download]
  exit /b 1
)
"%taskPowerShell%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup.ps1" -Build -Package
set "taskExitCode=%errorlevel%"
echo.
echo Press any key to close this window.
pause >nul
exit /b %taskExitCode%

:check
"%taskPowerShell%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup.ps1" -Check
exit /b %errorlevel%

:no_download
"%taskPowerShell%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup.ps1" -Build -Package -NoDownload
exit /b %errorlevel%
