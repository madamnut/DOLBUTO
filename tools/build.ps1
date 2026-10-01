param(
    [ValidateSet('dev','profile','release')][string]$Preset = 'dev',
    [switch]$NoDownload
)
$ErrorActionPreference = 'Stop'
# Route direct builds through the same consent UI. setup dispatches back with -NoDownload.
if (-not $NoDownload) {
    & (Join-Path $PSHOME $(if ($PSVersionTable.PSVersion.Major -ge 7) { 'pwsh.exe' } else { 'powershell.exe' })) -NoLogo -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/setup.ps1" -Build -Preset $Preset
    exit $LASTEXITCODE
}
. "$PSScriptRoot/environment.ps1"
Push-Location $taskProjectRoot
try {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed.' }
    & cmake --build --preset $Preset --parallel 8
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
} finally { Pop-Location }
