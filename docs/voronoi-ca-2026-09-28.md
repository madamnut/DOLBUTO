# Editor-only periodic Voronoi cellular automaton

User approved land/sea only, removal of archipelago, and CA on existing large cells with `실시`. Game remains flat stone. Climate and master-seed domains are unchanged. No new biome/river/height generation. Archived historical docs describe superseded behavior.

## Rules and geometry

The existing stratified periodic site generator retains its seed domain, cell spacing rounding and jitter. Grid count is 4..256 per axis. Each cell has an independent initial hash using the new `ca_initial` domain; upper 24 bits map to [0,1), land iff below initial probability. Defaults: initial 50%, birth 60%, survival 40%, 5 steps. Percentages accept 0..100; steps integer 0..100. Each synchronous step uses previous neighbors only: sea becomes land at >= birth; land remains land at >= survival. Threshold equality survives/births. No self vote or area weighting. Independent thresholds may permit oscillation; no forced convergence.

Voronoi polygons are clipped by candidate half-planes in normalized stratum units. Every location has a site within sqrt(2), so only competitors within 2sqrt(2) can bound a cell; +/-3 strata suffice. Clipping propagates incoming edge owner IDs. Positive-length edges >1e-9 normalized units define neighbors; point contact is excluded. Edges use canonical periodic IDs, deduplicate as undirected pairs, and populate symmetric adjacency. Polygon shoelace areas are normalized to world area. Double arithmetic/tolerance is used, not exact geometric predicates.

Geometry cache key is grid count/jitter/master seed. CA cache key additionally uses the three probability/threshold values; history extends lazily to requested steps and supports backward selection. At maximum size, 101 byte states per cell are about 6.31 MiB. Viewport/warp changes reuse graph/states. Server currently handles requests serially, so one editor-local cache has no concurrent mutation.

Warping only changes preview lookup coordinates; graph and global area/component statistics remain unwarped. Components include seam adjacency. Largest component statistic is area divided by total land area, zero when no land. Displayed viewport pixel estimate is separately labeled. Native raster still searches 5x5 periodic sites.

## Persistence / UI

Experiment version4 retains common site/warp settings and adds ca_initial_percent, ca_birth_percent, ca_death_percent, ca_steps. Retired land/archipelago/island/fine-cell fields and seed domains are removed from live generation. Versions1..3 import existing site/seed/warp/range and CA defaults, dropping retired views to land. Integer JSON master seed remains integer through save; workspace still stores exactly one shared seed in config. Unified workspace schema1 and climate schema12 are unchanged. Migration is in memory; explicit save writes version4. Existing user JSON files remain byte-for-byte untouched during build/package.

UI supports initial state, one step, specified iteration count, initial/final/random/degree/cells/edges/distance views, per-cell hover, whole-world statistics, geometry/CA/total times and cache indicators. Existing map navigator and section revision conflict checks remain. Initial-view statistics explicitly describe requested final stage.

Binary preview v4 uses float32 throughout: 24 header values, site records of8, pixel records of2, undirected edge pairs of2. Header: version,width,height,count,spacing,sites,steps,total_ms,geometry_ms,ca_ms,components,land_cells,land_area,largest_share,average_degree,geometry_hit,site_stride,pixel_stride,site_offset,pixel_offset,edge_offset,edges,ca_hit,total_area. Site: x,z,initial_random,initial_state,current_state,degree,area_fraction,current_land_neighbor_ratio. Pixel: siteID,normalized_F1. IDs/offsets fit exact float32 integer range.

## Manual verification

Release build passed without compiler warnings. No automatic test suite, CTest, browser automation, computer use or synthetic input. Isolated editor copy served native HTTP and used copies of user workspace/published settings.

