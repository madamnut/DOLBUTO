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
