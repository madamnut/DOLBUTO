# Global cell tendencies — 2026-09-28

## Contract

User-approved editor preprocessing, NOT final surface elevation. Cell boundaries are not biome boundaries. Existing land/sea growth, climate, game flat stone and saved user files remain unchanged. Retire previous HeightModel, section route, line drawing and section chart. No block-height conversion, final density/compression formula, river network or biome integration in this scope.

Each canonical Voronoi site stores altitude tendency and compression tendency. Altitude sign represents the cell's pre-existing ownership: land positive, sea negative. Magnitude .01..1 is regional high/deep tendency; compression0..1 is normalized weak/strong tendency. These have no block units or discrete terrain classes. Future world generation must explicitly define how they become density parameters.

## Preprocessing

Use the selected global growth frame and existing exact pre-warp coast distance at every site. d<0 (no coast) uses distance tendency1, otherwise distance tendency=1-exp(-d/distanceScale). Raw magnitude=clamp(.12+distanceStrength*distanceTendency+sharedStrength*broadNoise+localStrength*siteRandom,.01,1). Broad and compression are independent periodic DoublePerlin2octaves/gain.5, derived with terrain_tendency/terrain_compression domains; site random uniform[-1,1) uses terrain_local. All master seeded, no per-feature seed controls.

Initial compression=clamp(baseCompression+variation*compressionNoise,0,1). Apply configurable Jacobi passes: mix each previous-pass center with the mean of previous-pass neighbors of the SAME land/sea type. Isolated cells retain own values. Apply sea negative sign afterward. This is deterministic correlated variation, not a neighbor class matching rule or randomized discrete tier assignment. High local/shared strength permits exceptions to distance trends; stronger mixing suppresses isolated exceptions.

Defaults: distance8192, distanceStrength.45, sharedStrength.65, localStrength.15, noise log2 spacing14, neighborMix.55, passes3, compression.45, compressionVariation.35, transitionWidth.65. Limits are visible in numeric controls. Changing tendencies never changes owner IDs or growth history.

## Shared constraints and coordinate lookup

Reuse actual periodic polygon boundary segments. Each undirected segment stores its two owning cells, shared endpoints and shared midpoint. Midpoint value is the mean of the two centers. Coincident clipped vertices are deduplicated using neighboring .001-block spatial buckets and distance tolerance1e-5 blocks, including toroidal wrap. Coordinates themselves are retained, not quantized. Each common vertex is evaluated once by a compact smooth radial kernel over surrounding centers, radius2 grid spacings, weight=(1-r)^4*(1+4r), normalized. Include all owner types so a shared corner has one value.

Every cell references its incident shared edges. Query nearest canonical cell, then locate a fan triangle(center, edge midpoint, endpoint), compute barycentric coordinates. Along boundary, interpolate midpoint/endpoint values linearly. Blend toward center using smoothstep(clamp(centerBarycentric/transitionWidth,0,1)). Small width keeps the center plateau larger and confines transitions near boundaries. Values agree across shared edges and within each fan; C0 continuity, not guaranteed slope continuity. Degenerate numerical fallback evaluates the common radial field. Representative mode simply returns the center record.

Warping transforms query coordinates before lookup, not topology/preprocessed values. Thus displayed cell shape can fragment under strong warp, as before. Interpolated signed altitude can cross zero inside either ownership type near coasts: it does NOT reclassify land/sea or move the coastline. Actual generator must later combine these tendencies with ownership and define coastline/density behavior. No claim of finalized cliffs or beaches.

## Cache, protocol and persistence

One complete global TrendMap in editor memory: centers, unique vertices, edges, cell-edge references. Cache key contains seed/grid geometry/growth config/frame and tendency generation settings. Viewport, resolution, warp, representative/interpolated mode and transitionWidth reuse it (width changes lookup only). Growth/geometry caches remain independent. Coast preparation is required for tendencies; per-pixel coast queries are skipped unless explicitly requested by coast_distance. Memory estimate includes populated records/references, excludes capacity overhead and other caches.

Experiment schema6 reads1..5, retains earlier site/growth/warp migration, defaults new fields, discards obsolete height_* and display.section. Old height view maps to trend-altitude; old files stay byte-identical until user explicitly saves. Workspace schema1 remains. Settings only are persisted, not preprocessed datasets.

