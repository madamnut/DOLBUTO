[CmdletBinding()]
param([string]$JavaHome, [string]$ToolchainHome)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$sourceRoot = Join-Path $projectRoot 'ref/sources/MCP-Reborn'
if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot 'gradlew.bat'))) { throw 'Run download.bat first.' }
$buildText = Get-Content -LiteralPath (Join-Path $sourceRoot 'build.gradle') -Raw
$match = [regex]::Match($buildText, 'JavaLanguageVersion\.of\((\d+)\)')
if (-not $match.Success) { throw 'Cannot determine upstream JDK requirement; inspect build.gradle.' }
$requiredMajor = [int]$match.Groups[1].Value
$homes = @($JavaHome, $ToolchainHome, $env:JAVA_HOME)
$command = Get-Command java.exe -ErrorAction SilentlyContinue
if ($command) { $homes += Split-Path (Split-Path $command.Source -Parent) -Parent }
foreach ($parent in @((Join-Path $projectRoot '.tools'), (Join-Path $env:ProgramFiles 'Eclipse Adoptium'), (Join-Path $env:ProgramFiles 'Java'), (Join-Path $env:ProgramFiles 'Microsoft'))) {
    if (Test-Path -LiteralPath $parent) { $homes += @(Get-ChildItem -LiteralPath $parent -Directory | Select-Object -ExpandProperty FullName) }
}
$jdks = @()
foreach ($candidate in @($homes | Where-Object {$_} | Select-Object -Unique)) {
    $release = Join-Path $candidate 'release'
    if (-not (Test-Path -LiteralPath $release) -or -not (Test-Path -LiteralPath (Join-Path $candidate 'bin/javac.exe'))) { continue }
    $text = Get-Content -LiteralPath $release -Raw
    $version = [regex]::Match($text, '(?m)^JAVA_VERSION="(\d+)')
    if ($version.Success) { $jdks += [pscustomobject]@{path=$candidate;major=[int]$version.Groups[1].Value} }
}
if (-not $JavaHome) {
    # Current upstream wrapper is Gradle 8.14: run it on JDK21, while its toolchain may be JDK25.
    $runtime = $jdks | Where-Object major -eq 21 | Select-Object -First 1
    if (-not $runtime) { $runtime = $jdks | Where-Object {$_.major -ge 17 -and $_.major -le 24} | Sort-Object major -Descending | Select-Object -First 1 }
    if ($runtime) { $JavaHome = $runtime.path }
}
if (-not $ToolchainHome) {
    $toolchain = $jdks | Where-Object major -eq $requiredMajor | Select-Object -First 1
    if ($toolchain) { $ToolchainHome = $toolchain.path }
}
if (-not $JavaHome -or -not $ToolchainHome) {
    throw "JDK21 for Gradle and JDK$requiredMajor for Minecraft are needed. Install/extract from https://adoptium.net/temurin/releases/ then retry, or pass -JavaHome and -ToolchainHome. No system installation is performed by this script."
}
if (-not (Test-Path -LiteralPath (Join-Path $JavaHome 'bin/java.exe')) -or -not (Test-Path -LiteralPath (Join-Path $ToolchainHome 'bin/javac.exe'))) { throw 'Invalid JDK path.' }
$cache = Join-Path $projectRoot '.cache/mcp-gradle'
New-Item -ItemType Directory -Force -Path $cache | Out-Null
$properties = Join-Path $cache 'gradle.properties'
$previous = ''; if (Test-Path -LiteralPath $properties) { $previous = Get-Content -LiteralPath $properties -Raw }
$value = 'org.gradle.java.installations.paths=' + $ToolchainHome.Replace('\','/')
$updated = [regex]::Replace($previous, '(?m)^org\.gradle\.java\.installations\.paths=.*\r?\n?', '')
$updated = $updated.TrimEnd() + [Environment]::NewLine + $value + [Environment]::NewLine
if ($previous -ne $updated) {
    if (Test-Path -LiteralPath $properties) { Copy-Item -LiteralPath $properties -Destination ($properties+'.before-ref-setup-'+[datetime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'.bak') }
    [IO.File]::WriteAllText($properties,$updated,(New-Object Text.UTF8Encoding($false)))
}
$savedJava = $env:JAVA_HOME; $savedPath = $env:PATH; $savedGradle = $env:GRADLE_USER_HOME
try {
    $env:JAVA_HOME = $JavaHome
    $env:PATH = (Join-Path $JavaHome 'bin') + ';' + $env:PATH
    $env:GRADLE_USER_HOME = $cache
    Write-Host ('Gradle JDK: '+$JavaHome)
    Write-Host ('Minecraft toolchain: '+$ToolchainHome)
    Push-Location $sourceRoot
    try {
        & ./gradlew.bat --no-daemon --console=plain setup
        if ($LASTEXITCODE -ne 0) { throw 'Gradle setup failed; archive download alone does not create Minecraft source.' }
        $files = @(Get-ChildItem -LiteralPath 'src/main/java' -Recurse -Filter '*.java')
        if ($files.Count -eq 0) { throw 'Gradle returned success but no Minecraft Java sources were found.' }
        Write-Host ('Ready: '+$files.Count+' Java sources in '+(Join-Path $sourceRoot 'src/main/java'))
    } finally { Pop-Location }
} finally { $env:JAVA_HOME=$savedJava; $env:PATH=$savedPath; $env:GRADLE_USER_HOME=$savedGradle }
