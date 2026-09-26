# Editor workspace persistence — 2026-09-27

User approved native settings save/load, startup restoration, dirty indication, retained JSON interchange and one saved master seed with `실시`. The game remains flat stone, and this does not publish experimental terrain to the game.

## File and API

`worldgen.editor.json` beside worldgen_editor.exe holds schema_version1, config (normalized GenerationConfig schema12 with one master seed), and voronoi (null or experiment version3 without parameters.seed). GET responses/JSON export reinsert the common config.seed into the experiment for standalone interchange. Saving either section changes only its values and the shared seed, retaining the other section's saved values. Unsaved changes in another tab are not silently saved.

GET `/api/workspace` validates and returns config, voronoi, climate_revision, voronoi_revision, base_seed, exists and path. Missing file falls back to legacy worldgen.draft.json, then worldgen.json, then assets/worldgen/default.json for climate; experiment is null. Reads never create or rewrite files. Malformed/unsupported workspaces return errors, not replacement defaults. Legacy draft, game worldgen and settings are preserved.

POST `/api/workspace/climate` takes config/revision/base_seed. POST `/api/workspace/voronoi` takes voronoi/revision/base_seed. Requests use existing loopback/token/origin authentication. Input ranges/types are validated without generating a map. Experiments include validated parameters, viewport, resolution256/512/1024 and optional display (view/edges/sites/auto, defaults for old exports). Game noise formulas and experiment algorithms are unchanged.

Revisions fingerprint each normalized section excluding the shared seed. Writes reject409 if that section changed since loading, or the saved seed changed to a value different from both the loaded seed and requested seed. Thus independent section edits merge, including the same synchronized seed, while stale overlapping writes are rejected. Existing folder mutex and serial server prevent competing editor instances. External editors are not locked against simultaneous writes.

Write a complete .tmp file, close/check stream, then MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH). Final workspace stays intact on validation/conflict/write failure. The two filenames are git-ignored and packaging does not overwrite either. Existing legacy /api/draft routes remain for compatibility; current UI uses workspace endpoints. Game publish still only updates worldgen.json and its backup, independent of explicit workspace save.

## UI

Voronoi header offers 설정 저장 and 마지막 저장 불러오기; persistent status below the general status shows clean/dirty/not-yet-saved and actual path. Snapshot captured at save start is marked saved only after success, preserving dirty status for edits made while waiting. Inputs, viewport, resolution and display toggles participate in dirty detection. Explicit load asks before replacing edits, applies saved seed to shared live tabs and refreshes the map. Startup loads saved values then adopts any newer live session seed; that seed difference remains dirty. Imports stay working changes until saved. Exports still produce independently importable experiment JSON3. Tab navigation/close uses beforeunload for unsaved changes.

Climate startup restores workspace config; explicit 확정 불러오기 retains its original meaning. 작업 저장 saves current climate and common seed, 작업 불러오기 restores workspace values. Both browser pages retain master-seed live synchronization. Reopening another editor session uses the persisted seed rather than the old session-token browser record. No game process synchronization or auto publication.

## Verification

Release compilation and JS syntax checks passed. Manual native HTTP diagnostics in isolated build/release/editor-save-review, not an automated suite:

- First GET reported missing workspace and created no file.
- Saved custom seed/subdivisions6/negative viewport/resolution256/regions view/disabled display toggles and round-tripped exactly. File contains one seed key.
- Climate save with a changed gain and same shared seed preserved the experiment. Subsequent experiment save preserved normalized climate exactly.
- Stale experiment revision returned409; invalid subdivisions returned400. Both left saved bytes intact.
- Stopped editor and started a fresh process/token; GET workspace matched the saved config/experiment/revisions exactly.
- Deliberately malformed isolated file returned400 and remained untouched; restored diagnostic copy afterward.
- Game published settings remained byte-identical during both save paths and restart. Isolated process exited0 via authenticated shutdown.

Browser clicks, dialog appearance and actual multi-tab gestures were not exercised; no computer use, synthetic input or CTest. Release packaging completed: executables/editor assets match build outputs and packaged README matches source. Installed settings/worldgen hashes unchanged; absent legacy draft/editor workspace remained absent. No commit/push performed.