POST /api/voronoi/preview adds trend_map and interpolate flags. Binary9 header40: existing0..31 meanings retained with siteStride16/pixelStride5/siteOffset40;32 hasTrends,33 preprocessMs,34 cacheHit,35 populatedBytes,36 boundaryOffset,37 boundaryCount,38 boundaryStride14,39 transitionWidth. Site0..13 unchanged (site coast distances also present when tendencies required preparation),14 altitude,15 compression. Pixel0ID/1F1/2coast/3altitude/4compression. Header31 still indicates per-pixel distances; absent pixel distance−1. Absent tendencies0, distinguished by header32. Edge pairs unchanged. Boundary14=[ownerA,ownerB,Ax,Az,Aalt,Acomp,Bx,Bz,Balt,Bcomp,Mx,Mz,Malt,Mcomp]. M=midpoint. All coordinates pre-warp canonical. /api/voronoi/section is no longer handled.

UI four views: representative altitude/depth, representative compression, interpolated altitude/depth, interpolated compression. Representative values are single colors per cell; optional edges gently darken colors. Smooth views suppress cell edges/sites. Selected cell inspector shows center and incident common constraints with coordinates as tooltips. Chart and actual height values removed. Older in-flight responses cannot overwrite newer request revisions; interpolation mode captured with each request.

## Verification

Release native build succeeded. No automated test suite/CTest, CU/browser automation or synthetic input. Isolated native HTTP calls used copied user settings. Node syntax checks and static DOM IDs checked. Native fields rendered with PIL to build/tendency-manual/tendencies.png; not a browser screenshot. Browser interaction itself remains unverified under the no-CU policy.

- Previous site's first14 fields, pixel owner/F1 and graph edges exactly matched pre-change native output for identical user configs.
- 16,384-cell representative raster exactly matches each owning center; land positive and sea negative. Sample center range−.82715..+.82469.
- Whole-world smooth raster X/Z opposite edges exactly equal, including enabled warp.
- Representative→smooth, viewport/resolution change and transitionWidth change reuse preprocessed centers exactly; compression setting change rebuilds.
- Independent small4x4 native point probes: centers exactly reproduced; midpoint values max error1.79e−7 (float32 coordinates exported), .04-block two-sided boundary separation max value difference6.26e−7.
- Regular4x4 all-land growth with four-way corners and no coast produced finite values; no-coast segment count0.
- New settings save/read roundtrip retained schema6/new view/parameters, omitted retired fields. Invalid width0 rejected400; removed section route rejected400 by generic unknown-command handling.
- Populated TrendMap data5.00MiB for16,384 cells. One observed first preprocessing85.15ms; after further probes/rebuild, preprocessing131.33ms and full representative512 raster240.46ms. Warm smooth512 raster99.47ms. These single observations are not stable performance guarantees.

Final package preserves settings.json, worldgen.json and worldgen.editor.json hashes. No game terrain generation enabled; no commits/pushes.


## Follow-up: independent land and sea tendencies

User approved separation after feedback that shared land/sea tendency patterns look unnatural. This section supersedes mixed-type interpolation above. Terrain land broad/local streams unchanged. Sea broad and independent cell jitter now use sea_tendency/sea_local domains derived from the same master. Controls remain common in this scope, but random fields are distinct. Compression representative field and all prior owner/growth/climate values remain unchanged.

Vertices and edge midpoints store two Value records indexed by sea/land. Vertex kernel excludes the other type, preserving shared values between same-type incident cells, including multi-cell coast corners. A type with no supporting site receives an unused zero sentinel; any incident owner has support within the radius. Same-type edge midpoint remains the mean of its two centers. At a coast each side retains its own center value at midpoint. Fan lookup selects the owning type's constraints, as does the numerical fallback. Thus interpolation cannot flip the altitude sign or wash out coastal highland/deep-sea tendencies. There is deliberately no continuity requirement across different land/sea types: these are independent input conditions for a future terrain generator, not a rendered cliff profile. Within the same type and across the torus, continuity remains.

Binary10 keeps header40/site16/pixel5/edge2. Boundary stride20: fields0..13 follow prior layout but values belong to ownerA;14..15 endpointA ownerB value,16..17 endpointB ownerB value,18..19 midpoint ownerB value. Same-type sides duplicate equal values. UI uses selected owner's side; coastal inspector adds a separate opposite-side row. JSON schema6 unchanged, no new parameters or file rewrite. Existing saved experiments deterministically acquire new sea patterns on load.

Manual native checks with isolated copied configurations: previous site0..13, land center altitude and all center compression values exactly unchanged;8742/8770 sea centers changed (clamping can leave identical endpoints).5992 coastal midpoints exactly retain each side's center values; same-type paired edge records identical.512² smooth raster finite, every altitude sign matches sampled cell ownership, both torus edges identical.17 small-world coastal midpoint two-sided probes at.02-block offsets max value error1.19e−7; regular all-land/no-coast finite. Returning from other geometry reproduces all center and boundary records exactly. JS syntax and DOM references checked, no browser automation. Release build/package and user JSON hash preservation completed.
