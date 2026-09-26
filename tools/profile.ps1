param([ValidateSet('dev','profile')][string]$Preset = 'profile')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskCapture = Join-Path $taskRoot '.tools/tracy-0.14.1/tracy-capture.exe'
if(-not (Test-Path $taskCapture)){throw 'Run tools/setup-diagnostics.ps1 first.'}
$taskOutput = Join-Path $taskRoot "build/$Preset/captures"
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskProcess = Start-Process -FilePath $taskCapture -ArgumentList @('-o',"`"$taskOutput/sandbox.tracy`"",'-f','-a','127.0.0.1','-s','3') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$taskOutput/tracy.log" -RedirectStandardError "$taskOutput/tracy-errors.log"
try {
    & "$taskRoot/build/$Preset/bin/sandbox.exe" --seconds 5 --debug-ui
    if($LASTEXITCODE -ne 0){throw 'Profile run failed.'}
    if(-not $taskProcess.WaitForExit(15000)){throw 'Tracy capture timed out.'}
    if($taskProcess.ExitCode -ne 0){throw 'Tracy capture failed; inspect the capture logs.'}
    Get-Item "$taskOutput/sandbox.tracy" | Select-Object FullName,Length
} finally {
    if(-not $taskProcess.HasExited){$taskProcess.Kill(); $taskProcess.WaitForExit()}
    $taskProcess.Dispose()
}
