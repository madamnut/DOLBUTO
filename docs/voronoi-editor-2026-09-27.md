# Editor-only periodic Voronoi experiment — 2026-09-27

Historical initial implementation. The land/ocean extension and response/experiment format changes are documented in voronoi-land-ocean-2026-09-27.md; that newer scope supersedes the initial no-land-assignment limitation below.

## Scope and isolation
User confirmed temperature/precipitation remain, and approved Voronoi experimentation only in the editor. No land/ocean assignment, terrain height, rivers, biome decisions, graph extraction, or game generation changes. Native implementation is linked exclusively into worldgen_editor, not world_core or DOLBUTO. Independent web page and authenticated POST /api/voronoi/preview are computational only; they bypass world-generation config parsing and never publish/draft/save. Main editor exposes a token-bearing new-tab button. No outside services.

## Algorithm
World period131072. Requested spacing512..32768 rounds to a whole stratum count4..256 per axis, actual spacing=131072/count. Each stratum owns one deterministic hashed site, interpolating center to a random within-stratum location as jitter0..1. Site ID=canonicalX+count*canonicalZ. Neighbor images share IDs and use unwrapped positions across seams. F1 searches5x5 strata: the home site is less than sqrt(2) cells away, and any site beyond the search neighborhood is at least2 cells away. Equal distances choose lowerID. Pixel distance is normalized by actual spacing. No F2 or geometric edge-distance approximation is claimed.

Optional warp samples two independent periodic2D DoublePerlin fields with experiment seed, offsets71191/193327, octave gain/spacing and strength. Neither reads nor modifies climate parameters. Displaced query coordinates wrap before Voronoi lookup. This coordinate warp can fold/split apparent cells at high strength; it is not guaranteed to preserve region topology. Site markers only appear without effective warping because native site coordinates precede the coordinate transform.

## Interface and format
Defaults: seed1337, requested spacing4096 (32x32 sites), jitter1; warp OFF, strength1024, exponent11, octaves3, gain.5; map512. Views: stable ID colors, raster boundaries, F1 distance. Borders detect unequal neighboring sample IDs, not an exact extracted graph. Clicking highlights one ID; hover shows canonical coordinate/site/F1. Full-world, X/Z seam views, wheel zoom, drag-pan, explicit ranges (including seam crossing), PNG export. Browser-only parameters can be exported/imported as dolbuto-voronoi-experiment version1 JSON, separate from worldgen.json. UI never exposes a game-publish action in this page. Automatic requests debounce300ms and serialize/coalesce pending work; stale results are discarded.

Response Float32: width,height,cells,spacing,site_count,compute_ms, then site_count canonical(X,Z) pairs, then row-major (ID,F1/spacing) pixel pairs. Parameter/range/finite/integer bounds checked by server; max map1024, max65536 sites. View extent per axis1..131072; absolute coordinates−131072..262144.

## Verification
Release game/editor build succeeded with no new compiler warnings; JS syntax and diff whitespace checks passed. No automated tests, CTest, browser/computer automation, synthetic input or game interaction.

Isolated manual HTTP inspection (build/release/voronoi-review):
- Plain and warped512x512 full-world maps finite; opposing X and Z edges exactly equal for ID and F1; translated seam windows identical.
- Repeat output exact excluding compute duration.
- Independent full-site nearest search at16 sites jitter0/1 and49 sites jitter1 found zero owner mismatches; max normalized distance error3.23e-7 using Float32 returned site coordinates.
- Invalid seed/spacing/fractional octaves/jitter rejected400.
- Climate API still returns the same raw values at the sampled location; installed legacy worldgen copy remains byte-identical and no draft/backup files created.
- Single-request native durations on this PC: plain23.1ms, warp37.3ms for512x512/1024 sites. These are observations, not a statistical benchmark or frame-time estimate.
- Native data rendered to an inspection image and visually checked; browser layout/gesture behavior not exercised because computer use remains disallowed. Logs/raw maps/comparison.png under ignored build/release/voronoi-review.

Installed settings/worldgen must remain byte-identical across packaging. No commit/push requested.
