# Seeded continent growth (editor only)

Current correction: the seed cap and continent deletion described in the original implementation below were removed by the final section.

User approved growth from land seeds, merge limit5 original seeds, whole-continent deletion of the side with fewer seeds when limit exceeded, deterministic ties and regrowth into deleted space. Replaces prior CA birth/survival/random flip. Game flat stone, climate, master seed and periodic geometry/warp are unchanged. No new noise field or permanent sea mask is added.

## State / phases

Each cached frame has owner[N] uint32 (0 sea, otherwise representative original seed cellID+1), seed_counts[N+1] uint8 (only live representatives nonzero1..5), cumulative deleted continent events and original seed counts. Distinct owner roots carry disjoint original seed ancestry, so merging sums counts without double counting; representative is the original seed with lowest seeded priority. Individual ancestry lists are unnecessary for this rule. Deleted original seeds never respawn even when their old cell is occupied again by another continent.

New domains land_seeds=0x4c534545, land_growth=0x4c47524f; existing geometry/climate domains unchanged. Priority[i]=mix_seed(derive_seed(master,land_seeds) XOR i). Sort by(priority,ID), greedily place seeds only if no existing seed shares an edge (including seams), stop at requested count. High requests on small grids may yield fewer seeds; UI reports actual/requested. Default32, range1..4096.

Each step:
1. Copy prior owners. Only prior sea cells adjacent to prior land can grow. Draw=(mix_seed(derive_seed(master,land_growth) XOR mix_seed(destination_step) XOR cellID)>>8)/2^24. Grow iff draw<growth_percent/100. Defaults50%,12 steps, ranges0..100%,0..100 steps. Newly grown cells cannot drive another layer in the same step.
2. If multiple donor continents touch a candidate, choose lowest seeded representative priority. Existing land is preserved at this stage.
3. Enumerate contacts along all graph edges using proposed owners, including new-new and new-old contacts and periodic seams. Normalize pairs by representative priority, sort lexicographically by the two seeded priorities and IDs, deduplicate. This explicit sequential resolution is independent of graph traversal order; it is not an order-free simultaneous multiway merge.
4. Union-find resolves current representatives. Skip same/dead roots. Counts sum<=5: merge, keep lowest priority representative. Sum>5: delete smaller seed-count root; ties delete worse priority. Deleting a root removes all its old and proposed cells, including any groups merged into it earlier this step. Update counts immediately for later contacts. Do not attach deleted roots to winners.
5. Canonicalize all owners and clear all dead-root cells. Regrowth is allowed next step only; no same-step refill or seed resurrection.

Every surviving connected continent contains at most5 original seeds. Limit is not area: repeated growth can fill the whole world with a surviving continent. Existing unwarped Voronoi graph is reused; visual warping does not change contacts. Cached frames allow backward steps and deterministic replay. At65536 cells/101 frames owner+count payload is about31.6MiB, plus vectors/geometry/temporary contacts. Cache keys are geometry(count,jitter,master) plus requested seed_count,growth_percent. Viewport and warp changes reuse growth.

## Persistence and presentation

Experiment version5 fields growth_seed_count/growth_percent/growth_steps replace ca_* fields. Old versions1..4 retain seed/site/warp/range, initialize growth32/50/12 in memory; no implicit user file writes. Old valid display views remain supported on native normalization; browser JSON import resets old display to land. Workspace schema1/climate schema12 remain unchanged. Explicit save writes5, excludes retired fields and still stores master seed only once.

Binary5:28 float header,10 float site records,2 float pixels,2 float undirected edge records. Header first24 positions retain v4 meanings except version5,stride10,offset28; slot9 now growth time. Added24 actual initial seed count,25 surviving seeds,26 cumulative deleted continent events,27 cumulative deleted seeds. Site first8 retain positions(x,z,priority0..1,initial occupancy,current occupancy,degree,area,land_neighbor_ratio); new8 ownerID (0 sea),9 original seed count. Priority visualization rounds to24 bits but decisions use full32-bit hashes. Pixel/edge format unchanged.

Editor adds continent colors and seed-count shades1..5, hover rootID/count, current continent count and cumulative deletion statistics. Global statistics refer to unwarped polygons/graph and selected final step, even on initial-seed view. Current raster land percentage is separate. Retains map interaction, save/import/export and shared master seed.

## Verification

Release build passed without compiler warnings; JS syntax checked. Isolated native HTTP with copies of user files; no automated test suite, CTest, computer use, browser automation or synthetic input.

Independent Python reference used explicit original-seed sets and whole-array owner replacement instead of union-find. On1024 sites, seed105180838,64 seeds,growth65%, native and reference initial selection and all20 steps matched every owner, seed count and cumulative deletion count. Every continent remained connected; max ancestry5; alive+deleted seeds=initial. Observed merge2+3→5 and deletion5+1,5+2,5+3,5+4, tie5+5. Reference recorded892 regrown previously deleted cells across steps. Atstep12 one5-seed continent covered59.98%; step20 it covered100%, consistent with unrestricted area.

Backward/forward cached replay and geometry rebuild reproduced results. Growth0 retained initial owners through100 steps. One seed at100% grew exactly one graph ring after one step. Oversized4096 request on16 cells placed3 independent seeds and reported3. Max65536 cells/4096 requested seeds/100 steps completed one observed native request in202.51ms (geometry104.43ms,growth93.86ms), never exceeded5 ancestry and conserved original-seed accounting. These single samples are not stable benchmark guarantees.

Old workspace migrated to5 without changing file bytes. Isolated explicit save/read roundtrip returned77 seeds/34.5%/17 steps. Invalid seed_count0/4097,growth101,steps1.5 returned400. Actual browser interaction is unverified per user policy. Installed settings/worldgen/workspace are preserved during packaging.

## Removal of deletion and merge limit

User authorized removing land deletion. All touching continents now merge without any seed-count limit. Existing occupied cells never become sea. Removed death counters, loser selection, contact sorting/deduplication (unconditional union gives the same stable minimum-priority root independent of edge order), and dead-root clearing. Seed counts now uint32 to support up to4096 original seeds without overflow. Seed placement, probability draws and synchronous one-ring proposal behavior are unchanged; no new growth noise was introduced.

UI no longer shows deletion counters. Seed-count colors use logarithmic interpolation from1 to actual initial seed count, supporting counts above5/255. Binary5 header26/27 remain reserved zeros for layout compatibility; header25 equals initial seed count. Experiment5/user settings schema unchanged. History payload now approximately50.5MiB at65536 cells/101 frames (owner+seed_count arrays).

Release build and JS syntax passed. Isolated HTTP manual inspection over20 steps with4096 cells/512 seeds/65% growth showed every previous land cell persisted, per-continent counts equaled original seed cells within that continent, and a512-seed continent merged correctly. Seed accounting remained512, reserved fields0, history replay exact and0% growth unchanged through100 steps. No automated tests/browser automation/computer use. Package preserves installed user configuration bytes.
