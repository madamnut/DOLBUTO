$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path -Parent $PSScriptRoot
$taskLlvm = Join-Path $taskProjectRoot '.tools/llvm-23.1.0/bin'
if (-not (Test-Path "$taskLlvm/clang-cl.exe")) { throw 'Run tools/setup.ps1 first to prepare LLVM.' }
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $taskVswhere)) { throw 'Install Visual Studio Build Tools with the Desktop development with C++ workload.' }
$taskVsRoot = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $taskVsRoot) { throw 'MSVC standard library and Windows SDK are required by clang-cl.' }
& "$taskVsRoot/Common7/Tools/Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$taskCmakeBin = Join-Path $taskVsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$taskNinjaBin = Join-Path $taskVsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$env:PATH = "$taskLlvm;$taskCmakeBin;$taskNinjaBin;$env:PATH"
if (-not $env:VULKAN_SDK -or -not (Test-Path "$env:VULKAN_SDK/Bin/glslc.exe")) { throw 'Install the Vulkan SDK (including shader compiler and validation layers).' }
$env:PATH = "$env:VULKAN_SDK/Bin;$env:PATH"

