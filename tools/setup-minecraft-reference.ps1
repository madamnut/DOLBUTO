[CmdletBinding()]
param(
    [string]$JavaHome,
    [string]$ToolchainHome,
    [switch]$DownloadJdks,
    [switch]$NoDownload
)
$ErrorActionPreference = 'Stop'
if ($DownloadJdks -and $NoDownload) { throw 'Use either -DownloadJdks or -NoDownload, not both.' }
$projectRoot = Split-Path $PSScriptRoot -Parent

function GetReferenceJdk([string]$candidate) {
    if (-not $candidate) { return }
    $release = Join-Path $candidate 'release'
    if (-not (Test-Path -LiteralPath $release) -or
        -not (Test-Path -LiteralPath (Join-Path $candidate 'bin/java.exe')) -or
        -not (Test-Path -LiteralPath (Join-Path $candidate 'bin/javac.exe'))) { return }
    $version = [regex]::Match((Get-Content -LiteralPath $release -Raw), '(?m)^JAVA_VERSION="(\d+)')
    if ($version.Success) { return [pscustomobject]@{path=[IO.Path]::GetFullPath($candidate);major=[int]$version.Groups[1].Value} }
}

function JdkChildPath([string]$parent, [string]$relative) {
    if ([IO.Path]::IsPathRooted($relative) -or $relative.Contains(':')) { throw "Unsafe JDK path: $relative" }
    $prefix = [IO.Path]::GetFullPath($parent).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $result = [IO.Path]::GetFullPath((Join-Path $prefix $relative))
    if (-not $result.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "JDK path escapes its parent: $relative" }
    return $result
}

function JdkNoLinks([string]$path) {
    for ($current = $path; $current; $current = Split-Path $current -Parent) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked JDK path is not allowed: $current" }
        }
    }
}

