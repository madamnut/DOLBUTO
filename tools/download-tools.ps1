# Loaded only after the user accepts the complete preparation plan.
function Save-ToolDownload {
    param([string]$Url, [string]$Path, [string]$Sha256, [string]$Publisher)
    New-Item -ItemType Directory -Path (Split-Path $Path) -Force | Out-Null
    if (Test-Path -LiteralPath $Path) {
        if ($Sha256 -and (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Sha256) { return }
        if ($Publisher) {
            $taskSignature = Get-AuthenticodeSignature -LiteralPath $Path
            if ($taskSignature.Status -eq 'Valid' -and $taskSignature.SignerCertificate.Subject -match $Publisher) { return }
        }
    }
    $taskPartial = "$Path.partial"
    Write-Host "다운로드: $Url"
    $taskOldProgress = $ProgressPreference
    try {
        $ProgressPreference = 'SilentlyContinue'
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $taskPartial
        if ($Sha256 -and (Get-FileHash -LiteralPath $taskPartial -Algorithm SHA256).Hash -ne $Sha256) { throw "SHA-256 불일치: $Url" }
        if ($Publisher) {
            # Authenticode needs the original .exe extension.
            $taskSignedFile = "$Path.pending.exe"
            Move-Item -LiteralPath $taskPartial -Destination $taskSignedFile -Force
            $taskPartial = $taskSignedFile
            $taskSignature = Get-AuthenticodeSignature -LiteralPath $taskPartial
            if ($taskSignature.Status -ne 'Valid' -or $taskSignature.SignerCertificate.Subject -notmatch $Publisher) {
                throw "설치 파일의 게시자 서명을 확인할 수 없습니다: $Url"
            }
        }
        Move-Item -LiteralPath $taskPartial -Destination $Path -Force
    } finally {
        $ProgressPreference = $taskOldProgress
        if (Test-Path -LiteralPath $taskPartial) { Remove-Item -LiteralPath $taskPartial -Force }
    }
}

function Invoke-ToolInstaller {
    param([string]$Path, [string[]]$Arguments)
    Write-Host '시스템 도구 설치 중입니다. Windows 관리자 권한 확인 창이 나타날 수 있습니다.'
    $taskProcess = Start-Process -FilePath $Path -ArgumentList $Arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
    if ($taskProcess.ExitCode -in @(3010, 1641)) { throw '설치를 완료하려면 재부팅이 필요합니다. 작업을 저장하고 재부팅한 뒤 build.bat를 다시 실행하세요.' }
    if ($taskProcess.ExitCode -ne 0) { throw "설치 실패/취소 (종료 코드 $($taskProcess.ExitCode)). 설치 로그와 디스크 공간을 확인하고 다시 실행하세요." }
}

function Install-BuildTools {
    param($Inventory, [bool]$NeedVisualStudio, [string]$VisualStudioPath)
    if (-not $Inventory.PowerShell) {
        $taskArchive = "$taskProjectRoot/.cache/PowerShell-$($taskToolVersions.PowerShell)-win-x64.zip"
        Save-ToolDownload "https://github.com/PowerShell/PowerShell/releases/download/v$($taskToolVersions.PowerShell)/PowerShell-$($taskToolVersions.PowerShell)-win-x64.zip" $taskArchive '32eb8f6cdce08f86e987d625a2733e54ac3e289ae7e1621b14c0b5bcec2434ea'
        Expand-Archive -LiteralPath $taskArchive -DestinationPath "$taskProjectRoot/.tools/powershell-$($taskToolVersions.PowerShell)" -Force
    }
    if ($NeedVisualStudio) {
        $taskInstaller = "$taskProjectRoot/.cache/vs_buildtools-2022.exe"
        Save-ToolDownload 'https://aka.ms/vs/17/release/vs_buildtools.exe' $taskInstaller -Publisher 'O=Microsoft Corporation(?:,|$)'
        $taskInstallArgs = @('--installPath', "`"$VisualStudioPath`"", '--quiet', '--wait', '--norestart',
            '--add', 'Microsoft.VisualStudio.Workload.VCTools', '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
            '--add', 'Microsoft.VisualStudio.Component.VC.CMake.Project', '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.22621')
        if ($Inventory.BuildTools) { $taskInstallArgs = @('modify', '--channelId', 'VisualStudio.17.Release') + $taskInstallArgs }
        Invoke-ToolInstaller $taskInstaller $taskInstallArgs
    }
    if (-not $Inventory.Vulkan) {
        $taskInstaller = "$taskProjectRoot/.cache/vulkansdk-$($taskToolVersions.Vulkan).exe"
        Save-ToolDownload "https://sdk.lunarg.com/sdk/download/$($taskToolVersions.Vulkan)/windows/vulkansdk-windows-X64-$($taskToolVersions.Vulkan).exe" $taskInstaller -Publisher 'O=("?LunarG,? Inc\.?"?)(?:,|$)'
        Invoke-ToolInstaller $taskInstaller @('--root', "`"$env:SystemDrive\VulkanSDK\$($taskToolVersions.Vulkan)`"", '--accept-licenses', '--default-answer', '--confirm-command', 'install')
    }
    $taskTar = Get-CommandPath 'tar.exe'
    if (-not $Inventory.LLVM) {
        $taskArchive = "$taskProjectRoot/.cache/llvm-$($taskToolVersions.LLVM).tar.zst"
        Save-ToolDownload "https://github.com/llvm/llvm-project/releases/download/llvmorg-$($taskToolVersions.LLVM)/clang%2Bllvm-$($taskToolVersions.LLVM)-x86_64-pc-windows-msvc.tar.zst" $taskArchive '1aebf024b959b3835c3bd936da2fa58cd002c61ccf47fce1714447b900bd9837'
        $taskDestination = "$taskProjectRoot/.tools/llvm-$($taskToolVersions.LLVM)"
        New-Item -ItemType Directory -Path $taskDestination -Force | Out-Null
        & $taskTar -xf $taskArchive -C $taskDestination --strip-components=1
        if ($LASTEXITCODE -ne 0) { throw 'LLVM 압축 해제 실패. 디스크 공간과 Windows tar의 zstd 지원을 확인하세요.' }
    }
    foreach ($taskDep in $Inventory.MissingDependencies) {
        $taskDestination = "$taskProjectRoot/.cache/deps/$($taskDep.name)"
        # Preserve mismatched/incomplete sources instead of mixing versions.
        if (Test-Path -LiteralPath $taskDestination) {
            $taskBackup = "$taskDestination.backup-$([guid]::NewGuid().ToString('N'))"
            $taskResolved = [IO.Path]::GetFullPath($taskDestination)
            $taskAllowed = [IO.Path]::GetFullPath("$taskProjectRoot/.cache/deps") + [IO.Path]::DirectorySeparatorChar
            $taskBackupResolved = [IO.Path]::GetFullPath($taskBackup)
            if (-not $taskResolved.StartsWith($taskAllowed, [StringComparison]::OrdinalIgnoreCase) -or
                -not $taskBackupResolved.StartsWith($taskAllowed, [StringComparison]::OrdinalIgnoreCase)) { throw '의존성 경로가 프로젝트 캐시 밖입니다.' }
            Move-Item -LiteralPath $taskResolved -Destination $taskBackupResolved
            Write-Host "기존 소스 보관: $taskBackupResolved"
        }
        $taskArchive = "$taskProjectRoot/.cache/$($taskDep.name)-$($taskDep.commit).tar.gz"
        Save-ToolDownload $taskDep.url $taskArchive $taskDep.sha256
        New-Item -ItemType Directory -Path $taskDestination -Force | Out-Null
        & $taskTar -xf $taskArchive -C $taskDestination --strip-components=1
        if ($LASTEXITCODE -ne 0) { throw "라이브러리 압축 해제 실패: $($taskDep.name)" }
        Set-Content -LiteralPath "$taskDestination/.source-id" -Value $taskDep.commit -NoNewline -Encoding Ascii
    }
}
