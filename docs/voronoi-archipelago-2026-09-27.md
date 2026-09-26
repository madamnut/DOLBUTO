# Archipelago cells and one master seed — 2026-09-27

User requested a single seed for all elements, then approved smaller cells only in archipelago regions with `한번 해보자 실시`. This extends the standalone editor experiment. The game remains flat stone at Y192; no natural water, heights, rivers, coastal slopes or biome rules were added.

## Native hierarchy

`src/editor/voronoi_preview.cpp` is linked only into worldgen_editor. Large sites use periodic stratified jittered Voronoi (4..256 strata per axis), exact nearest-site search in5x5 strata. Land noise is sampled at each unwarped large site; value>land_threshold gives continent. Remaining sites use separate archipelago noise; value>arch_threshold gives archipelago, otherwise outer ocean. The three distribution noises use periodic Double Perlin,3 octaves,gain.5 with explicit halving spacings.

Only pixels belonging to archipelago regions query a global fine grid with coarseCount*subdivisions strata per axis. It is not a separate fine grid per parent: adjacent archipelago regions share fine IDs, site coordinates and classification. The fine grid is clipped to the union of archipelago regions. Both hierarchies use the same jitter amount and the same warped query coordinate. Fine global IDs are exact Float32 integers (maximum4194303); sampled fine sites are cached by ID within each preview request, then noise is sampled in a batch.

Fine site's center outside the archipelago union is water. Inside it, compute distance to nearest coarse site and nearest non-archipelago site; normalize their gap by2*fadeWidth, clamp0..1, apply smoothstep. The exterior search covers7x7 strata; beyond this neighborhood the allowed fade<=.5 coarse spacing is already saturated. This is a continuous distance-gap proxy, not exact metric distance to a polygon edge. Neighboring archipelago sites are never exterior candidates. Raise the island threshold by2*(1-weight); island iff weight>0 and raw island noise exceeds raised threshold. Fade0 disables the inward taper but still requires the fine center inside the union. A fine polygon can cross the union boundary and be clipped there; fade reduces this artifact but does not guarantee all islands avoid the boundary.

Defaults: coarse spacing4096, jitter1; continent spacing32768/threshold0; archipelago enabled, spacing16384/threshold.15; subdivisions4 (fine spacing1024); island spacing2048/threshold0; fade.18 coarse spacing. Subdivisions2..8, archipelago spacing512..131072, island spacing64..131072, thresholds−1..1, fade0...5. Raw noise is not clamped; thresholds are not target area fractions. Boundary warp remains initially OFF, strength1024, spacing_log2=11,3 octaves,gain.5.

## Single master seed

`src/core/random_seed.hpp` has a fixed uint32 mixing function and purpose domains for climate Shift, temperature, precipitation, coarse sites, fine sites, continent, archipelago, island and boundary X/Z. Derived seeds are internal constants/functions, not user parameters. Same master/settings reproduce each field independently of generation order. Climate generation formulas, latitude band, octave settings and warp connection survive; their patterns change because seed derivation changed.

GenerationConfig serialization is schema12 and omits individual seed_offset. Schema5..12 can be read under existing migration rules; legacy noise seed offsets are ignored in memory, not written back to disk. Generic NoiseSettings retains its offset member for isolated noise primitives/benchmarks, but climate validation requires0. Ingame/F8/climate preview and standalone climate editor use the same core derivation.

`assets/editor/master-seed.js` shares the working seed across tabs of one server session (token-scoped localStorage record/storage events and BroadcastChannel). A newly opened experiment adopts the climate draft seed if that browser session already has one, otherwise the published seed. Updates invalidate existing previews. Explicit loading of climate published/draft settings or experiment JSON also selects that seed. Changing a browser seed does not publish game settings. Experiment parameters remain tab-local and are not part of worldgen.json. Browser storage unavailability falls back to channel synchronization between live tabs; neither mechanism synchronizes a separately running game process or a different editor server session.

Experiment JSON version3 omits land_seed and contains hierarchy controls. Version1/2 imports retain their main seed, ignore separate land_seed and initialize new controls, so old maps are not preserved. New views: final continent/island/ocean, coarse regions, archipelago distribution and fine island distribution; existing cell/edge/distance modes retained. Edges compare shared fine IDs inside archipelago; coarse views compare parent IDs. Stats distinguish global parent counts from viewport sample percentages. Marker coordinates are shown only before warp.

## Binary API version3

16 Float32 header fields: version,width,height,coarseCountAxis,coarseSpacing,coarseSitesTotal,fineCountAxis,fineSpacing,sampledLeafCount,milliseconds,coarseStride6,leafStride6,pixelStride4,coarseOffset,leafOffset,pixelOffset.

Coarse record: x,z,landNoise,region(0ocean/1continent/2archipelago),archNoise,reserved0. Leaf record: globalFineId,x,z,islandNoise,edgeWeight(-1 if center outside archipelago),isLand. Pixel record: parentID,leafIndex(-1 outside archipelago),normalized active F1,kind(0ocean/1continent/2island/3archipelago water). Leaf indices are request-local; compare global IDs across views. Response size is bounded by resolution<=1024 and coarse sites<=65536.

## Manual verification

Release build and JavaScript syntax checks passed. Native editor was run hidden with --no-browser in build/release/archipelago-review; no browser/computer-use/synthetic input/CTest/test-suite execution.

- Installed seed105180838,512² full view:350 ocean/518 continent/156 archipelago parents. Sampled area:50.40% continent,5.60% islands,9.60% archipelago water,34.40% outer ocean. Default fine spacing1024. Native response timings149ms unwarped/161ms warped for these individual requests (not a benchmark).
- Repeated request identical excluding reported timing; both opposite edges equal, with and without warp. Seam windows translated by131072 in both axes preserve decoded IDs/F1/kinds exactly.
- Fine queries exist only in archipelago pixels.751 visible fine IDs cross multiple coarse parents. Disabling archipelago produces no fine records. With all parents forced to archipelago, all sampled weights are1 even at maximum fade; internal parent edges do not cause attenuation.
- Fade0 keeps geometry but raises island sample area to7.24%; default fade only removes islands from that classification. Native field image rendered with Pillow and visually inspected.
- One master change changes coarse positions, continent/archipelago signals and both climate probe values. Legacy separate land_seed has no effect. Legacy generation file normalizes to schema12 without seed_offset; invalid subdivisions1/9/2.5,fade.51 and fractional master seed rejected400.
- Shared seed static asset served correctly; tab callbacks/import paths reviewed in source. Actual multi-tab browser interactions and import gestures were not exercised.

Final Release build and packaging completed. Executables and changed runtime assets match build/release/bin. Installed settings.json and worldgen.json hashes remained1597232D238F770788C3E86C69B4C331B3F0310CC30ED52DFD6E4A7558253D62 and E6991E0A058F663EE92C22BC8CAFDC161F62605792BC649E28976E6C935FCDB8 respectively. No draft/backup files were created by diagnostics; the isolated server was shut down. No commit or push was performed.
