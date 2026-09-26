param([ValidateSet('dev','profile','release')][string]$Preset = 'profile')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskRenderDoc = Join-Path $taskRoot '.tools/renderdoc-1.46/RenderDoc_1.46_64'
if(-not (Test-Path "$taskRenderDoc/renderdoc.dll")){throw 'Run tools/setup-diagnostics.ps1 first.'}
$taskOldPaths = $env:VK_ADD_LAYER_PATH
$taskOldLayers = $env:VK_INSTANCE_LAYERS
$taskOldEnable = $env:ENABLE_VULKAN_RENDERDOC_CAPTURE
$taskOldDll = $env:SANDBOX_RENDERDOC_DLL
try {
    $env:VK_ADD_LAYER_PATH = "$taskRenderDoc;$taskOldPaths"
    $env:VK_INSTANCE_LAYERS = if($taskOldLayers){"VK_LAYER_RENDERDOC_Capture;$taskOldLayers"}else{'VK_LAYER_RENDERDOC_Capture'}
    $env:ENABLE_VULKAN_RENDERDOC_CAPTURE = '1'
    $env:SANDBOX_RENDERDOC_DLL = "$taskRenderDoc/renderdoc.dll"
    & "$taskRoot/build/$Preset/bin/sandbox.exe" --frames 60 --debug-ui --rdc "$taskRoot/build/$Preset/captures/sandbox"
    if($LASTEXITCODE -ne 0){throw 'RenderDoc capture failed.'}
    Get-ChildItem "$taskRoot/build/$Preset/captures" -Filter '*.rdc' | Select-Object FullName,Length
} finally {
    $env:VK_ADD_LAYER_PATH = $taskOldPaths
    $env:VK_INSTANCE_LAYERS = $taskOldLayers
    $env:ENABLE_VULKAN_RENDERDOC_CAPTURE = $taskOldEnable
    $env:SANDBOX_RENDERDOC_DLL = $taskOldDll
}
