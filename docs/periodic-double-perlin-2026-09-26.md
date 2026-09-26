# Periodic Double Perlin / original nested terrain (AI context)

## Approval and scope

User approved `실시` after agreeing to replace Simplex, match shared Shift, original nested offset/factor/jaggedness splines and the base 3D density composition. World size131072, height512, sea192 and temperature latitude bands stay fixed. Build/package included. No CTest, automated regression registration, synthetic input or computer use. Explicit isolated numerical/CLI measurements are within this change. Original reference is the locally inspected **Minecraft Java26.2 normal overworld**, not an assertion of26.3 parity. Caves, aquifers and biome placement are excluded.

## Implemented model

- `PeriodicNoise` uses two independent Perlin families. First/second desired frequency ratio1:1.0181268882175227; each octave's cells per world are rounded separately to an integer, minimum1. Effective frequency=cells/131072. This deliberately changes exact original frequencies to preserve the torus world without trigonometric torus coordinates.
- FastNoise2 gradient/hash interpolation is dispatched through SSE2/SSE4.1/AVX2/AVX512. Coordinate reduction uses double before float SIMD fractions. This retains fine Jagged samples at large coordinates. Hashing is project-owned seeded lattice hashing, **not Minecraft permutation/Xoroshiro/MD5 seed parity**. 2D signals use true 2D Perlin gradients scaled to length sqrt(2), not original 3D fixed-Y slices. The independent reference page retains the original exact seed/noise evaluator.
- For N octaves, amplitudes are multiplied by `2^(N-1)/(2^N-1) * 2^-i`. Double-Perlin normalization is `(1/6)/(.1*(1+1/(active_index_span+1)))`. Input weights are amplitudes, not normalized fractions of their sum. Preview-only weighted octave mode retains original indices/seeds/full normalization, so its sum equals the full signal.
- Groundness spacing2048, amplitudes[1,1,2,2,2,1,1,1,1]; Smoothness2048/[1,1,0,1,1]; Weirdness512/[1,2,1,0,0,0]; temperature4096/[1.5,0,1,0,0,0]; precipitation1024/[1,1,0,0,0,0]; Jagged base65536 with input multiplier1500 and16 amplitudes1. Other octaves halve spacing. Groundness retains custom per-octave spacing, now .001..131072; period count capped1e9. All effective spacings are visible in editors.
- One `shift` field: base spacing32, amplitudes[1,1,1,0], strength16 in world blocks. Same 3D Double Perlin evaluated at `(x,0,z)` and `(z,x,0)`; all internal axes periodic. Five main signals select shift by default; Jagged is unwarped and validator rejects a warp for it. The original .25 input scale and displacement4 are absorbed into world-space spacings/strength16. No extra per-signal warp fields.
- Temperature raw noise is X-periodic with a straight Z axis; the unchanged latitude band suppresses variations at both polar boundaries. Precipitation is X/Z periodic.
- Authoritative splines use recursive Hermite trees on C/E/W/PV, including endpoint tangent extrapolation. `periodic_spline_preset.inc` contains only numerical default control points/tangents converted from the previously verified original reference data. Legacy grid storage/evaluator remains solely as an internal compatibility representation; schema10 requires trees. Old diverse/ridged presets are removed rather than overriding the new trees.
- Blended3D uses original limit16/limit16/control8 octaves, parameters xz_scale.25/y_scale.125/xz_factor80/y_factor160/smear8. Frequencies684.412*scale, descending octave frequencies and reciprocal amplitudes; limit outputs divided by65536, control blend=clamp((control/10+1)/2). Only contributing limit samples are evaluated. Original vertical smear formula is retained. X/Z octave periods are integer-quantized. Y maps `(world_y-192)*.75+63`.
- Profile height=`192 + height_scale*(offset+.0040625) + jagged_scale*jaggedness*half_negative(jagged_noise)`; default scales512/3. Per-block gradient slope=`squash*factor/height_scale`, default squash1. Density adds Blended3D to `4*quarter_negative((height-y)*slope)`.
- Original bottom slide0..24 stretched to0..32, top slide original upper80..64 stretched to world upper106.667..85.333, target densities .1171875 and-.078125. Final monotone squeeze is omitted from block classification because its sign is identical **after interpolation**. Density (including slide and nonlinear quarter-negative) is computed at the existing4-block lattice before trilinear interpolation. No caves/aquifers/post-density additions.
- Surface search and block generation share this lattice formula. TerrainTile retains25 profile corners. Conservative uniform chunk bounds consider all lattice corners and exclude slide transition regions; bound|Blended|<2. Chunk generation remains column-data/independent chunk compatible.

