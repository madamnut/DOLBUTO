# Shared, read-only detection. Compatible with Windows PowerShell 5.1.
$taskProjectRoot = Split-Path -Parent $PSScriptRoot
$taskToolVersions = @{ PowerShell = '7.6.5'; LLVM = '23.1.0'; Vulkan = '1.4.341.1' }

function Get-CommandPath {
    param([string]$Name)
    $taskCommand = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($taskCommand) { return $taskCommand.Source }
}

function Find-Tool {
    param([string[]]$Paths, [version]$Minimum, [version]$Exact, [string[]]$Arguments = @('--version'))
    foreach ($taskPath in ($Paths | Where-Object { $_ } | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $taskPath -PathType Leaf)) { continue }
        try {
            $taskText = (& $taskPath @Arguments 2>&1 | Out-String)
            if ($LASTEXITCODE -ne 0 -or $taskText -notmatch '(\d+\.\d+\.\d+(?:\.\d+)?)') { continue }
            $taskVersion = [version]$Matches[1]
            if ((-not $Minimum -or $taskVersion -ge $Minimum) -and (-not $Exact -or $taskVersion -eq $Exact)) {
                return [pscustomobject]@{ Path = $taskPath; Version = $taskVersion.ToString() }
            }
        } catch { }
    }
    return $null
}

function Get-BuildInventory {
    $taskPwsh = Find-Tool -Paths @(
        "$taskProjectRoot/.tools/powershell-$($taskToolVersions.PowerShell)/pwsh.exe",
        (Get-CommandPath 'pwsh.exe'), "$env:ProgramFiles/PowerShell/7/pwsh.exe"
    ) -Minimum '7.0.0' -Arguments @('-NoLogo', '-NoProfile', '-Command', '$PSVersionTable.PSVersion.ToString()')
    $taskLlvm = Find-Tool -Paths @(
        "$taskProjectRoot/.tools/llvm-$($taskToolVersions.LLVM)/bin/clang-cl.exe", (Get-CommandPath 'clang-cl.exe')
    ) -Exact $taskToolVersions.LLVM
    if ($taskLlvm -and -not (Test-Path -LiteralPath (Join-Path (Split-Path $taskLlvm.Path) 'lld-link.exe'))) { $taskLlvm = $null }
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $taskVs = $null; $taskBuildTools = $null; $taskInstances = @()
    if (Test-Path -LiteralPath $taskVswhere) {
        $taskInstances = & $taskVswhere -all -products '*' -version '[17.0,)' -format json -utf8 | ConvertFrom-Json
        # Windows PowerShell 5.1 emits JSON arrays as one pipeline object.
        $taskInstances = @($taskInstances) | Sort-Object -Property @{ Expression = {
            (Test-Path -LiteralPath "$($_.installationPath)/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe") -and
            (Test-Path -LiteralPath "$($_.installationPath)/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe")
        }; Descending = $true }, @{ Expression = { [version]$_.installationVersion }; Descending = $true }
        $taskBuildTools = $taskInstances | Where-Object { $_.productId -eq 'Microsoft.VisualStudio.Product.BuildTools' -and $_.installationVersion -like '17.*' } | Select-Object -First 1
        foreach ($taskInstance in $taskInstances) {
            $taskVsPath = $taskInstance.installationPath
            $taskVersionFile = "$taskVsPath/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt"
            if (-not $taskInstance.isComplete -or -not (Test-Path -LiteralPath $taskVersionFile)) { continue }
            $taskMsvcVersion = (Get-Content -LiteralPath $taskVersionFile -Raw).Trim()
            if ((Test-Path -LiteralPath "$taskVsPath/VC/Tools/MSVC/$taskMsvcVersion/include/vector") -and
                (Test-Path -LiteralPath "$taskVsPath/VC/Tools/MSVC/$taskMsvcVersion/lib/x64/msvcrt.lib") -and
                (Test-Path -LiteralPath "$taskVsPath/Common7/Tools/Launch-VsDevShell.ps1")) {
                $taskVs = [pscustomobject]@{ Path = $taskVsPath; Version = $taskMsvcVersion }
                break
            }
        }
    }
    $taskSdk = $null
    $taskKitsRoots = @("${env:ProgramFiles(x86)}/Windows Kits/10")
    foreach ($taskReg in @('HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots')) {
        $taskKits = Get-ItemProperty -LiteralPath $taskReg -Name KitsRoot10 -ErrorAction SilentlyContinue
        if ($taskKits) { $taskKitsRoots += $taskKits.KitsRoot10 }
    }
    foreach ($taskKitsRoot in ($taskKitsRoots | Select-Object -Unique)) {
        foreach ($taskSdkDir in (Get-ChildItem -LiteralPath "$taskKitsRoot/Include" -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending)) {
            if ($taskSdkDir.Name -notmatch '^10\.0\.\d+\.0$' -or [version]$taskSdkDir.Name -lt [version]'10.0.19041.0') { continue }
            $taskSdkVersion = $taskSdkDir.Name
            $taskSdkFiles = @("Include/$taskSdkVersion/um/Windows.h", "Include/$taskSdkVersion/ucrt/stdio.h", "Lib/$taskSdkVersion/um/x64/kernel32.lib", "Lib/$taskSdkVersion/ucrt/x64/ucrt.lib", "bin/$taskSdkVersion/x64/rc.exe")
            if (@($taskSdkFiles | Where-Object { -not (Test-Path -LiteralPath "$taskKitsRoot/$_") }).Count -eq 0) {
                $taskSdk = [pscustomobject]@{ Path = $taskKitsRoot; Version = $taskSdkVersion }; break
            }
        }
        if ($taskSdk) { break }
    }
    $taskCmakePaths = @(); $taskNinjaPaths = @()
    if ($taskVs) {
        $taskCmakePaths += "$($taskVs.Path)/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        $taskNinjaPaths += "$($taskVs.Path)/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
    }
    foreach ($taskInstance in $taskInstances) {
        if (-not $taskInstance.isComplete) { continue }
        $taskCmakePaths += "$($taskInstance.installationPath)/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        $taskNinjaPaths += "$($taskInstance.installationPath)/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
    }
    $taskCmakePaths += (Get-CommandPath 'cmake.exe'); $taskNinjaPaths += (Get-CommandPath 'ninja.exe')
    $taskCmake = Find-Tool -Paths $taskCmakePaths -Minimum '3.26.0'
    $taskNinja = Find-Tool -Paths $taskNinjaPaths -Minimum '1.10.0'
    $taskVulkan = $null
    $taskVulkanRoots = @($env:VULKAN_SDK, [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'User'), [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'Machine'))
    $taskVulkanRoots += @(Get-ChildItem -LiteralPath "$env:SystemDrive/VulkanSDK" -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | ForEach-Object FullName)
    foreach ($taskVulkanRoot in ($taskVulkanRoots | Where-Object { $_ } | Select-Object -Unique)) {
        $taskHeader = "$taskVulkanRoot/Include/vulkan/vulkan_core.h"
        if (-not (Test-Path -LiteralPath $taskHeader)) { continue }
        $taskHeaderText = Get-Content -LiteralPath $taskHeader -Raw
        if ($taskHeaderText -notmatch '#define VK_HEADER_VERSION_COMPLETE VK_MAKE_API_VERSION\(\s*0,\s*(\d+),\s*(\d+),') { continue }
        $taskMajor = $Matches[1]; $taskMinor = $Matches[2]
        if ($taskHeaderText -notmatch '#define VK_HEADER_VERSION\s+(\d+)') { continue }
        $taskVulkanVersion = [version]"$taskMajor.$taskMinor.$($Matches[1])"
        if ($taskVulkanVersion -lt [version]'1.4.341') { continue }
        if ((Find-Tool -Paths @("$taskVulkanRoot/Bin/glslc.exe")) -and
            (Test-Path -LiteralPath "$taskVulkanRoot/Bin/VkLayer_khronos_validation.json") -and
            (Test-Path -LiteralPath "$taskVulkanRoot/Bin/VkLayer_khronos_validation.dll")) {
            $taskVulkan = [pscustomobject]@{ Path = $taskVulkanRoot; Version = $taskVulkanVersion.ToString() }; break
        }
    }
    $taskDependencies = Get-Content -LiteralPath "$taskProjectRoot/dependencies.lock.json" -Raw | ConvertFrom-Json
    $taskDependencies = @($taskDependencies)
    $taskMissingDependencies = @($taskDependencies | Where-Object {
        $taskMarker = "$taskProjectRoot/.cache/deps/$($_.name)/.source-id"
        -not (Test-Path -LiteralPath $taskMarker) -or (Get-Content -LiteralPath $taskMarker -Raw).Trim() -ne $_.commit
    })
    return [pscustomobject]@{
        PowerShell = $taskPwsh; LLVM = $taskLlvm; VisualStudio = $taskVs; BuildTools = $taskBuildTools
        WindowsSDK = $taskSdk; CMake = $taskCmake; Ninja = $taskNinja; Vulkan = $taskVulkan
        Dependencies = $taskDependencies; MissingDependencies = $taskMissingDependencies
    }
}