- Old workspace normalized to experiment4 without modifying its file; explicit isolated save/read roundtrip retained integer seed and one seed only. Invalid steps101 returned HTTP400. Static HTML served200.
- Regular 4x4 torus: independently expected four cardinal neighbors per cell, including both seams, matched exactly; all areas1/16, total1. Five-step independent neighbor update matched; returning to step0 reproduced initial state and cache hits.
- Irregular 4x4 jitter.25 and1:48 undirected edges (=3V), areas sum1; seam raster edges identical, including warp.
- Initial probabilities0/100 yielded entirely sea/land under default thresholds.
- At1024/16384/65536 sites, average degree6 and normalized area total1. Degrees reconstructed from edge pairs matched site records. Independent next-step calculation matched every native cell. Full-world opposite raster edges matched on X/Z. Subsequent step reused geometry.

Single request timing samples at256 raster resolution, seed105180838, jitter1, current saved warp settings; these are observations, not stable benchmark guarantees:

| Sites | Geometry ms | First5 CA steps ms | Total ms | Next step CA ms | Next request total ms |
|---:|---:|---:|---:|---:|---:|
|1024|4.25|0.24|19.89|0.023|22.06|
|16384|35.24|0.95|47.68|0.224|13.48|
|65536|105.86|4.10|124.16|1.361|18.65|

Total includes geometry, CA, component statistics, warp/raster preparation and filling response, excluding HTTP transfer/browser drawing. Actual browser interactions were not exercised. JS syntax checked with node --check.

## Homogeneous-interior wildcard follow-up

User requested independent flip probabilities for sea cells with all-sea neighbors, and land cells with all-land neighbors, then authorized `실시`. Editor exposes ca_sea_flip_percent and ca_land_flip_percent, each numeric0..100 with fractional values, default0/off. Optional fields in experiment version4 normalize missing values to0 (no automatic file rewrite). UI save/import/export includes both; binary protocol unchanged.

Eligibility uses the previous synchronous state, including the center cell. Normal CA result is computed first. A successful eligible wildcard overrides it with the inverse of the previous center state; a failed draw keeps normal CA. This preserves all prior threshold behavior at0%, including birth threshold0. At100% all eligible cells flip; mixed neighborhoods do not receive wildcard changes. New isolated cells may disappear next iteration; no persistence mask is added.

For destination step k=history.size(), starting1, stream=mix_seed(master XOR ca_wildcard=0x43415743) XOR mix_seed(k). Cell draw=(mix_seed(stream XOR canonicalID)>>8)/2^24; success when draw<probability/100. Separate toggles choose probability based on prior center state; existing initial draws and geometry are unaffected. Both probabilities enter CA cache identity; geometry cache is unchanged. History replay, direct multi-step requests and rebuilds are deterministic.

Manual isolated HTTP observations after Release build: all-sea/all-land with both100% alternated completely at steps1/2; independently computed five-step states at0/0,23.5/71.25,100/100 matched all1024 cells. Backward/forward history replay and a geometry rebuild reproduced identical results. Parameter changes invalidated CA cache but reused geometry. Legacy missing fields normalized0 without file writes; explicit isolated save/read preserved23.5 and71.25. Negative, >100, string and null input returned400. JS syntax checked; no automated test suite or browser interaction used. Installed settings/worldgen/workspace hashes remained unchanged through packaging.

## Unconditional wildcard correction

User explicitly replaced the two homogeneous-interior probabilities with one unconditional state flip (`실시`). Current field is ca_flip_percent, default0, numeric0..100 including fractions. Retired sea/land fields are dropped; missing new field defaults0 without rewriting existing user files. Experiment4 and binary4 remain unchanged. Each cell in every step participates regardless of its own or neighbor states. Success overrides ordinary CA with !previous[cell]; failure keeps ordinary result. At100% every cell alternates, including mixed neighborhoods. This is not a flip of the already computed next state. Hash inputs are unchanged; CA cache identity now has one flip probability. UI and persistence include the single field. The prior homogeneous eligibility description is historical.

Verification: Release build/package and JS syntax passed. Isolated HTTP five-step comparison for1024 mixed-state cells matched independent normal+unconditional-hash updates at0%,23.5%,100%. At100%, every cell was the inverse of initial after5 steps; history replay matched. Legacy read preserved file bytes/default0; isolated explicit save/read returned23.5. Packaged user settings/published/workspace hashes remained unchanged. No automated test suite or browser interaction.
