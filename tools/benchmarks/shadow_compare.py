"""Explicit before/after shadow diagnostic; not a build or CTest dependency."""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import time

from gpu_profile import SCENES, stats

SCENES = {k: SCENES[k] for k in ("coast", "land", "hills", "underwater")}
SCENES.update({"sunset": (7040, 5504, 220, -90, -22, 19),
               "night": (7040, 5504, 220, -90, -22, 0)})


def sha(data):
    return hashlib.sha256(data).hexdigest()


def summarize(directory):
    groups = collections.defaultdict(lambda: collections.defaultdict(list))
    report = {"runs": {}, "groups": {}, "maps": {}}
    for path in sorted(directory.glob("*.run.json")):
        meta = json.loads(path.read_text())
        if meta["returncode"]:
            raise RuntimeError(f"Failed run {path}")
        tag = path.name.removesuffix(".run.json")
        frames = collections.defaultdict(dict)
        dimensions = set()
        with (directory / (tag + ".csv")).open(newline="") as f:
            for row in csv.DictReader(f):
                if row["steady"] != "1":
                    continue
                frame = frames[int(row["frame"])]
                if row["stage"] in frame:
                    raise RuntimeError("Duplicate frame/stage")
                frame[row["stage"]] = float(row["ms"])
                dimensions.add((int(row["width"]), int(row["height"]), int(row["columns"])))
        if len(frames) != meta["samples"] or dimensions != {(1280, 900, 1793)}:
            raise RuntimeError(f"Incomplete/changed sample {tag}: {dimensions}")
        stages = collections.defaultdict(list)
        for frame in frames.values():
            if abs(frame["total"] - sum(v for k, v in frame.items() if k != "total")) > .0001:
                raise RuntimeError("GPU timeline sum mismatch")
            frame["shadow_total"] = sum(v for k, v in frame.items() if k.startswith("shadow_"))
            for stage, value in frame.items():
                stages[stage].append(value)
                groups[meta["scene"] + "/" + meta["variant"]][stage].append(value)
        log = (directory / (tag + ".log")).read_text()
        if "CHECK: Vulkan errors=0, warnings=0, UI issues=0, texture failures=0" not in log:
            raise RuntimeError(f"Runtime error log: {tag}")
        report["runs"][tag] = {"metadata": meta, "stages": {k: stats(v) for k, v in stages.items()}}
    report["groups"] = {key: {k: stats(v) for k, v in d.items()} for key, d in groups.items()}
    for scene in SCENES:
        before = directory / (scene + "-before-0-maps")
        after = directory / (scene + "-after-0-maps")
        if not before.exists() or not after.exists():
            continue
        files = {}
        for name in ["extent.txt", "all-depth.f32", "solid-depth.f32", "colour.rgba8", "shaft.rgba8"]:
            a, b = (before / name).read_bytes(), (after / name).read_bytes()
            files[name] = {"bytes": len(a), "before_sha256": sha(a), "after_sha256": sha(b), "identical": a == b}
        report["maps"][scene] = files
    (directory / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    for scene in SCENES:
        before = report["groups"].get(scene + "/before")
        after = report["groups"].get(scene + "/after")
        if before and after:
            print(scene, "shadow", round(before['shadow_total']['mean'], 4), "->", round(after['shadow_total']['mean'], 4),
                  "total", round(before['total']['mean'], 4), "->", round(after['total']['mean'], 4), flush=True)
    for scene, files in report["maps"].items():
        print("MAPS", scene, "identical:", all(f["identical"] for f in files.values()), flush=True)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--scenes", choices=SCENES, nargs="+", default=["coast", "land", "hills", "underwater"])
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--samples", type=int, default=600)
    args = parser.parse_args()
    directory = args.directory.resolve()
    if args.runtime:
        runtime = args.runtime.resolve()
        if "out" in (p.lower() for p in runtime.parts) or runtime == directory:
            raise RuntimeError("Use a separate disposable runtime")
        settings = (directory / "settings.json").read_bytes()
        worldgen = (directory / "worldgen.json").read_bytes()
        (runtime / "settings.json").write_bytes(settings)
        (runtime / "worldgen.json").write_bytes(worldgen)
        for repeat in range(args.repetitions):
            scenes = args.scenes if repeat % 2 == 0 else list(reversed(args.scenes))
            for scene in scenes:
                variants = ["before", "after"] if repeat % 2 == 0 else ["after", "before"]
                for variant in variants:
                    tag = f"{scene}-{variant}-{repeat}"
                    if (directory / (tag + ".run.json")).exists():
                        raise RuntimeError(f"Refusing to overwrite {tag}")
                    x, z, *view = SCENES[scene]
                    exe = runtime / (variant + ".exe")
                    cmd = [str(exe), "--profile-gpu", str(directory / (tag + ".csv")),
                           "--profile-origin", str(x), str(z), "--profile-view", *map(str, view),
                           "--profile-samples", str(args.samples), "--seconds", "120"]
                    if repeat == 0:
                        cmd += ["--capture", str(directory / (tag + ".png")),
                                "--capture-shadow-maps", str(directory / (tag + "-maps"))]
                    started = time.time()
                    print("RUN", tag, flush=True)
                    with (directory / (tag + ".log")).open("wb") as log:
                        result = subprocess.run(cmd, cwd=runtime, stdout=log, stderr=subprocess.STDOUT, timeout=150)
                    meta = {"scene": scene, "variant": variant, "repetition": repeat, "samples": args.samples,
                            "returncode": result.returncode, "command": cmd, "started_unix": started,
                            "wall_seconds": time.time() - started, "exe_sha256": sha(exe.read_bytes()),
                            "settings_sha256": sha(settings), "worldgen_sha256": sha(worldgen)}
                    (directory / (tag + ".run.json")).write_text(json.dumps(meta, indent=2) + "\n")
                    print("DONE", tag, result.returncode, round(meta['wall_seconds'], 2), "s", flush=True)
                    if result.returncode:
                        raise RuntimeError(f"Run failed: {tag}")
    summarize(directory)


if __name__ == "__main__":
    main()
