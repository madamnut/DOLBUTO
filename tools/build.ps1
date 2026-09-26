param([ValidateSet('dev','profile','release')][string]$Preset = 'dev')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/environment.ps1"
Push-Location $taskProjectRoot
try {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed.' }
    & cmake --build --preset $Preset --parallel 8
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
} finally { Pop-Location }

