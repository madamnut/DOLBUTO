# Reference sources

This directory contains reference-source addresses and Windows download recipes for AI research. Downloaded code is local-only and is not part of DOLBUTO's build or package.

## Entry points

- `download.bat` → `../tools/download-references.ps1`: fetch the latest default-branch snapshot from GitHub/GitLab, or the latest stable Modrinth release. Uses built-in Windows PowerShell 5.1 and Windows `System32/tar.exe`; PowerShell 7 and Git are not required.
- `setup-minecraft.bat` → `../tools/setup-minecraft-reference.ps1`: after downloading, run MCP-Reborn's Gradle `setup` to generate Minecraft Java source. Finds compatible JDKs or offers to download missing ones into `.tools`; it does not install them system-wide or launch Minecraft.
- `sources.json`: source-of-truth provider/repository list. New machines need this file, both BAT files here, and the two implementation scripts in `tools/`.

| Local directory under `sources/` | Upstream | Selection |
| --- | --- | --- |
| ComplementaryUnbound | https://github.com/ComplementaryDevelopment/ComplementaryReimagined | Default branch; shared Reimagined/Unbound source, upstream defaults preserved |
| DistantHorizons | https://gitlab.com/distant-horizons-team/distant-horizons | Default branch and its pinned Core submodule |
| FreeTerraForged | https://github.com/ETcodehome/FreeTerraForged | Default branch |
| Lithosphere | https://modrinth.com/datapack/lithosphere | Latest `release` by publication date; primary ZIP/JAR, project ID `iv9jp2k9` |
| MCP-Reborn | https://github.com/Hexeption/MCP-Reborn | Default branch; generated source requires separate setup |
| SimplexTerrain | https://github.com/jaskarth/simplexterrain | Default branch |
| TerraForged | https://github.com/TerraForged/TerraForged | Default branch of the archived project, not its old 1.16.5 release |
| Terralith | https://github.com/Stardust-Labs-MC/Terralith | Default branch, which may differ from the latest mod-site release |

Distant Horizons Core is https://gitlab.com/distant-horizons-team/distant-horizons-core and is placed at `sources/DistantHorizons/coreSubProjects/`. Its old `jeseibel/` URL is resolved through GitLab project metadata. Submodules use the commit pinned by the parent, not an independently chosen latest commit. A `.gitmodules` declaration with no actual gitlink in that parent commit is recorded as stale and skipped (currently TerraForged/Engine).

## Refresh behavior

The downloader resolves the latest revision each time. Matching installed `.reference.json` revisions are skipped. Otherwise it downloads and extracts in `sources/_work`, fetches required nested repositories, and only then replaces the managed directory. Existing managed data is moved to `sources/_backups` first; an unsuccessful placement attempts to restore it. Initial legacy folders immediately under `ref/` are never touched by the downloader.

ZIP entries are checked for escaping paths and symbolic links before extraction with Windows tar. Repository archives have their single enclosing directory stripped directly into the payload; release archives keep their original layout. Short GUID staging directories avoid unnecessary path nesting, and native extraction avoids Windows PowerShell 5.1's `ExtractToDirectory` long-path failure. No machine-wide long-path setting is changed.

Each successful archive is deleted after extraction and placement. A failed download/extraction keeps its staging artifacts for diagnosis. Modrinth files are checked against the upstream SHA512; repository snapshots are fixed to resolved commit IDs and their downloaded SHA256 is recorded. `.reference.json` also records source URL, branch/version, commit and UTC retrieval time, including submodules. No Git history is downloaded.

`download.bat -Check` resolves latest versions without writing data. `download.bat -Only DistantHorizons` limits the operation to one reference. `download.bat -Force` redownloads and backs up matching revisions too. For multiple names use PowerShell directly: `../tools/download-references.ps1 -Only DistantHorizons,Lithosphere`. Failures are reported per reference, other references continue, and the final exit code is nonzero if any failed. Concurrent writers are rejected by an exclusive file lock. Public API/network limits can cause temporary failures; retry later.

## Minecraft source generation

Current MCP-Reborn uses Gradle 8.14.4 and a Java 25 toolchain. JDK21 is preferred for Gradle; an existing compatible JDK17–24 can also be reused. The Minecraft toolchain major is read from upstream `build.gradle`. The setup script discovers local `.tools` JDKs, `JAVA_HOME`, PATH and common Windows installation directories; explicit `-JavaHome` and `-ToolchainHome` parameters are also available and checked for compatible versions. Invalid explicit paths fail rather than being silently replaced.

If a JDK is missing, setup lists the requirements and destination, then asks `Download missing JDKs and continue Minecraft source setup? [y/N]`. Only `y` or `yes` proceeds; otherwise it stops before downloading or running Gradle. On Windows x64 it queries the official Adoptium API for the latest Temurin HotSpot ZIP of each missing major, verifies the supplied SHA256 and archive paths, and extracts with Windows tar into `.tools/temurin-jdk-<major>`. A missing Gradle runtime downloads JDK21. Existing compatible JDKs are reused without an update query. Successful downloads record URL/version/hash/time in `.reference-jdk.json` and delete their ZIP/stage; failed stages remain for diagnosis. Existing incomplete destination folders are preserved and reported. An exclusive lock prevents concurrent JDK installation. No administrator access, system installation, registry changes or permanent JAVA_HOME/PATH changes are needed. JDKs remain Git-ignored.

`setup-minecraft.bat -NoDownload` refuses missing JDKs without prompting or downloading them. `setup-minecraft.bat -DownloadJdks` explicitly consents to the missing-JDK downloads for unattended use; it cannot be combined with `-NoDownload`. These flags govern JDK preparation only: once JDKs are ready, Gradle setup can still download its distribution, dependencies and Minecraft inputs. Setup uses project-local `.cache/mcp-gradle`, updates only that cache's Java toolchain path (backing up existing properties), and restores process environment variables afterwards. Source output is `sources/MCP-Reborn/src/main/java`.

The upstream MCP-Reborn license/README govern generated Minecraft code. Generated code and downloaded upstream sources stay Git-ignored. Only this README, sources.json and the two BAT launchers are tracked under ref/. The two PS1 implementations are tracked normally under tools/. Launchers and scripts resolve paths from their own locations, independently of the current working directory.
