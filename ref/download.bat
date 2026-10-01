@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tools\download-references.ps1" %*
set "REF_EXIT=%ERRORLEVEL%"
echo.
if not "%REF_EXIT%"=="0" echo Some references failed. See the messages above.
pause
exit /b %REF_EXIT%
