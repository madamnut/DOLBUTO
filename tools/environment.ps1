$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/toolchain.ps1"
$taskEnvironment = Get-BuildInventory
if (-not (Test-BuildInventory $taskEnvironment)) {
    Show-BuildInventory $taskEnvironment
    throw 'Build tools are missing or incompatible. Run build.bat or tools/setup.ps1 to review and accept preparation.'
}
$taskVsRoot = $taskEnvironment.VisualStudio.Path
& "$taskVsRoot/Common7/Tools/Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
if (-not $env:INCLUDE -or -not $env:LIB -or -not (Get-Command rc.exe -ErrorAction SilentlyContinue)) {
    throw 'The Visual Studio developer environment is incomplete. Repair the C++ tools and Windows SDK in Visual Studio Installer.'
}
$taskLlvm = Split-Path $taskEnvironment.LLVM.Path
$taskCmakeBin = Split-Path $taskEnvironment.CMake.Path
$taskNinjaBin = Split-Path $taskEnvironment.Ninja.Path
$env:VULKAN_SDK = $taskEnvironment.Vulkan.Path
# Only this process and its children are changed. Re-discover system variables after installation.
$env:PATH = "$taskLlvm;$taskCmakeBin;$taskNinjaBin;$env:VULKAN_SDK/Bin;$env:PATH"
