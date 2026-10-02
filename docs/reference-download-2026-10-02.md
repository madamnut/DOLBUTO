# Reference refresh — AI context, 2026-10-02

User authorized removal of all legacy ref materials and creation/execution of a latest-source downloader. Bulk deletion using verified bounded paths was rejected by tool policy; a second deletion with the eight explicit absolute paths was also rejected. Both failed before execution. Do not report the legacy folders deleted. They remain as originally supplied; no alternative deletion mechanism was used. New managed references live at ref/sources. This task does not authorize commit/push.

Sources, entry points and operation are documented in ref/README.md; manifest is ref/sources.json. Git ignore changed from /ref/ to /ref/* with four explicit files allowed (README, JSON, two BAT); the two PS1 implementations are tracked under tools/. Downloaded sources, submodules, generated Minecraft code, working directories, old materials and backups remain ignored and excluded from packaging.

Downloads use default-branch HEAD snapshots for six GitHub projects and Distant Horizons on GitLab; Lithosphere uses the most recently published stable Modrinth primary file. Submodule gitlinks are recursively resolved to their parent-pinned commit. Core URL's old namespace resolves to its current project ID. TerraForged Engine appears in .gitmodules but no gitlink exists in HEAD, so metadata records the stale entry without fetching unrelated newest Engine. Repository files/default settings are not rewritten.

Windows PowerShell 5.1 actual execution caught Invoke-RestMethod's non-enumerated array return behavior; Api now returns the assigned response to enumerate it. The resulting whole-reference refresh succeeded. The first failed work directories are retained for diagnosis as the script promises. No automated suite, CTest, input synthesis or computer use.

Successful versions in this run:
- Complementary: c09950df0650b27dac260c1b0f69afc11387aa6f (main).
- DH: 7fc66709300a84ae868331d310087d200d5c9e71 (main); Core 0cf1aadae7f7819904d494f06950dc6f8b560413.
- FreeTerraForged: a4e4af44441d3ca15c3e86645d45a6cc3e782d08 (1.21.1).
- Lithosphere: 1.8.2+mod / aatZx6pq; upstream SHA512 verified.
- MCP-Reborn: 727d72ffc66bcdf1a8c16ee92b120db2eaa46e26 (26.2).
- SimplexTerrain: 39d74e99bf060741b40bede9ba3a57ae7fced6b4 (master).
- TerraForged: 6dd607ebfd41a3b7274e090b1f2cf565d53d1d44 (0.3.x, archived).
- Terralith: 5d7b5c58ec302c20328214f92c2a033b180dfdf2 (1.20; latest default-branch source, not a claim of latest mod-site build).

Per-project .reference.json carries actual URLs, complete revisions, hashes and times. Five unchanged repositories were skipped during the second full run. A separate source-generation script finds installed runtime/toolchain JDKs and invokes upstream Gradle setup with a project-local cache; missing JDKs fail with setup instructions rather than installing software silently. Existing cache gradle.properties was backed up before replacing its obsolete sandbox JDK path.

Logs are local/ignored: .cache/ref-download*.log, .cache/ref-minecraft-setup.log. Release build/package verification and final source-generation outcome are recorded below when completed.

## Final verification

- MCP setup succeeded in 2m54s, 6 executed tasks; generated 7,055 Java files under ref/sources/MCP-Reborn/src/main/java. Upstream Gradle deprecation warning (future Gradle 9 compatibility) appeared, but setup completed successfully with the bundled 8.14.4 wrapper.
- Re-ran DistantHorizons and Lithosphere using Windows PowerShell 5.1: both latest revisions skipped correctly. Original full retry already verified the unchanged GitHub skip path.
- Eight managed reference stamps present; DH Core/core and Core/api exist. Completed reference roots retain no downloaded ZIP archives. Failed initial staging payloads remain under sources/_work as documented.
- PowerShell parser checks passed; git diff --check passed. git status exposes only the six recipe files, .gitignore, AGENTS.md and this report. Downloaded roots and generated Java files are ignored.
- Release incremental build and package succeeded; build/output DOLBUTO.exe SHA256 matched. No gameplay/rendering changes; only reference recipes and documentation were edited.
- Legacy deletion remains incomplete due to tool policy rejection. All original eight ref child directories remain, including the old MCP-Reborn generated source. The downloader deliberately has no legacy-deletion operation.

## Script layout follow-up — 2026-10-02

User approved moving PS1 implementations into tools after the layout proposal and Minecraft-setup explanation. Paths are tools/download-references.ps1 and tools/setup-minecraft-reference.ps1; ref retains README, manifest and two BAT launchers. Script-root resolution now locates project/ref rather than treating PSScriptRoot as ref. BAT uses its own directory plus ../tools, so working-directory independence is preserved. Download destination, source revisions and generated Minecraft code remain unchanged. Removed the obsolete PS1 exceptions in .gitignore. Legacy deletion remains blocked as previously recorded; it was not retried.

## Long-path extraction follow-up — 2026-10-02

The user's pasted log from C:/Users/admin/Desktop/steve/DOLBUTO showed six repository failures in Windows PowerShell 5.1's .NET ExtractToDirectory. The reference-name/GUID stage, second GUID unpack folder and repository/commit wrapper made temporary paths too long even though final installed paths were short enough. Lithosphere and MCP-Reborn had already succeeded. This machine's state differs from the earlier source-generation verification above; MCP-Reborn here contains the downloaded project only, and Minecraft source generation was not rerun.

The downloader now validates ZIP paths and symbolic-link attributes before using the built-in System32/tar.exe. Repository ZIPs must have a single top-level directory; paths are validated again after stripping it, then extracted directly into payload with --strip-components=1. Modrinth archives retain their layout. Stages use only a GUID, eliminating the second unpack directory and extra upstream wrapper. Native tar errors prevent publication. Existing backup/rollback, parent-pinned submodules, hashes and failure-artifact retention remain in place. No registry or system long-path policy was changed.

Actual execution under Windows PowerShell 5.1 completed with exit 0: all six previously failed repositories were installed, including DH Core at 0cf1aadae7f7819904d494f06950dc6f8b560413. A separate Lithosphere -Force refresh verified the release archive extraction path, upstream SHA512 and managed backup. All eight references then skipped as current during another full run (exit 0). Eight stamps and installed file trees were inspected; Core/api and Core/core are present, and Lithosphere retains pack.mcmeta and its data/overlays. The six original failed stages remain untouched. Successful stages were removed by the normal completion path. Local diagnostic log: .cache/ref-download-path-fix.log.

PowerShell parsing and git diff --check passed. Only the downloader and its documentation changed; no game build, Minecraft setup, automated test suite, commit or push was performed for this follow-up. Downloaded sources and backups remain Git-ignored.

## Project-local JDK preparation — 2026-10-02

After explicitly requiring `실시`, `ㅅㅅ` or `ㄱㄱ` before edits, the user approved the proposed optional project-local JDK downloads with `ㄱㄱ`. Error logs alone must not trigger future edits. This approval covers missing-JDK preparation in setup-minecraft-reference.ps1, verification and the normal Release build/package; it does not authorize commit/push.

Setup retains discovery of installed JDKs and explicit path parameters, validates explicit versions, and only offers downloads for missing roles. The runtime prefers 21 and retains the existing Gradle-compatible 17–24 fallback; the Minecraft toolchain major comes from build.gradle (currently 25). The default prompt lists missing roles, source and .tools destination and requires y/yes; all other answers stop without downloading or invoking Gradle. -DownloadJdks is explicit unattended consent, while -NoDownload refuses missing JDKs without a prompt. These switches affect JDK downloads, not Gradle's own dependency downloads during setup.

The Windows x64 downloader queries the official Adoptium latest-major API, restricts the returned ZIP URL to that major's Adoptium GitHub release repository, verifies SHA256, checks ZIP traversal/symlinks/root layout, and uses Windows tar with the repository wrapper stripped. A private stage is validated for java.exe, javac.exe and release major before moving into .tools/temurin-jdk-<major>. It records .reference-jdk.json and removes only its completed ZIP and empty stage. Failures retain diagnostics; existing incomplete destinations are preserved. An exclusive project-local lock prevents competing JDK installations. Compatible local JDKs are reused without querying for updates. No system installer, permanent environment changes or new Git exclusions are required.

Primary references: [Adoptium archive installation and checksum guidance](https://adoptium.net/installation/archives/) and [Adoptium API](https://api.adoptium.net/q/swagger-ui/).

Verification on this PC found an existing JDK17 at C:/Program Files/Eclipse Adoptium/jdk-17.0.20.8-hotspot, so only missing JDK25 was needed. Before installation, Windows PowerShell 5.1 -NoDownload stopped without creating JDK directories. The approved -DownloadJdks run downloaded and verified Temurin 25.0.4+101.0.LTS (SHA256 00C847D804F4A78E9F04F2683FAF14FED898535B177B7FC704486CB0284E9283) into .tools/temurin-jdk-25. JDK21 download uses the same implementation but was unnecessary and was not separately executed; the interactive y/N prompt was source-reviewed without synthesized input.

Release build and packaging succeeded; build/release/bin/DOLBUTO.exe and out/DOLBUTO/DOLBUTO.exe both hash to 05335BDD66B0A1EA73972D0CB8727A9928296B6BA27DA21EBA59080F67434E40. Logs are local/ignored: .cache/ref-jdk-setup.log, .cache/ref-jdk-reuse.log and .cache/ref-jdk-build.log.

Minecraft setup completed successfully in 3m13s (six tasks executed), producing 7,055 Java files. Upstream Gradle reported deprecated features relevant to future Gradle 9, without failing. A second -NoDownload run reused the downloaded JDK25 and installed JDK17, completed in 8s (five tasks up-to-date), kept JDK metadata unchanged and restored JAVA_HOME/PATH/GRADLE_USER_HOME in the calling process. The first attempt to invoke the script directly in the default shell was blocked by that shell's execution policy; verification was rerun through a child Windows PowerShell with -ExecutionPolicy Bypass, matching the existing BAT launcher. No persistent execution policy was changed. Parser/diff checks passed, and JDKs, caches and generated Java remain ignored. No automated suite, synthetic input, commit or push was performed.
