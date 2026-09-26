@echo off
setlocal DisableDelayedExpansion
title Sandbox Build
set "taskExitCode=1"
set "taskPowerShell=pwsh.exe"

where /q pwsh.exe
if not errorlevel 1 goto powershell_ready
set "taskPowerShell=%ProgramFiles%\PowerShell\7\pwsh.exe"
if exist "%taskPowerShell%" goto powershell_ready
set "taskPowerShell=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe"
if exist "%taskPowerShell%" goto powershell_ready
echo PowerShell 7 is required. Install it, then run this file again.
goto finish

:powershell_ready
if /i "%~1"=="--check" goto check_powershell
pushd "%~dp0"
if errorlevel 1 goto directory_failed

echo [1/2] Building release...
"%taskPowerShell%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "tools\build.ps1" -Preset release
set "taskExitCode=%errorlevel%"
if not "%taskExitCode%"=="0" goto failed

echo [2/2] Updating the game folder...
"%taskPowerShell%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "tools\package.ps1"
set "taskExitCode=%errorlevel%"
if not "%taskExitCode%"=="0" goto failed

echo.
echo Build complete.
echo Ready: "%CD%\out\Sandbox\sandbox.exe"
popd
goto finish

:failed
echo.
echo Build stopped. Check the error messages above.
popd
goto finish

:directory_failed
echo Cannot open the project folder.
goto finish

:check_powershell
echo PowerShell: "%taskPowerShell%"
"%taskPowerShell%" -NoLogo -NoProfile -Command "if ($PSVersionTable.PSVersion.Major -lt 7) { exit 1 }; $PSVersionTable.PSVersion.ToString()"
exit /b %errorlevel%

:finish
if /i "%~1"=="--check" exit /b %taskExitCode%
echo.
echo Press any key to close this window.
pause >nul
exit /b %taskExitCode%
