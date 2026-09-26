$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
Push-Location $taskRoot
try {
    New-Item -ItemType Directory -Path '.cache','.tools' -Force | Out-Null
    $taskPackages = @(
        @{Name='tracy-0.14.1'; Archive='tracy-windows.zip'; Hash='f7499d74914aa3ba94a2c1ce72f36477d7b61d9d0f7c9790e05274c258c97fb5'; Url='https://github.com/wolfpld/tracy/releases/download/v0.14.1/windows-0.14.1.zip'},
        @{Name='renderdoc-1.46'; Archive='renderdoc.zip'; Hash='9ca4d09ecaba2cc791168660d6fc2a7e3d70fe67146e87ec05c5f8dea70772f7'; Url='https://renderdoc.org/stable/1.46/RenderDoc_1.46_64.zip'}
    )
    foreach($taskPackage in $taskPackages) {
        $taskArchive = ".cache/$($taskPackage.Archive)"
        if (-not (Test-Path $taskArchive)) {
            & curl.exe -L --fail --silent --show-error --retry 3 -o $taskArchive $taskPackage.Url
            if($LASTEXITCODE -ne 0){throw "Download failed: $($taskPackage.Name)"}
        }
        if((Get-FileHash $taskArchive).Hash.ToLowerInvariant() -ne $taskPackage.Hash){throw "Checksum mismatch: $($taskPackage.Name)"}
        Expand-Archive -LiteralPath $taskArchive -DestinationPath ".tools/$($taskPackage.Name)" -Force
    }
    Write-Host 'RenderDoc and Tracy are ready. No global Vulkan layer registration was changed.'
} finally { Pop-Location }
