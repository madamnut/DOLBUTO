# Fixed spatial growth probabilities

User approved a fixed large/small noise map to reduce isotropic growth, including initial settings, with `초기설정넣어서 만들어봐 실시`. Keep manual steps, existing land, unconstrained merging, climate/game flat stone and periodic world. Do not add permanent barriers, area/cost budgets, deletion or random reseeding.

## Fields and formula

Each canonical unwarped Voronoi site is sampled once at its physical XZ position. Use existing PeriodicNoise (periodic2D Double Perlin), two octaves each, gain.5. Master domains growth_large=0x474c4152 and growth_small=0x47534d41 isolate new signals from existing geometry, seed placement, growth draws and climate. Both noise families retain integer lattice periods through existing implementation. Large/small refer to intended scale, not a hard ordering constraint; users can freely exchange scales.

Defaults: growth_noise_enabled=true; growth_noise_strength=8 (range0..24); growth_large_log2=14 (range9..17); growth_small_log2=12 (range9..17); growth_detail_mix=.25 (range0..1). Sizes mean2^n blocks before Double-Perlin integer period rounding. Raw noise is not guaranteed in[-1,1]; map colors alone saturate at endpoints.

signal=(1-detail)*large + detail*small; p=base_growth_percent/100. For0<p<1, local_p=p/(p+(1-p)*exp(-strength*signal)). This shifts log odds smoothly and does not introduce a hard blocked threshold. Exact base0/1 remain0/1; disabled/strength0 use the original uniform double probability exactly. All simulation probabilities are double; preview outputs float. Noise is float. Each step keeps the existing master+step+cell hash draw and compares it to local_p. Thus the probability landscape stays fixed, while independent step draws still advance the front. Initial seeds do not depend on this map.

No extra per-frame noise sampling: graph/growth preparation batches both maps over all sites and stores probabilities/large/small vectors. History cache keys include all five settings, seed count and base probability; geometry cache remains unchanged. Rebuilding on rule changes starts at step0. Step/range/warp-only changes reuse prepared map. Noise fields are still prepared when disabled so their diagnostic views remain available.

## UI and data

New controls under seed growth; views growth-rate (black0%→white100%), growth-large/growth-small (raw−1→+1 saturation), hover actual percentage. Gray map is the actual probability used by the simulation, per cell, not an unrelated continuous image. Existing preview coordinate warp chooses the same cell and shows its probability, preserving the distinction between visual warp and topology.

Experiment JSON5 gains optional fields; missing fields normalize to these initial defaults, preserving old geometry/seed/steps/base probability. No automatic rewriting of installed workspace/published/settings. Explicit save/import/export carry all map fields. Binary6 retains28-float header and2-float pixels/edges, increases site stride10→13: slots10 local_p,11 large_raw,12 small_raw. Older header offsets retain semantics. Client version/stride guards enforce matching executable/assets.

## Manual verification

Release build passed; node --check and element-reference inspection used, no browser automation/computer use/synthetic input/automated test suite. Isolated old packaged executable and new native executable were queried via authenticated HTTP with copied configurations.

- Disabled map matched all old site fields and raster exactly; strength0 matched disabled including prepared fields. Base0/100 matched old owners and returned uniform0/1.
- Default seed105180838,16384 sites,base50%,32 seeds,12 steps: probability percentiles(min,5,25,50,75,95,max)≈.05,2.55,17.83,50.17,81.93,97.52,99.96. Raw large≈[-1.0813,1.0617],small≈[-1.1382,1.2100]. Recomputed formula maximum float-preview error2.98e−8. Changed4327 occupancy cells compared to uniform.
- Initial seeds unchanged; prepared map identical atsteps0/12; existing land preserved atstep13. History replay and geometry rebuild reproduce all site fields. Full-world opposite raster edges match on both axes with warp.
- Isolated save/read retainedstrength9.5/detail.4 and growth-rate view. Invalidstrength25/log2=8/detail1.1/enabled=1 returned400.
- One observed native default request at512 raster: geometry cache hit, map+growth12.68ms, total65.98ms; not a stable benchmark guarantee.
- Native raster diagnostic comparison saved to ignored build/growth-map-manual/comparison.png: uniform12steps, fixed-map12steps, probability. Visually inspected the rendered image without browser interaction; new growth follows broad fast regions with slow-water inlets and irregular fronts. This does not guarantee no round regions for every seed/scale.

Packaged installed settings/worldgen/workspace bytes remain unchanged. User opens editor to adjust steps and probabilities; no game terrain change.
