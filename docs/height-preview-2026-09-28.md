# Editor height and section experiment — 2026-09-28

Approved scope: color elevation preview and a line cross section in the existing Voronoi experiment. Sea level192, no coast-distance land taper; offshore distance drives seabed only. Actual game flat stone, climate, growth and user JSON remain unchanged.

## Height model

All three independent fields derive fixed domains from the master seed. Periodic Double Perlin broad2octaves/gain.5, ridge3/.5, seabed2/.5. Sample the continuous warped query coordinate, not one height per cell. Broad/seabed spacing2^14, ridge2^12 by default. Let v=clamp(.5+.5*broad,0,1), S(t)=clamp(t,0,1)^2*(3-2*clamp(t,0,1)).

Land shape interpolates (0,0),(.25,.04),(.45,.2),(.6,.55),(.75,.55),(1,1) with S per segment. Elevation=clamp(192+base+relief*shape+mountains*S((v-.5)/.35)*max(0,1-abs(ridge))^3,192,511). Defaults base8,relief160,mountains120. The plateau is a starting experiment, not a biome or final terrain rule. No coast taper; discontinuities at land/sea boundaries deliberately permit cliffs.

Sea t=S(distance/shelf), or1 if no coastline. Elevation=clamp(192-shallow-(deep-shallow)*t+seabedRelief*seabed*t,0,191). Defaults shallow6,deep128,shelf8192,seabedRelief16. Distance remains the pre-warp geometric metric, visually transported with coordinates; it is not physical warped shoreline distance. No rivers, drainage, beaches, surface blocks, erosion or 3D density added.

## API and UI

Optional height parameters in experiment5 default in memory. Validation: base0..128,relief/mountains0..256,broad exponent10..17,ridge8..17,shallow/deep1..191 with deep>=shallow,shelf1..131072,seabedRelief0..64. Optional display.section defaults horizontal whole-world middle line; height is a new view ID. Explicit save stores these fields; existing files are never automatically rewritten.

POST /api/voronoi/preview height_map=true enables heights and coast distances. Ordinary maps omit both unless requesting distance. POST /api/voronoi/section forces heights; endpoints x0,z0,x1,z1 accept horizontal/vertical/reversed/seam-crossing paths, coordinates[-W,2W], each axis span<=W and length>=1. Resolution32..1024 gives a one-row result including both endpoints. UI uses512 samples and the parameters of the displayed map. Pending section requests replace older queued requests, with stale-response rejection. Map navigation remains standard unless explicit line mode is active; Escape/cancel restores navigation. Chart uses a fixed0..512 Y scale, dashed192 sea level, hover coordinates/elevation/depth. Sampling can miss narrow features; graph segments are not full-resolution mesh geometry.

Binary8: header36 floats; existing first32 meanings retained except version8, pixel stride4, site offset36. Header32 hasHeights,33 seaLevel,34/35 sampled height extrema (−1 when omitted). Site stride14 unchanged. Pixel0 ID,1 F1,2 coast distance,3 elevation (−1 omitted). Edge pairs unchanged. Height parameters do not invalidate growth/geometry caches. Shared map-navigation optional lineTool/overlay hooks leave climate callers unchanged.

## Manual verification

No automated test suite, computer use, browser automation or synthetic input. Native isolated editor with copied user configs; JS syntax and static element references checked. Native raster rendered to build/height-manual/height-preview.png and visually inspected (not a browser screenshot).

- Existing site records, first three pixel fields and graph edges matched previous binary7 packaged executable exactly.
- 65x65 whole-world height raster: periodic X/Z edge difference0; all samples finite0..511. Horizontal, vertical and reversed65-sample sections matched corresponding map samples exactly.
- Changing only sea settings left every sampled land elevation exactly unchanged; all2222 sea samples changed. Initial sample land201.52..478.84, seabed53.67..186.
- Fully grown all-land case had zero coast segments and finite heights200.00..443.05.
- Isolated save/read retained height fields, height view and a reversed/seam-crossing section. Invalid deep<shallow and zero-length section returned400.
- Single observed warm512² raster698.16ms; section512 samples3.32ms. These are observations, not performance guarantees.

Release build/package and installed settings/worldgen/workspace hash preservation are required on completion. Browser interaction/layout remains unverified under the no-CU restriction.
