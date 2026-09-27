# Archipelago candidate white noise — 2026-09-28

User approved a per-large-cell white-noise gate before the existing archipelago decision. No implementation in game terrain; standalone editor only.

`SeedDomain::archipelago_candidate = 0x41474348` is a new independent domain; all previous explicit domain constants remain unchanged. Candidate = float(mix_seed(derive_seed(master,domain) XOR canonicalCellId) >> 8) * 2^-24. The result is exactly representable in Float32, includes0, excludes1, and depends on neither viewport nor warp nor request order. Changing cell layout/count may change the spatial distribution of IDs as before.

Classification: landNoise>land_threshold is always continent. Otherwise enabled AND candidate<arch_candidate_percent/100 AND archNoise>arch_threshold gives archipelago; else ocean. Equality fails the candidate/archipelago thresholds.0% rejects every candidate;100% accepts every candidate. The existing batch archipelago noise is still sampled at large sites for inspection; the new gate precedes its classification, not SIMD batch evaluation. Fine-grid island sampling and edge attenuation are unchanged. Newly rejected coarse regions count as exterior, so neighboring islands can also be reduced by the existing fade.

UI exposes0..100 percent with fractional values, default50; candidate grayscale view is0black..1white and hover reports value/acceptance. Candidate display on a continent is diagnostic only and never overrides continent priority. Existing experiment JSON3/workspace schema1 remain supported: missing optional arch_candidate_percent becomes50 in memory, with no automatic file rewrite. Saving/exporting writes the field. To preserve old distribution choose100. Native binary version3 remains the same size; coarse record slot5, previously reserved, now holds the exact candidate value. Browser and native save validator both accept arch-candidates display mode.

Manual diagnostics (build/release/arch-candidates-review), without CTest, browser automation or synthetic input:

- Old packaged editor vs new editor at100%: coarse geometry/raw noise/classification and all fine records/pixel records identical (excluding timing/new candidate slot).
- Installed seed/settings:0/25/50/75/100% yield0/1344/2705/4040/5469 archipelago parents. Every parent matched the stated predicate, continents remained identical, and both opposite pixel edges matched at every rate.
- Same request repeated identically except timing. Warp toggle leaves candidates identical. Changing master seed changes candidate values.
- Legacy workspace loads with50%. A37.5% value plus candidate grayscale view round-trips through native workspace save/load in an isolated folder. Negative/over100 rates rejected400.
- Both isolated servers exited via authenticated shutdown. Source JS syntax and Release build passed. Actual UI gestures were not exercised.

Release packaging completed; executables and changed editor assets match build outputs. Installed settings/worldgen/editor workspace/legacy draft bytes or absence were preserved. No commit/push performed.
