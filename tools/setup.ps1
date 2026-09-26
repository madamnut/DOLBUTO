$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
Push-Location $taskRoot
try {
    New-Item -ItemType Directory -Path '.cache', '.tools' -Force | Out-Null
    $taskLlvmArchive = '.cache/llvm-23.1.0.tar.zst'
    $taskLlvmHash = '1aebf024b959b3835c3bd936da2fa58cd002c61ccf47fce1714447b900bd9837'
    if (-not (Test-Path '.tools/llvm-23.1.0/bin/clang-cl.exe')) {
        if (-not (Test-Path $taskLlvmArchive)) {
            & curl.exe -L --fail --silent --show-error --retry 3 -o $taskLlvmArchive 'https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.0/clang%2Bllvm-23.1.0-x86_64-pc-windows-msvc.tar.zst'
            if ($LASTEXITCODE -ne 0) { throw 'LLVM download failed.' }
        }
        if ((Get-FileHash $taskLlvmArchive).Hash.ToLowerInvariant() -ne $taskLlvmHash) { throw 'LLVM checksum mismatch.' }
        New-Item -ItemType Directory -Path '.tools/llvm-23.1.0' -Force | Out-Null
        & tar.exe -xf $taskLlvmArchive -C '.tools/llvm-23.1.0' --strip-components=1
        if ($LASTEXITCODE -ne 0) { throw 'LLVM extraction failed.' }
    }
    foreach ($taskDep in (Get-Content 'dependencies.lock.json' -Raw | ConvertFrom-Json)) {
        $taskDir = ".cache/deps/$($taskDep.name)"
        if (Test-Path "$taskDir/.source-id") {
            if ((Get-Content "$taskDir/.source-id" -Raw).Trim() -ne $taskDep.commit) { throw "Dependency version mismatch: $($taskDep.name)" }
            continue
        }
        $taskArchive = ".cache/$($taskDep.name).tar.gz"
        if (-not (Test-Path $taskArchive)) {
            & curl.exe -L --fail --silent --show-error --retry 3 -o $taskArchive $taskDep.url
            if ($LASTEXITCODE -ne 0) { throw "Download failed: $($taskDep.name)" }
        }
        if ((Get-FileHash $taskArchive).Hash.ToLowerInvariant() -ne $taskDep.sha256) { throw "Checksum mismatch: $($taskDep.name)" }
        New-Item -ItemType Directory -Path $taskDir -Force | Out-Null
        & tar.exe -xf $taskArchive -C $taskDir --strip-components=1
        if ($LASTEXITCODE -ne 0) { throw "Extraction failed: $($taskDep.name)" }
        Set-Content "$taskDir/.source-id" -Value $taskDep.commit -NoNewline
        Write-Host "Prepared $($taskDep.name) $($taskDep.version)"
    }
    . "$PSScriptRoot/environment.ps1"
    & clang-cl --version
    & cmake --version
    Write-Host 'Setup complete. Run tools/build.ps1, then tools/run.ps1.'
} finally { Pop-Location }

