# Editor coastline distances

User approved the next diagnostic layer: distance inland and seaward from land/sea boundary. No height mapping, surface material, growth behavior, climate or game world changes.

## Geometry / distance

The existing Voronoi half-plane clipper retains each positive-length polygon edge together with its two canonical cell IDs. Store once from the lower canonical ID side. This includes seam images and distinct edges between the same periodic cell pair if present. Site positions, neighbor graph and growth are unchanged.

At a requested growth frame, filter edges whose two owner occupancies differ (land vs sea); different continent IDs with both sides land do not create a coastline. Recenter each segment midpoint into canonical131072 square. Endpoints may extend outside the canonical tile. Build a median-split AABB hierarchy (longest box axis, leaves<=8 segments). Exact point-to-segment squared distance uses clamped projection, including endpoint distance. Query the canonical point and eight neighboring periodic point images; box lower bounds prune branches. Final sqrt converts to blocks. This is double floating geometry, subject to existing clipping tolerance1e−9 stratum units; not exact predicates.

For each cell, retain the distance at its original site. For each rendered pixel, query its actual sample coordinate directly, so the image is continuous rather than one flat distance per cell. The existing warp moves the query coordinate before nearest-cell and coast-distance lookup. Thus values are distances in the unwarped world, visually transported by warp, not physical distances along warped coast geometry. Both inland and sea distances are the same unsigned metric, masked by sampled occupancy; disconnected seas/lakes all count as sea boundaries.

No coastline (all land or all sea) is undefined: return−1 rather than0 or a fabricated maximum; UI marks purple and explains. Maximum distance statistics refer only to sampled site centers, not continuous global maxima.

## Cache / performance

One CoastMap for the current requested step holds filtered segments, hierarchy and site distances. Invalidate on graph/growth-rule regeneration or requested step change. Viewport, display scale, resolution and coordinate-warp changes reuse the prepared map but new raster samples still query exact distances. Build preparation time is separate; total time includes per-pixel queries.

Browser requests coast_distance=true only for coast-land/coast-sea/coast-both. Otherwise it sendsfalse and skips preparation and all distance queries. API defaulttrue supports explicit diagnostic clients without extra flags. Existing successful distance raster can be recolored between the three views/scale immediately. Switching to distance while a non-distance request is pending advances revision and queues the required request; stale results are discarded. Changes honor existing map navigation and stored automatic-preview behavior; explicit selection of a not-yet-computed distance view performs its requested calculation.

## UI / protocol

Views: coast-land masks sea dark blue; coast-sea masks land dark green; coast-both shows both. Gray is linear distance/display.distance_scale, default8192, allowed finite1..131072, clipped white above scale. Suppress cell-edge and site overlays during distance views to avoid confusing cell edges with coast; cell selection highlighting remains. Hover shows sampled-point distance. If current raster omitted distances, hover says to calculate in distance view. No browser automation was used to verify interaction.

Experiment JSON5 gains optional display.distance_scale; old files default8192 in memory. Save/import/export preserve it and the new view IDs. Existing user files are not auto-rewritten. Binary7 retains header fields0..25 with version7/site stride14/pixel stride3/site offset32; header26 preparation_ms,27 preparation_cache_hit,28 number_coast_segments,29 max_land_site_distance,30 max_sea_site_distance,31 distance_fields_present. Without distance fields,28..30 and per-site/per-pixel distances are−1; present flag distinguishes skipped work from genuinely absent coast. Site13 adds unsigned site distance; pixel2 adds unsigned sample distance. All former site0..12 and pixel0..1 semantics unchanged. Edge pairs unchanged.

## Manual verification

Release build, node syntax and static element references checked. No automated test suite/CTest, computer use, browser automation or synthetic input. Isolated authenticated native HTTP with copied user configurations and comparison against prior packaged executable.

- Regular4x4 torus, one seed, zero steps: analytic square-island distances matched all33x33 raster samples within.00315blocks (float32 output);41 exact boundary samples were0 and four coast segments present.
- Irregular4x4: independently clipped the single land polygon against every periodic competitor site, then brute-forced all segment images. Five edges agreed and max error.00717blocks, using float32-exported site positions for independent calculation.
- Existing site0..12 and pixel0..1 matched old executable exactly. Site/pixel distances finite; full-world opposite X/Z edges identical with warp. Different raster resolution reused site distances; step replay reproduced all distances. All-land state returned zero coast segments and−1 throughout.
- Isolated save/read retainedcoast-land and4096.5 display scale; invalid0/131073/null rejected400.
- Lazy request sequencefalse/true/false/true produced correct presence flags, omitted sentinels, unchanged original fields, cached preparation on return and identical distances. One observed512 raster warm normal request49.23ms vs full distance626.17ms; initial full-distance sample709.08ms including32.65ms preparation and5992 coast segments. Single observations, not stable performance guarantees. Accurate distance raster costs are isolated to these views.
- Native output rendered and inspected at build/coast-manual/comparison.png (land/sea, inland, sea distance; white8192). Smooth inland/offshore gradients and dark boundaries visible; image is a diagnostic raster, not a browser screenshot.

Installed settings/worldgen/workspace hashes verified preserved through final packaging.
