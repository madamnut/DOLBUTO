param([ValidateSet('dev','profile','release')][string]$Preset = 'dev', [string[]]$GameArgs = @())
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskExecutable = Join-Path $taskRoot "build/$Preset/bin/sandbox.exe"
if (-not (Test-Path $taskExecutable)) { throw 'Build the selected preset first with tools/build.ps1.' }
& $taskExecutable @GameArgs
if ($LASTEXITCODE -ne 0) { throw "Game exited with code $LASTEXITCODE" }