## Settings and UI

Schema10 `periodic_double_perlin`, `blended` object, tree splines, noise `frequency_multiplier`. Shape JSON only retains its active `seed_offset`; old shape octave/vertical_scale fields are not serialized. Legacy config members remain only for the historical opt-in torus benchmark.

Old schema5..9 spline-mode configs migrate in memory to the new defaults, retaining seed and temperature_bands. They cannot preserve old terrain identity. No automatic save on read. This explicitly approved switch updated both assets/worldgen/default.json and out/Sandbox/worldgen.json. Original runtime bytes are backed up as `worldgen.before-periodic-double-perlin-2026-09-26.json.bak`, SHA256 E16C2EED246E6B85ECA968FCF5E2C98A7328CB309A1C60BDF03862DBB7DDB7DB. Existing draft bytes are backed up if present; drafts are not overwritten. settings.json SHA256 remains4C52D19C6722F79F1853A9C71D23989CAF0CA1FC5B608E6618D88B84491CDEE1.

F8 has shared Shift, effective octave spacings, original Blended parameters, nested spline tree editing and3D toggle. Web tree editor keeps draggable controls/tangents, child navigation, whole-tree copy/paste, arbitrary C/E/W slices and map preview. Reference page remains independent/read-only. Browser interaction was not used; actual browser layout/input is not visually verified.

## Explicit inspection results

Unregistered helpers/artifacts under build/release/periodic-transition and periodic-http-inspection. No helpers are packaged.

-12 maps repeated by±131072: max difference0. Shape and Shift's swapped Y-plane periodic difference0.
-12 map128x128 previews: both opposing edges exactly equal, all finite.
-13*13*41=6929 spline inputs ×3 outputs vs independent original reference: max1.6689301e-6 (float PV/interpolation rounding). Paired HTTP curve sweeps max7.1525574e-7.
-433 nested nodes opened through HTTP curve API; edited tangent and fractional spacing accepted.
-Weighted Groundness octave contributions sum to full output within8.94e-8.
-Boundary left/right slope difference decreases with step: h1:.0027744;h.5:.0014508;h.25:.00073488;h.125:.00037289. These are finite-difference truncation observations, not mathematical equality of arbitrary derivatives or exhaustive parameter verification.
-Six complete columns /786432 block cells: highest solid agrees with surface rule lookup,0 mismatches; periodic column copies0 block mismatches. New default only. Source proof bounds additionally covers early-outs.
-Single thread16384 arbitrary samples,7 measured repetitions after1 warmup, median ms: Groundness3.1234, Smoothness2.2876, Weirdness2.1383, Jagged2.2826, Blended3D3.592. Six-column means profile/surface4.80695ms + blocks1.5616ms, excluding lighting/meshing/streaming.

Whole256x256 HTTP map latency,7 repetitions afterwarmup (includes request/config/row construction, not pure noise):

|map|old saved Simplex config ms|new original defaults ms|
|---|---:|---:|
|Groundness|36.707|18.995|
|Smoothness|29.683|14.148|
|Weirdness|32.971|13.839|
|Jagged|28.978|13.993|
|base height|54.720|72.489|
|effective height|61.567|84.506|

Both settings AND algorithms changed: these are practical preview measurements, not equal-work algorithm benchmarks. Individual noises improve, full height maps slow down~32.5/37.3% due to the new composite workload. Do not claim a universal generation/frame-rate improvement.

Actual isolated game CLI: render radius4, profile origin1500/2000, camera300/yaw0/pitch-25/time09:00,30 measured GPU frames. Exit0,49columnsloaded, screenshot saved, Vulkan errors0/UI issues0/texture failures0.14 shader interface performance warnings from unchanged graphics pipeline; not fixed in this worldgen work. Existing warning behavior is separate from build warnings. No synthetic input/CU. This is a stationary loading/render check, not broad gameplay verification.

Release build and packaging completion recorded in docs/verification.md after final checks.