function Show-BuildInventory {
    param($Inventory)
    Write-Host "`nDOLBUTO 빌드 환경 (Windows x64)" -ForegroundColor Cyan
    foreach ($taskEntry in @(
        @('PowerShell', 'PowerShell 7 이상'), @('VisualStudio', 'Visual Studio 2022 이상 C++ 도구 / MSVC STL'),
        @('WindowsSDK', 'Windows SDK 10.0.19041 이상'), @('CMake', 'CMake 3.26 이상'),
        @('Ninja', 'Ninja 1.10 이상'), @('LLVM', "LLVM $($taskToolVersions.LLVM)"),
        @('Vulkan', 'Vulkan SDK 1.4.341 이상 / glslc / 검증 레이어')
    )) {
        $taskFound = $Inventory.($taskEntry[0])
        if ($taskFound) { Write-Host "[준비됨] $($taskEntry[1]) : $($taskFound.Version) ($($taskFound.Path))" }
        else { Write-Host "[준비 필요] $($taskEntry[1]) : 미설치, 버전 불일치 또는 불완전한 설치" -ForegroundColor Yellow }
    }
    foreach ($taskDep in $Inventory.Dependencies) {
        $taskStatus = if ($Inventory.MissingDependencies.name -contains $taskDep.name) { '준비 필요' } else { '준비됨' }
        Write-Host "[$taskStatus] 라이브러리 $($taskDep.name) $($taskDep.version)"
    }
}

function Test-BuildInventory {
    param($Inventory)
    return ($null -ne $Inventory.PowerShell -and $null -ne $Inventory.VisualStudio -and $null -ne $Inventory.WindowsSDK -and
        $null -ne $Inventory.CMake -and $null -ne $Inventory.Ninja -and $null -ne $Inventory.LLVM -and
        $null -ne $Inventory.Vulkan -and $Inventory.MissingDependencies.Count -eq 0)
}
