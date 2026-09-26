# Voronoi land/ocean experiment — 2026-09-27

## Approved behavior
User approved a broad periodic land distribution, sampled once at each original Voronoi site and classified per cell. This extends only the standalone editor experiment. Temperature, precipitation, game flat stone terrain, F8 and worldgen schema11 are unchanged. No terrain height, river or coastal-slope rules.

Native editor module samples PeriodicNoise at double-precision unwarped site coordinates. Land distribution uses independent uint32 land_seed (default23917), spacing (default32768, range512..131072), fixed3 octaves, gain.5, seed_offset0. Octave spacings are requested/2^i; DoublePerlin families round to integer periodic lattice counts. Exact land rule: sampled Float32 value > threshold; equality is ocean. Threshold default0, range−1..1. Raw noise remains unclamped; grayscale is clamped for display only. Changing land seed/spacing/threshold leaves site positions, IDs, F1 distances and boundary warp fields unchanged. Boundary warping changes which owner appears at each coordinate but retains the owner's land value/class.

## UI and persistence
Default view is green land/blue ocean. Additional per-cell land-noise grayscale, existing ID colors/edge/F1 modes retained. Three numeric controls and independent random land seed. Hover shows class/value; stats distinguish whole-world cell counts from sampled area fraction of current viewport. Fractions are approximate and do not count highlighted cells differently. Threshold is not an exact area target. Actual first-octave spacings displayed; heights/beaches/cliffs not implied by colors.

Experiment JSON exports version2 with land fields; imports version1 with all land fields initialized to documented defaults. Version2 requires valid explicit values. No game config publication path added. Settings remain tab-only unless explicitly exported.

Binary response now has unchanged6-float header, then site_count records of4 floats (canonicalX,Z,land_value,is_land), then unchanged2-float pixel records (ID,F1/spacing). Same-request site classifications are authoritative in UI, including equality behavior. Max-site memory is bounded at65536 records. Main server route, authentication and content size limits are unchanged.

## Verification
Release build and JS syntax/whitespace checks passed. Manual isolated HTTP inspection (build/release/voronoi-land-review), not an automated test suite:
- Default1024 cells:465 land/559 ocean; raw values−.73588..+.84138;512² sampled land area45.54% plain/45.53% warped.
- Both plain/warped opposite X/Z edges and translated seam windows identical. Warping did not alter the site table or classification.
- Threshold−1/−.25/0/.25/1 yielded1024/806/465/245/0 land cells; exact rule matches every site; site coordinates/raw noise/pixel ownership/F1 unchanged.
- Different land seed or spacing changes raw distribution while preserving all geometry; geometry also byte-compared equal to previous version's saved512² map.
- Invalid fractional seed/zero spacing/out-of-range threshold rejected400.
- Climate probe still temperature−.004185318946838379 and precipitation−.00013596461212728173 at(0,32768) with installed settings.
- No publication/draft/backup files created; copied legacy worldgen bytes unchanged. Native data map rendered to land-ocean.png and inspected visually. Browser UI interaction/import gestures were not exercised; no computer use, synthetic input or CTest.

Installed settings/worldgen preservation and packaging confirmed separately. No commit or push requested.
