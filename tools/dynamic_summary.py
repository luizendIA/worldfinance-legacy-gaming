"""Summarize paired dynamic-terrain-v1 captures (Python standard library)."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
from statistics import median, mean


def validate(run):
    frames = run["frames"]
    budget = run["target_gpu_ms"]
    if run["workload"] != "dynamic-terrain-v1" or len(frames) != 72 or budget not in (5, 8):
        raise ValueError("Unexpected workload, length or target")
    scale, high, low = 8, 0, 0
    for i, f in enumerate(frames):
        if (f["frame"], f["width"], f["height"], f["terrain_passes"]) != (
                i, 160 * scale, 90 * scale, 3 if 24 <= i < 48 else 1):
            raise ValueError(f"Unexpected controller state at frame {i}")
        metrics = [f[k] for k in ("native_gpu_ms", "dynamic_gpu_ms", "native_wall_ms", "dynamic_wall_ms")]
        if not all(math.isfinite(v) and v > 0 for v in metrics):
            raise ValueError("Invalid timing")
        error = f["rgb_rmse"]
        if not 0 <= error <= 255 or (scale == 8 and error != 0):
            raise ValueError("Invalid pixel comparison")
        gpu = f["dynamic_gpu_ms"]
        high = high + 1 if gpu > budget else 0
        low = low + 1 if gpu * ((scale + 1) / scale) ** 2 < budget * .9 else 0
        if high >= 2 and scale > 4:
            scale -= 1
        elif low >= 4 and scale < 8:
            scale += 1
        else:
            continue
        high = low = 0


def summarize(runs):
    if not runs:
        raise ValueError("Missing paired captures")
    for r in runs:
        validate(r)
    budgets = {r["target_gpu_ms"] for r in runs}
    if len(budgets) != 1:
        raise ValueError("Summarize one budget at a time")
    budget = budgets.pop()
    frames = [f for r in runs for f in r["frames"]]
    result = {"target_ms": budget, "pairs": len(runs), "paired_frames": len(frames)}
    for mode in ("native", "dynamic"):
        key = mode + "_gpu_ms"
        series = [sorted(f[key] for f in r["frames"]) for r in runs]
        result[mode] = {
            "mean_gpu_ms": mean(f[key] for f in frames),
            "median_gpu_ms": median(median(a) for a in series),
            "p95_gpu_ms": median(a[math.ceil(.95 * len(a)) - 1] for a in series),
            "mean_wall_ms": mean(f[mode + "_wall_ms"] for f in frames),
            "within_budget": sum(f[key] <= budget for f in frames),
        }
    result["change_mean_gpu_pct"] = 100 * (result["dynamic"]["mean_gpu_ms"] / result["native"]["mean_gpu_ms"] - 1)
    result["rgb_rmse"] = math.sqrt(mean(f["rgb_rmse"] ** 2 for f in frames))
    result["worst_frame_rmse"] = max(f["rgb_rmse"] for f in frames)
    result["width_counts"] = dict(sorted(Counter(f["width"] for f in frames).items()))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path, help="Directory with raw paired captures for one budget")
    args = parser.parse_args()
    runs = [json.loads(f.read_text(encoding="utf-8-sig")) for f in sorted(args.captures.glob("*.json"))]
    print(json.dumps(summarize(runs), indent=2))
