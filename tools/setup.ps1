param(
    [switch]$Check,
    [switch]$NoDownload,
    [switch]$Build,
    [switch]$Package,
    [ValidateSet('dev','profile','release')][string]$Preset = 'release'
)
$ErrorActionPreference = 'Stop'
try {
    if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitProcess -or
        $env:PROCESSOR_ARCHITECTURE -ne 'AMD64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') {
        throw '현재 자동 준비는 Windows x64만 지원합니다. 64비트 PowerShell에서 실행하세요.'
    }
    if ($Package -and (-not $Build -or $Preset -ne 'release')) { throw '-Package는 -Build -Preset release와 함께 사용하세요.' }
    . "$PSScriptRoot/toolchain.ps1"
    $taskInventory = Get-BuildInventory
    Show-BuildInventory $taskInventory
    $taskReady = Test-BuildInventory $taskInventory
    if ($Check) {
        if ($taskReady) { exit 0 }
        exit 1
    }
    if (-not $taskReady) {
        if ($NoDownload) { Write-Host '다운로드 금지 모드: 환경이 준비되지 않아 중단합니다.'; exit 2 }
        if ((-not $taskInventory.LLVM -or $taskInventory.MissingDependencies.Count -gt 0) -and -not (Get-CommandPath 'tar.exe')) {
            throw 'Windows 기본 tar.exe가 필요합니다. Windows 10/11을 업데이트한 뒤 다시 실행하세요.'
        }
        Write-Host "`n준비 계획" -ForegroundColor Cyan
        Write-Host "로컬 도구/라이브러리: $taskProjectRoot\.tools 및 .cache (Git 업로드 제외)"
        if (-not $taskInventory.PowerShell) { Write-Host "- PowerShell $($taskToolVersions.PowerShell): 프로젝트 .tools에 ZIP 압축 해제 (관리자 권한 불필요)" }
        if (-not $taskInventory.LLVM) { Write-Host "- LLVM $($taskToolVersions.LLVM): 프로젝트 .tools에 압축 해제 (관리자 권한 불필요)" }
        $taskNeedVs = -not $taskInventory.VisualStudio -or -not $taskInventory.WindowsSDK -or -not $taskInventory.CMake -or -not $taskInventory.Ninja
        $taskVsInstallPath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
        if ($taskInventory.BuildTools) { $taskVsInstallPath = $taskInventory.BuildTools.installationPath }
        if ($taskNeedVs) {
            Write-Host "- Microsoft Build Tools 2022: $taskVsInstallPath (시스템 설치 / 관리자 권한 필요)"
            Write-Host '  C++ 도구, Windows SDK 22621, CMake, Ninja 및 런타임을 추가합니다. 기존 Build Tools 2022가 있으면 해당 설치를 보완합니다.'
            Write-Host '  2022 채널의 최신 서비스 버전을 사용합니다. SDK 및 설치 캐시는 시스템 공용 위치에도 저장됩니다.'
        }
        if (-not $taskInventory.Vulkan) { Write-Host "- Vulkan SDK $($taskToolVersions.Vulkan): $env:SystemDrive\VulkanSDK\$($taskToolVersions.Vulkan) (시스템 설치 / 관리자 권한 필요 / 환경 변수 및 검증 레이어 등록)" }
        Write-Host '- 필요한 라이브러리만 dependencies.lock.json의 지정 버전으로 준비합니다.'
        Write-Host '공식 Microsoft/GitHub/LunarG 배포처를 사용합니다. 전체 준비에는 수 GB 이상의 다운로드와 디스크 공간이 필요할 수 있습니다.'
        Write-Host '동의하면 위 도구를 다운로드하고 설치하며, 설치 프로그램의 라이선스에 동의하는 옵션을 사용합니다.'
        Write-Host '라이선스 안내: https://visualstudio.microsoft.com/license-terms/ / https://vulkan.lunarg.com/ / 각 라이브러리 LICENSE'
        $taskAnswer = Read-Host '다운로드 및 설치할까요? [y/N] (Enter: 취소)'
        if ($taskAnswer -notmatch '^(?i:y|yes|예)$') { Write-Host '취소했습니다. 다운로드나 설치를 하지 않았습니다.'; exit 2 }
        . "$PSScriptRoot/download-tools.ps1"
        Install-BuildTools -Inventory $taskInventory -NeedVisualStudio $taskNeedVs -VisualStudioPath $taskVsInstallPath
        $taskInventory = Get-BuildInventory
        Show-BuildInventory $taskInventory
        if (-not (Test-BuildInventory $taskInventory)) { throw '준비 후에도 부족한 항목이 있습니다. 위 목록을 확인하고 설치를 복구한 뒤 다시 실행하세요.' }
    }
    Write-Host "`n빌드 환경이 준비되었습니다." -ForegroundColor Green
    if ($Build) {
        & $taskInventory.PowerShell.Path -NoLogo -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/build.ps1" -Preset $Preset -NoDownload
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        if ($Package) {
            & $taskInventory.PowerShell.Path -NoLogo -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/package.ps1"
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
    }
    exit 0
} catch {
    Write-Host "`n준비/빌드 중단: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host '직접 설치하거나 문제를 해결한 뒤 build.bat를 다시 실행하세요. 검사만 하려면 build.bat --check를 사용하세요.'
    exit 1
}
