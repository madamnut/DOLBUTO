@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tools\setup-minecraft-reference.ps1" %*
set "REF_EXIT=%ERRORLEVEL%"
echo.
if not "%REF_EXIT%"=="0" echo Minecraft source setup failed. See the messages above.
pause
exit /b %REF_EXIT%
