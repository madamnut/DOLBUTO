@echo off
setlocal DisableDelayedExpansion
if not exist "%~dp0out\DOLBUTO\worldgen_editor.exe" (
  echo Build the project first with build.bat.
  pause
  exit /b 1
)
"%~dp0out\DOLBUTO\worldgen_editor.exe"
if errorlevel 1 pause