function InstallReferenceJdk([int]$major) {
    if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitProcess -or
        $env:PROCESSOR_ARCHITECTURE -ne 'AMD64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') {
        throw 'Automatic JDK downloads require Windows x64 and 64-bit PowerShell. Provide local JDK paths on other platforms.'
    }
    $tar = Join-Path $env:SystemRoot 'System32/tar.exe'
    if (-not (Test-Path -LiteralPath $tar)) { throw 'Windows built-in tar.exe is required to extract JDK archives.' }
    $toolsRoot = JdkChildPath $projectRoot '.tools'
    JdkNoLinks $toolsRoot
    New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null
    $lock = $null
    $lockPath = JdkChildPath $toolsRoot '.reference-jdk.lock'
    JdkNoLinks $lockPath
    try {
        $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $destination = JdkChildPath $toolsRoot ('temurin-jdk-'+$major)
        JdkNoLinks $destination
        $existing = GetReferenceJdk $destination
        if ($existing -and $existing.major -eq $major) { return $existing.path }
        if (Test-Path -LiteralPath $destination) { throw "Incomplete or incompatible JDK directory; preserve it and provide another path: $destination" }

        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        $ProgressPreference = 'SilentlyContinue'
        $api = 'https://api.adoptium.net/v3/assets/latest/'+$major+'/hotspot?architecture=x64&image_type=jdk&os=windows&vendor=eclipse'
        $releases = Invoke-RestMethod -UseBasicParsing -Uri $api -TimeoutSec 60
        $release = @($releases) | Where-Object { $_.version.major -eq $major -and $_.binary.package.name -like '*.zip' } | Select-Object -First 1
        if (-not $release) { throw "No Windows x64 Temurin JDK $major archive was returned by Adoptium." }
        $package = $release.binary.package
        $url = [uri]$package.link
        if ($url.Scheme -ne 'https' -or $url.Host -ne 'github.com' -or
            -not $url.AbsolutePath.StartsWith('/adoptium/temurin'+$major+'-binaries/releases/download/', [StringComparison]::Ordinal) -or
            $package.checksum -notmatch '^[a-fA-F0-9]{64}$') { throw 'Unexpected Adoptium download metadata.' }
        $work = JdkChildPath $toolsRoot ('_jdk-'+[guid]::NewGuid().ToString('N'))
        JdkNoLinks $work
        New-Item -ItemType Directory -Path $work | Out-Null
        $archive = JdkChildPath $work 'jdk.zip'
        $payload = JdkChildPath $work 'payload'
        Write-Host ('Downloading Temurin JDK '+$major+' ('+[math]::Round($package.size/1MB)+' MiB): '+$package.name)
        Write-Host ('  From: '+$url.AbsoluteUri)
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $url.AbsoluteUri -OutFile $archive -TimeoutSec 1800
            $digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
            if ($digest -ine $package.checksum) { throw 'JDK archive SHA256 mismatch.' }
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            $zip = [IO.Compression.ZipFile]::OpenRead($archive)
            try {
                if ($zip.Entries.Count -eq 0) { throw 'Empty JDK archive.' }
                $root = $null
                foreach ($entry in $zip.Entries) {
                    $name = $entry.FullName.Replace('\','/')
                    $null = JdkChildPath $payload $name
                    if ((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw 'Symbolic link in JDK archive.' }
                    $slash = $name.IndexOf('/')
                    if ($slash -le 0) { throw 'Unexpected JDK archive layout.' }
                    $prefix = $name.Substring(0,$slash)
                    if ($null -eq $root) { $root = $prefix }
                    if ($prefix -cne $root) { throw 'Multiple JDK archive roots.' }
                    $relative = $name.Substring($slash+1)
                    if ($relative) { $null = JdkChildPath $payload $relative }
                }
            } finally { $zip.Dispose() }
            New-Item -ItemType Directory -Path $payload | Out-Null
            & $tar -xf $archive -C $payload --strip-components=1 --no-same-owner --no-same-permissions
            if ($LASTEXITCODE -ne 0) { throw "JDK extraction failed (tar exit $LASTEXITCODE)." }
            $jdk = GetReferenceJdk $payload
            if (-not $jdk -or $jdk.major -ne $major) { throw 'Extracted JDK version or executables are invalid.' }
            $metadata = [ordered]@{version=$release.version.semver;major=$major;url=$url.AbsoluteUri;sha256=$digest;downloaded_utc=[datetime]::UtcNow.ToString('o')}
            [IO.File]::WriteAllText((Join-Path $payload '.reference-jdk.json'), ($metadata | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))
            JdkNoLinks $payload
            JdkNoLinks $destination
            Move-Item -LiteralPath $payload -Destination $destination
            Remove-Item -LiteralPath $archive
            Remove-Item -LiteralPath $work
            Write-Host ('JDK ready: '+$destination)
            return $destination
        } catch {
            Write-Warning ('JDK preparation failed; remaining download artifacts are retained at '+$work)
            throw
        }
    } finally { if ($lock) { $lock.Dispose() } }
}

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
    $jdk = GetReferenceJdk $candidate
    if ($jdk) { $jdks += $jdk }
}
if ($JavaHome) {
    $runtime = GetReferenceJdk $JavaHome
    if (-not $runtime -or $runtime.major -lt 17 -or $runtime.major -gt 24) { throw '-JavaHome must point to a JDK compatible with the current Gradle wrapper (17-24; 21 recommended).' }
    $JavaHome = $runtime.path
}
if ($ToolchainHome) {
    $toolchain = GetReferenceJdk $ToolchainHome
    if (-not $toolchain -or $toolchain.major -ne $requiredMajor) { throw "-ToolchainHome must point to JDK $requiredMajor." }
    $ToolchainHome = $toolchain.path
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
    Write-Host 'Required JDKs:'
    if ($JavaHome) { Write-Host ('  Gradle: '+$JavaHome) } else { Write-Host '  MISSING: JDK 21 for Gradle' }
    if ($ToolchainHome) { Write-Host ('  Minecraft: '+$ToolchainHome) } else { Write-Host ('  MISSING: JDK '+$requiredMajor+' for Minecraft') }
    Write-Host ('Download missing Temurin JDKs from Adoptium into '+(Join-Path $projectRoot '.tools'))
    Write-Host 'Project-local ZIP extraction only; no system installation or permanent JAVA_HOME/PATH changes. JDKs are Git-ignored.'
    if ($NoDownload) { throw 'JDKs are missing and -NoDownload is set. Provide -JavaHome/-ToolchainHome or rerun without -NoDownload.' }
    if (-not $DownloadJdks) {
        $answer = Read-Host 'Download missing JDKs and continue Minecraft source setup? [y/N]'
        if ($answer -notmatch '^(?i:y|yes)$') { throw 'Cancelled; no JDKs downloaded. You can provide -JavaHome and -ToolchainHome instead.' }
    }
    if (-not $JavaHome) { $JavaHome = InstallReferenceJdk 21 }
    if (-not $ToolchainHome) {
        if ($requiredMajor -eq (GetReferenceJdk $JavaHome).major) { $ToolchainHome = $JavaHome }
        else { $ToolchainHome = InstallReferenceJdk $requiredMajor }
    }
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
