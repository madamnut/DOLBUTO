"""Explicit GPU pass measurement runner/summarizer. Never run by builds or CTest.

Run only in a disposable runtime directory containing DOLBUTO.exe/assets/shaders/DLLs.
The supplied settings/worldgen snapshots are copied there; the user's package is untouched.
"""
import argparse
import collections
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import time

SCENES = {
    "coast": (7040, 5504, 220, -90, -22, 12),
    "land": (3456, 1152, 270, -90, -22, 12),
    "hills": (8064, 3456, 285, -90, -22, 12),
    "sky": (7040, 5504, 220, -90, 25, 12),
    "underwater": (7040, 5504, 190, -90, 15, 12),
}
VARIANTS = {
    "baseline": {},
    "no_shadows": {"graphics": {"shadows": False}},
    "no_ssr": {"water": {"ssr": False}},
    "no_clouds": {"graphics": {"clouds": False}},
    "no_bloom": {"graphics": {"bloom": False}},
}


def stats(values):
    v = sorted(values)
    return {"n": len(v), "mean": statistics.mean(v), "median": statistics.median(v),
            "p95": v[math.ceil(len(v) * .95) - 1], "min": v[0], "max": v[-1]}


def summarize(directory):
    groups = collections.defaultdict(lambda: collections.defaultdict(list))
    result = {"runs": {}, "groups": {}}
    for manifest in sorted(directory.glob("*.run.json")):
        meta = json.loads(manifest.read_text())
        if meta["returncode"] != 0:
            raise RuntimeError(f"Failed run: {manifest}")
        frames = collections.defaultdict(dict)
        dimensions = set()
        with (directory / meta["csv"]).open(newline="") as f:
            for r in csv.DictReader(f):
                if r["steady"] != "1":
                    continue
                # Initial measurement binary used the C++ volume name. GLSL's
                # actual half-resolution pass is clouds only; shafts live in composite.
                r["stage"] = {"volume_clouds_shafts": "cloud_raymarch",
                              "volume_underwater": "cloud_raymarch_underwater"}.get(r["stage"], r["stage"])
                key = int(r["frame"])
                if r["stage"] in frames[key]:
                    raise RuntimeError("Duplicate frame/stage")
                frames[key][r["stage"]] = float(r["ms"])
                dimensions.add((int(r["width"]), int(r["height"]), int(r["columns"])))
        if len(frames) != meta["samples"] or len(dimensions) != 1:
            raise RuntimeError(f"Incomplete/inconsistent sample: {manifest}")
        stages = sorted(set().union(*(f.keys() for f in frames.values())))
        values = collections.defaultdict(list)
        max_error = 0
        for frame in frames.values():
            error = abs(sum(v for k, v in frame.items() if k != "total") - frame["total"])
            max_error = max(max_error, error)
            if error > .0001 or any(not math.isfinite(v) or v < 0 for v in frame.values()):
                raise RuntimeError("Invalid/non-telescoping GPU timeline")
            for stage in stages:
                values[stage].append(frame.get(stage, 0.0))
        experiment = meta.get("experiment", manifest.name.split("-")[0])
        group = experiment + "/" + meta["scene"] + "/" + meta["variant"]
        for stage, v in values.items():
            groups[group][stage].extend(v)
        result["runs"][manifest.stem] = {
            "metadata": meta, "dimensions_columns": list(dimensions)[0],
            "max_timeline_rounding_error_ms": max_error,
            "stages": {k: stats(v) for k, v in values.items()},
        }
    result["groups"] = {k: {stage: stats(v) for stage, v in d.items()} for k, d in groups.items()}
    (directory / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    for name, d in result["groups"].items():
        print(name, "GPU mean", round(d["total"]["mean"], 4), "ms", flush=True)
        for stage in sorted((s for s in d if s != "total"), key=lambda s: -d[s]["mean"])[:5]:
            print(" ", stage, round(d[stage]["mean"], 4), flush=True)


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--runtime", type=Path)
    p.add_argument("--scenes", nargs="+", choices=SCENES, default=list(SCENES))
    p.add_argument("--variants", nargs="+", choices=VARIANTS, default=["baseline"])
    p.add_argument("--repetitions", type=int, default=3)
    p.add_argument("--samples", type=int, default=600)
    p.add_argument("--prefix", default="main")
    args = p.parse_args()
    directory = args.directory.resolve()
    if args.runtime:
        runtime = args.runtime.resolve()
        # Refuse to overwrite the user-facing package's settings.
        if "out" in (x.lower() for x in runtime.parts) or runtime == directory:
            raise RuntimeError("Use a separate disposable runtime, not out/DOLBUTO")
        source_settings = (directory / "settings.json").read_bytes()
        source_worldgen = (directory / "worldgen.json").read_bytes()
        (runtime / "worldgen.json").write_bytes(source_worldgen)
        for repetition in range(args.repetitions):
            # Alternate case order to reduce systematic warm-up/order effects.
            cases = [(s, v) for s in args.scenes for v in args.variants]
            if repetition % 2:
                cases.reverse()
            for scene, variant in cases:
                tag = f"{args.prefix}-{scene}-{variant}-{repetition}"
                if (directory / (tag + ".run.json")).exists():
                    raise RuntimeError(f"Refusing to overwrite completed run {tag}")
                settings = json.loads(source_settings)
                for section, changes in VARIANTS[variant].items():
                    settings[section].update(changes)
                settings_bytes = (json.dumps(settings, indent=2) + "\n").encode()
                (runtime / "settings.json").write_bytes(settings_bytes)
                (directory / (tag + ".settings.json")).write_bytes(settings_bytes)
                x, z, *view = SCENES[scene]
                command = [str(runtime / "DOLBUTO.exe"), "--profile-gpu", str(directory / (tag + ".csv")),
                           "--profile-origin", str(x), str(z), "--profile-view", *map(str, view),
                           "--profile-samples", str(args.samples), "--seconds", "120"]
                if repetition == 0:
                    command += ["--capture", str(directory / (tag + ".png"))]
                started = time.time()
                print("RUN", tag, flush=True)
                with (directory / (tag + ".log")).open("wb") as log:
                    completed = subprocess.run(command, cwd=runtime, stdout=log, stderr=subprocess.STDOUT,
                                               timeout=150)
                meta = {"experiment": args.prefix, "scene": scene, "variant": variant, "repetition": repetition,
                        "samples": args.samples, "command": command, "csv": tag + ".csv",
                        "started_unix": started, "wall_seconds": time.time() - started,
                        "returncode": completed.returncode,
                        "exe_sha256": hashlib.sha256((runtime / "DOLBUTO.exe").read_bytes()).hexdigest(),
                        "worldgen_sha256": hashlib.sha256(source_worldgen).hexdigest(),
                        "settings_sha256": hashlib.sha256(settings_bytes).hexdigest()}
                (directory / (tag + ".run.json")).write_text(json.dumps(meta, indent=2) + "\n")
                print("DONE", tag, completed.returncode, round(meta["wall_seconds"], 2), "s", flush=True)
                if completed.returncode:
                    raise RuntimeError(f"Failed run {tag}; inspect log")
        (runtime / "settings.json").write_bytes(source_settings)
    summarize(directory)


if __name__ == "__main__":
    main()
