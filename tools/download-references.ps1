[CmdletBinding()]
param([string[]]$Only, [switch]$Check, [switch]$Force)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.IO.Compression.FileSystem
$referenceRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) 'ref'))
$storage = Join-Path $referenceRoot 'sources'
$headers = @{ 'User-Agent' = 'DOLBUTO-reference-fetcher/1.0'; 'Accept' = 'application/json' }
function ChildPath([string]$parent, [string]$relative) {
    $base = [IO.Path]::GetFullPath($parent).TrimEnd('\','/')
    if ([IO.Path]::IsPathRooted($relative) -or $relative.Contains(':')) { throw "Invalid relative path: $relative" }
    $path = [IO.Path]::GetFullPath((Join-Path $base $relative))
    if (-not $path.StartsWith($base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes destination: $relative" }
    return $path
}
function NoLinks([string]$path) {
    $cursor = [IO.Path]::GetFullPath($path)
    while ($cursor) {
        $item = Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
        if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Linked path is not supported: $cursor" }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}
function Api([string]$url) {
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        try { $response = Invoke-RestMethod -Uri $url -Headers $headers -UseBasicParsing -TimeoutSec 90; return $response }
        catch { if ($attempt -eq 2) { throw }; Start-Sleep -Seconds (2 + 2 * $attempt) }
    }
}
function Download([string]$url, [string]$file) {
    if (-not $url.StartsWith('https://')) { throw 'Only HTTPS downloads are supported.' }
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        try { Invoke-WebRequest -Uri $url -Headers @{'User-Agent'=$headers['User-Agent']} -UseBasicParsing -OutFile $file -TimeoutSec 600; return }
        catch { if ($attempt -eq 2) { throw }; Start-Sleep -Seconds (2 + 2 * $attempt) }
    }
}
function RepoInfo([string]$kind, [string]$repo, [string]$revision = '') {
    if ($kind -eq 'github') {
        $base = 'https://api.github.com/repos/' + $repo
        if (-not $revision) { $meta = Api $base; $branch = $meta.default_branch; $revision = $branch } else { $branch = $null }
        $commit = Api ($base + '/commits/' + [uri]::EscapeDataString($revision))
        return [pscustomobject]@{kind=$kind;repository=$repo;branch=$branch;revision=$commit.sha;url=('https://codeload.github.com/'+$repo+'/zip/'+$commit.sha);api=$base}
    }
    if ($kind -eq 'gitlab') {
        $base = 'https://gitlab.com/api/v4/projects/' + [uri]::EscapeDataString($repo)
        $meta = Api $base
        if (-not $revision) { $branch = $meta.default_branch; $revision = $branch } else { $branch = $null }
        $base = 'https://gitlab.com/api/v4/projects/' + $meta.id
        $commit = Api ($base + '/repository/commits/' + [uri]::EscapeDataString($revision))
        return [pscustomobject]@{kind=$kind;repository=$meta.path_with_namespace;branch=$branch;revision=$commit.id;url=($base+'/repository/archive.zip?sha='+$commit.id);api=$base}
    }
    throw "Unsupported repository provider: $kind"
}
function ExpandSafe([string]$archive, [string]$destination) {
    NoLinks $destination
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        if ($zip.Entries.Count -eq 0) { throw 'Empty archive.' }
        foreach ($entry in $zip.Entries) {
            $null = ChildPath $destination $entry.FullName
            if ((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw "Archive contains a symbolic link: $($entry.FullName)" }
        }
    } finally { $zip.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($archive, $destination)
}
function Modules([string]$root) {
    $file = Join-Path $root '.gitmodules'
    if (-not (Test-Path -LiteralPath $file)) { return }
    $text = [IO.File]::ReadAllText($file)
    foreach ($section in [regex]::Matches($text, '(?ms)^\s*\[submodule\s+"[^"]+"\]\s*\r?\n(.*?)(?=^\s*\[|\z)')) {
        $body = $section.Groups[1].Value
        $path = [regex]::Match($body, '(?m)^\s*path\s*=\s*(.+?)\s*$').Groups[1].Value.Trim('"')
        $url = [regex]::Match($body, '(?m)^\s*url\s*=\s*(.+?)\s*$').Groups[1].Value.Trim('"')
        if (-not $path -or -not $url) { throw 'Incomplete submodule declaration.' }
        $null = ChildPath $root $path
        $match = [regex]::Match($url, '^https://(github\.com|gitlab\.com)/(.+?)(?:\.git)?/?$')
        if (-not $match.Success) { throw "Unsupported submodule URL: $url" }
        $kind = 'github'; if ($match.Groups[1].Value -eq 'gitlab.com') { $kind = 'gitlab' }
        [pscustomobject]@{path=$path;kind=$kind;repository=$match.Groups[2].Value}
    }
}
function FetchRepo($info, [string]$destination, [string]$work, [int]$depth = 0) {
    if ($depth -gt 8) { throw 'Submodule nesting exceeds 8 levels.' }
    $id = [guid]::NewGuid().ToString('N')
    $archive = ChildPath $work ($id+'.zip')
    $unpacked = ChildPath $work ($id+'-unpack')
    Write-Host ('  Fetch '+$info.repository+' @ '+$info.revision.Substring(0,12))
    Download $info.url $archive
    $digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    ExpandSafe $archive $unpacked
    $entries = @(Get-ChildItem -LiteralPath $unpacked -Force)
    if ($entries.Count -ne 1 -or -not $entries[0].PSIsContainer) { throw 'Unexpected repository archive layout.' }
    if (Test-Path -LiteralPath $destination) {
        NoLinks $destination
        if (@(Get-ChildItem -LiteralPath $destination -Force).Count) { throw "Submodule target is not empty: $destination" }
        Remove-Item -LiteralPath $destination -ErrorAction Stop
    }
    $parent = Split-Path $destination -Parent
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    NoLinks $parent
    Move-Item -LiteralPath $entries[0].FullName -Destination $destination
    Remove-Item -LiteralPath $unpacked -ErrorAction Stop
    # Only remove the downloaded archive after successful extraction and placement.
    Remove-Item -LiteralPath $archive -ErrorAction Stop
    $children = @()
    $modules = @(Modules $destination)
    if ($modules.Count -and $info.kind -eq 'github') {
        $tree = Api ($info.api+'/git/trees/'+$info.revision+'?recursive=1')
        if ($tree.truncated) { throw 'GitHub returned a truncated submodule tree.' }
    }
    foreach ($module in $modules) {
        if ($info.kind -eq 'github') {
            $link = @($tree.tree | Where-Object { $_.path -eq $module.path -and $_.mode -eq '160000' })
            if ($link.Count -eq 0) {
                Write-Host ('  Ignoring stale .gitmodules entry with no gitlink: '+$module.path)
                $children += [pscustomobject]@{path=$module.path;status='stale declaration; no gitlink in parent commit'}
                continue
            }
            if ($link.Count -ne 1) { throw "Ambiguous gitlink: $($module.path)" }
            $sha = $link[0].sha
        } else {
            $slash = $module.path.LastIndexOf('/')
            $dir = ''; if ($slash -ge 0) { $dir = $module.path.Substring(0,$slash) }
            $link = @(); $page = 1
            do {
                $items = @(Api ($info.api+'/repository/tree?ref='+$info.revision+'&path='+[uri]::EscapeDataString($dir)+'&per_page=100&page='+$page))
                $link += @($items | Where-Object { $_.path -eq $module.path -and $_.type -eq 'commit' })
                $page++
            } while ($items.Count -eq 100 -and $link.Count -eq 0)
            if ($link.Count -ne 1) { throw "Missing gitlink: $($module.path)" }
            $sha = $link[0].id
        }
        $subInfo = RepoInfo $module.kind $module.repository $sha
        $sub = FetchRepo $subInfo (ChildPath $destination $module.path) $work ($depth+1)
        $children += [pscustomobject]@{path=$module.path;source=$sub}
    }
    return [pscustomobject]@{kind=$info.kind;repository=$info.repository;branch=$info.branch;revision=$info.revision;archive_url=$info.url;archive_sha256=$digest;submodules=$children}
}
NoLinks $referenceRoot
$manifest = Get-Content -LiteralPath (Join-Path $referenceRoot 'sources.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schema_version -ne 1) { throw 'Unknown sources manifest schema.' }
$selected = @($manifest.references)
if ($Only) {
    foreach ($name in $Only) { if ($name -notin $selected.name) { throw "Unknown reference: $name" } }
    $selected = @($selected | Where-Object {$_.name -in $Only})
}
$failed = @()
$lock = $null
try {
    if (-not $Check) {
        NoLinks $storage
        New-Item -ItemType Directory -Force -Path $storage | Out-Null
        NoLinks $storage
        $lock = [IO.File]::Open((Join-Path $storage '.download.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    }
    foreach ($reference in $selected) {
        try {
            Write-Host ('['+$reference.name+'] Checking latest...')
            if ($reference.kind -eq 'modrinth') {
                $versions = @(Api ('https://api.modrinth.com/v2/project/'+$reference.project+'/version'))
                $version = $versions | Where-Object {$_.version_type -eq 'release'} | Sort-Object {[datetime]$_.date_published} -Descending | Select-Object -First 1
                if (-not $version) { throw 'No stable Modrinth release found.' }
                $file = $version.files | Where-Object primary | Select-Object -First 1
                if (-not $file) { $file = $version.files[0] }
                $revision = $version.id
                $description = $version.version_number+' ('+$revision+')'
            } else {
                $info = RepoInfo $reference.kind $reference.repository
                $revision = $info.revision
                $description = $info.branch+' @ '+$revision
            }
            Write-Host ('  Latest: '+$description)
            if ($Check) { continue }
            $destination = ChildPath $storage $reference.name
            NoLinks $destination
            $stamp = Join-Path $destination '.reference.json'
            if (-not $Force -and (Test-Path -LiteralPath $stamp)) {
                $old = Get-Content -LiteralPath $stamp -Raw -Encoding UTF8 | ConvertFrom-Json
                if ($old.source.revision -eq $revision) { Write-Host '  Already current; skipped.'; continue }
            }
            $work = ChildPath $storage ('_work/'+$reference.name+'-'+[guid]::NewGuid().ToString('N'))
            NoLinks $work
            New-Item -ItemType Directory -Force -Path $work | Out-Null
            NoLinks $work
            $payload = ChildPath $work 'payload'
            if ($reference.kind -eq 'modrinth') {
                $archive = ChildPath $work 'release.zip'
                Download $file.url $archive
                if (-not $file.hashes.sha512) { throw 'Modrinth did not provide SHA512.' }
                if ((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ine $file.hashes.sha512) { throw 'Modrinth SHA512 mismatch.' }
                ExpandSafe $archive $payload
                Remove-Item -LiteralPath $archive -ErrorAction Stop
                $source = [pscustomobject]@{kind='modrinth';project=$reference.project;revision=$version.id;version=$version.version_number;published=$version.date_published;archive_url=$file.url;archive_sha512=$file.hashes.sha512}
            } else { $source = FetchRepo $info $payload $work }
            [pscustomobject]@{schema_version=1;name=$reference.name;downloaded_utc=[datetime]::UtcNow.ToString('o');source=$source} | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $payload '.reference.json') -Encoding UTF8
            # A complete new tree is prepared before moving an existing managed download.
            $backup = $null
            if (Test-Path -LiteralPath $destination) {
                $backup = ChildPath $storage ('_backups/'+$reference.name+'-'+[datetime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
                New-Item -ItemType Directory -Force -Path (Split-Path $backup -Parent) | Out-Null
                NoLinks (Split-Path $backup -Parent)
                Move-Item -LiteralPath $destination -Destination $backup
            }
            try { Move-Item -LiteralPath $payload -Destination $destination }
            catch { if ($backup -and -not (Test-Path -LiteralPath $destination)) { Move-Item -LiteralPath $backup -Destination $destination }; throw }
            Remove-Item -LiteralPath $work -ErrorAction Stop
            Write-Host ('  Ready: '+$destination)
        } catch {
            $failed += $reference.name
            Write-Warning ($reference.name+': '+$_.Exception.Message)
            Write-Host $_.ScriptStackTrace
            Write-Host '  Failed staging files are retained under sources/_work; existing installed data is preserved.'
        }
    }
} finally { if ($lock) { $lock.Dispose() } }
if ($failed.Count) { Write-Error ('Failed references: '+($failed -join ', ')); exit 1 }
Write-Host 'All requested references are ready. Minecraft source generation is available via setup-minecraft.bat.'
