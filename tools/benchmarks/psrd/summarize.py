"""Explicit analysis of two PSRD benchmark runs; requires numpy and matplotlib."""
import csv
import shutil
import statistics as stats
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

root = Path(__file__).resolve().parents[3]
dest = root / "docs/benchmarks"
dest.mkdir(exist_ok=True)
prefix = dest / "psrd-2026-09-24"
rows = []
for run in (1, 2):
    folder = root / f"build/release/psrd-run{run}"
    with (folder / "timings.csv").open() as f:
        rows += [dict(run=run, **r) for r in csv.DictReader(f)]
    for octave in (1, 6):
        shutil.copyfile(folder / f"inspection-o{octave}.txt",
                        f"{prefix}-run{run}-inspection-o{octave}.txt")
with open(f"{prefix}-raw.csv", "w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
groups = defaultdict(list)
for r in rows:
    groups[int(r["samples"]), int(r["octaves"]), r["method"]].append(r)
summary = []
table = ["|샘플 수|옥타브|방법|중앙값 ns|평균 ns|표준편차 ns|Perlin 대비 시간|",
         "|---:|---:|---|---:|---:|---:|---:|"]
for (count, octaves, method), entries in sorted(groups.items()):
    values = [float(r["ns_per_sample"]) for r in entries]
    baseline = stats.median(float(r["ns_per_sample"]) for r in groups[count, octaves, "periodic_perlin2d"])
    run_medians = [stats.median(float(r["ns_per_sample"]) for r in entries if r["run"] == run) for run in (1, 2)]
    s = dict(samples=count, octaves=octaves, method=method, observations=len(values),
             median_ns=stats.median(values), mean_ns=stats.mean(values), sd_ns=stats.stdev(values),
             min_ns=min(values), max_ns=max(values), ratio=stats.median(values)/baseline,
             run1_median_ns=run_medians[0], run2_median_ns=run_medians[1])
    summary.append(s)
    name = "주기적 Perlin" if method == "periodic_perlin2d" else "PSRD"
    table.append(f"|{count:,}|{octaves}|{name}|{s['median_ns']:.3f}|{s['mean_ns']:.3f}|{s['sd_ns']:.3f}|{s['ratio']:.2f}×|")
with open(f"{prefix}-summary.csv", "w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=summary[0].keys())
    writer.writeheader()
    writer.writerows(summary)
Path(f"{prefix}-table.md").write_text("\n".join(table) + "\n", encoding="utf-8")
print("\n".join(table))

folder = root / "build/release/psrd-run1"
fig, axes = plt.subplots(2, 2, figsize=(11, 11), layout="constrained")
contours, caxes = plt.subplots(2, 2, figsize=(11, 11), layout="constrained")
for row, octaves in enumerate((1, 6)):
    for col, method in enumerate(("perlin", "psrd")):
        data = np.fromfile(folder / f"{method}-o{octaves}.f32", dtype="<f4").reshape(1024, 1024)
        assert np.isfinite(data).all()
        title = f"{'Periodic Perlin' if col == 0 else 'PSRD'} | {octaves} octave{'s' if octaves > 1 else ''}"
        extent = (8192, 16384, 16384, 24576)
        im = axes[row, col].imshow(data, cmap="gray", vmin=-1, vmax=1, origin="lower",
                                   extent=extent, interpolation="nearest")
        caxes[row, col].contour(data, levels=[-.4, -.2, 0, .2, .4], colors="#273a50",
                               linewidths=.55, origin="lower", extent=extent)
        for ax in (axes[row, col], caxes[row, col]):
            ax.set(title=title, xlabel="X (blocks)", ylabel="Z (blocks)", aspect="equal")
            ax.tick_params(labelsize=8)
        print(f"{method} o{octaves} range={data.min():.5f}..{data.max():.5f} std={data.std():.5f}")
fig.colorbar(im, ax=axes, shrink=.6, label="Raw value (fixed scale, no contrast normalization)")
fig.suptitle("Same coordinates / seed 1337 / base spacing 1024 / gain 0.5", fontsize=13)
contours.suptitle("Same raw-value contours: -0.4, -0.2, 0, 0.2, 0.4", fontsize=13)
fig.savefig(f"{prefix}-comparison.png", dpi=140)
contours.savefig(f"{prefix}-contours.png", dpi=140)
