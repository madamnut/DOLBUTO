$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/environment.ps1"
$taskRoot = $taskProjectRoot
$taskSource = Join-Path $taskRoot 'build/release/bin'
$taskOutput = [System.IO.Path]::GetFullPath((Join-Path $taskRoot 'out/DOLBUTO'))
if(-not $taskOutput.StartsWith($taskRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay inside the project.'}
if(-not (Test-Path "$taskSource/DOLBUTO.exe")){throw 'Build the release preset first.'}
New-Item -ItemType Directory -Path "$taskOutput/assets","$taskOutput/shaders","$taskOutput/licenses" -Force | Out-Null
Copy-Item -LiteralPath "$taskSource/DOLBUTO.exe" -Destination $taskOutput -Force
Copy-Item -LiteralPath "$taskSource/worldgen_editor.exe" -Destination $taskOutput -Force
foreach($taskDirectory in @('assets','shaders')) {
    Get-ChildItem -LiteralPath "$taskSource/$taskDirectory" | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination "$taskOutput/$taskDirectory" -Recurse -Force
    }
}
# Remove only retired application assets; preserve user settings and backups.
foreach($taskRetired in @('minecraft.html','minecraft.js','minecraft.css','map-view.js')) {
    $taskRetiredPath = Join-Path $taskOutput "assets/editor/$taskRetired"
    if(Test-Path -LiteralPath $taskRetiredPath){Remove-Item -LiteralPath $taskRetiredPath -Force}
}
# App-local Microsoft runtime files keep this folder runnable without the build toolchain.
$taskCrt = Get-ChildItem -LiteralPath "$env:VCToolsRedistDir/x64" -Directory | Where-Object Name -Match '^Microsoft\.VC.*\.CRT$' | Select-Object -First 1
if(-not $taskCrt){throw 'Microsoft x64 CRT redistributable files were not found.'}
Get-ChildItem -LiteralPath $taskCrt.FullName -Filter '*.dll' | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $taskOutput -Force }
$taskNotices = @(
    @('fastnoise2/LICENSE','FastNoise2.txt'), @('fastsimd/LICENSE','FastSIMD.txt'),
    @('json/LICENSE.MIT','nlohmann-json.txt'), @('sdl/LICENSE.txt','SDL.txt'), @('rmlui/LICENSE.txt','RmlUi.txt'),
    @('rmlui/Include/RmlUi/Core/Containers/LICENSE.txt','RmlUi-containers.txt'),
    @('volk/LICENSE.md','volk.txt'), @('vma/LICENSE.txt','VMA.txt'),
    @('glm/copying.txt','GLM.txt'), @('imgui/LICENSE.txt','Dear-ImGui.txt'),
    @('stb/LICENSE','stb.txt'), @('freetype/docs/FTL.TXT','FreeType-FTL.txt'),
    @('freetype/LICENSE.TXT','FreeType-license.txt'), @('vulkan_headers/LICENSE.md','Vulkan-Headers.txt'),
    @('vulkan_headers/LICENSES/Apache-2.0.txt','Apache-2.0.txt'), @('vulkan_headers/LICENSES/MIT.txt','Vulkan-MIT.txt')
)
foreach($taskNotice in $taskNotices) {
    Copy-Item -LiteralPath (Join-Path $taskRoot ".cache/deps/$($taskNotice[0])") -Destination "$taskOutput/licenses/$($taskNotice[1])" -Force
}
Copy-Item -LiteralPath "$taskRoot/third_party/renderdoc/renderdoc_app.h" -Destination "$taskOutput/licenses/renderdoc_app.h" -Force
Copy-Item -LiteralPath "$taskRoot/README.md" -Destination "$taskOutput/README.md" -Force
@'
This software uses the FreeType library under the FreeType Project License (FTL).
Portions of this software are copyright (c) 2025 The FreeType Project (www.freetype.org). All rights reserved.
Noto Sans KR is distributed with its SIL Open Font License in assets/fonts/OFL.txt.
Other open-source notices are included in this licenses directory.
'@ | Set-Content -LiteralPath "$taskOutput/licenses/NOTICE.txt"
Write-Host "Ready: $taskOutput/DOLBUTO.exe"
