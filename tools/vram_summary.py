"""Validate and summarize terrain-vram-v1 hardware captures. Python standard library."""
import argparse
import itertools
import json
from pathlib import Path
from statistics import median

MIB = 1024 ** 2
STAGES = ["before_allocation", "after_allocation", "render_pending", "after_render",
          "after_readback", "after_release", "release_poll", "after_device_release"]


def load_capture(path):
    run = json.loads(path.read_text(encoding="utf-8-sig"))
    rows = run["samples"]
    if run["workload"] != "terrain-vram-v1" or run["schema"] != 1:
        raise ValueError(f"Unexpected format in {path}")
    phases = [(key, list(group)) for key, group in itertools.groupby(rows, lambda s: s["stage"])]
    # A fast adapter may have finished before the first event poll.
    stages = [key for key, _ in phases]
    if stages not in (STAGES, [key for key in STAGES if key != "render_pending"]):
        raise ValueError(f"Unexpected phase order in {path}")
    for stage, samples in phases:
        if stage == "release_poll" and len(samples) != 10:
            raise ValueError(f"Incomplete cleanup trace in {path}")
        if stage not in ("release_poll", "render_pending") and len(samples) != 1:
            raise ValueError(f"Repeated phase in {path}")
    times = [s["elapsed_us"] for s in rows]
    if times != sorted(times) or times[0] < 0:
        raise ValueError(f"Invalid timestamps in {path}")
    for s in rows:
        for key in ("local_usage", "local_budget", "nonlocal_usage", "nonlocal_budget"):
            if type(s[key]) is not int or s[key] < 0:
                raise ValueError(f"Invalid byte count in {path}")
    return run


def main(paths):
    runs = [load_capture(path) for path in paths]
    key = lambda r: (r["adapter"], r["scene_width"], r["scene_height"], r["output_width"], r["output_height"])
    for group, measurements in itertools.groupby(sorted(runs, key=key), key):
        captures = list(measurements)
        print(f"{group} ({len(captures)} captures)")
        for stage in STAGES:
            # Each run contributes one observed maximum per segment per phase.
            groups = [[s for s in r["samples"] if s["stage"] == stage] for r in captures]
            present = [samples for samples in groups if samples]
            if present:
                values = [median(max(s[k] for s in samples) for samples in present) / MIB
                          for k in ("local_usage", "nonlocal_usage")]
                print(f"  {stage:23s} local {values[0]:8.4f} MiB  non-local {values[1]:8.4f} MiB"
                      f"  n={len(present)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="+", type=Path, help="one or more raw JSON files or directories")
    args = parser.parse_args()
    files = [file for path in args.captures for file in (sorted(path.glob("*.json")) if path.is_dir() else [path])]
    if not files:
        parser.error("No JSON captures found")
    main(files)
