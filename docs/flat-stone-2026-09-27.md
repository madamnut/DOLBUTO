# Temporary flat stone terrain — 2026-09-27

## Scope
User approved removal of the existing terrain generator and a flat stone placeholder until its replacement is designed. No Voronoi or other new terrain design is implemented.

flat_surface_y=192: twelve uniform rock chunks per column (Y0..191), twenty uniform air chunks (Y192..511), no generated fluids. generate_tile records highest solid cell191, generate_chunk checks world height and optional prepared metadata, then chooses uniform rock/air without noise or per-cell allocations. Existing random temperate-band spawn starts walking with feet192. Chunk independence, torus131072, column publication, editable blocks/fluids, lighting and LOD use the existing downstream paths.

## Removed
Terrain Groundness/Smoothness/Weirdness/PV/Jagged fields; terrain Shift use; blended shape octaves, density slides/interpolation and profile surface search; spline trees/preset serialization; old terrain biome classification; corresponding F3 diagnostics, F8 controls, web controls/API and Minecraft reference comparison. Deleted old transition-only torus benchmark; world pipeline benchmark now samples origin/interior/seam because relief is identically zero. Generic noise and lighting measurement tools remain opt-in and were not run.

## Preserved
Temperature and precipitation retain their periodic DoublePerlin, shared3D Shift slices and latitude formula. They are climate-only and do not influence stone/air generation. F3 retains raw values and nine climate bins; F8 retains regeneration/save/load and independent async climate preview. Web retains the authenticated localhost server, validated drafts/publication/revision conflicts/backup/atomic save, climate fields/maps and JSON/PNG export. No automatic publication.

Schema11 records terrain=flat_stone and climate settings only. Existing schema10 settings load their exact seed/climate/Shift in memory while ignoring old terrain fields; the disk file is untouched. Schema5..9 keep their existing migration policy: seed/latitude preserved, climate/Shift defaults. Explicit save writes schema11; web publication backs up previous bytes. Packaging removes only four obsolete editor static assets, never user settings/backups.

## Verification
- Release game and standalone editor built successfully; packaged to out/DOLBUTO. JavaScript syntax check and git diff whitespace check passed. Retired preset import script/data and editor static files removed.
- Isolated game CLI with legacy schema10: walk spawn feet Y192 confirmed in flat.png; 29 published columns and no pending columns. Vulkan errors0/UI issues0/texture failures0. Fourteen existing unused shader output warnings remain; graphics shader code was not changed.
- Isolated F4 LOD CLI with schema11: 1,800 frames, 29 real columns, 222 selected LOD tiles, 748 distant columns generated, pending0, Vulkan errors0. This confirms the shared flat generation path reaches LOD.
- Isolated localhost editor HTTP: schema10 climate/seed/Shift values retained exactly, load/preview did not change legacy file bytes; both64x64 climate maps finite; X/Z seam samples equal; draft/publish/reload schema11 succeeded, backup matched previous bytes, stale revision rejected409 and retired groundness map rejected400. Editor exited normally.
- Original installed settings/worldgen SHA256 unchanged: settings 1597232D238F770788C3E86C69B4C331B3F0310CC30ED52DFD6E4A7558253D62; worldgen E6991E0A058F663EE92C22BC8CAFDC161F62605792BC649E28976E6C935FCDB8.
- No automated tests, CTest, computer use or synthetic input. Web/F8 pointer interactions were not exercised; verification was source review, compilation and isolated CLI/HTTP. Captures/logs are under build/release/flat-review (ignored).
