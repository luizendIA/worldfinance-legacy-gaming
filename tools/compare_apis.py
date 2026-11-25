"""Compare two native-720p hardware captures. File input only. Requires NumPy."""
import argparse
import hashlib
import json
import math
from pathlib import Path
from statistics import mean, median


def pair(direct3d, vulkan):
    import numpy as np
    inputs = [json.loads((d / "times.json").read_text(encoding="utf-8-sig")) for d in (direct3d, vulkan)]
    a, b = inputs
    if a["api"] != "D3D11" or b["api"] != "Vulkan" or a["adapter"] != b["adapter"]:
        raise ValueError("Check API labels and ensure both processes used the same adapter")
    if any(len(run["frames"]) != 72 for run in inputs):
        raise ValueError("Expected 72 camera positions")
    frames = []
    for i in range(72):
        buffers = [(d / f"{i}.rgba").read_bytes() for d in (direct3d, vulkan)]
        images = [np.frombuffer(buf, dtype=np.uint8).reshape(720, 1280, 4) for buf in buffers]
        if any(np.any(im[:, :, 3] != 255) or np.ptp(im[:, :, 1]) < 24 for im in images):
            raise ValueError(f"Invalid image in frame {i}")
        diff = np.abs(images[0][:, :, :3].astype(np.int16) - images[1][:, :, :3])
        row = {"frame": i, "rgb_squared_error": int(np.sum(np.square(diff, dtype=np.int32), dtype=np.int64)),
               "max_channel_error": int(np.max(diff)), "pixels_over_1": int(np.count_nonzero(diff.max(axis=2) > 1))}
        for run, key, buf in zip(inputs, ("d3d11", "vulkan"), buffers):
            f = run["frames"][i]
            if f["frame"] != i or not all(math.isfinite(f[k]) and f[k] > 0 for k in ("gpu_ms", "wall_ms")):
                raise ValueError(f"Invalid timing sample at frame {i}")
            row[key] = {k: f[k] for k in ("gpu_ms", "wall_ms")}
            row[key]["rgba_sha256"] = hashlib.sha256(buf).hexdigest()
        frames.append(row)
    return {"workload": "api-terrain-720p-v1", "adapter": a["adapter"], "frames": frames}


def summary(runs):
    rows = [f for run in runs for f in run["frames"]]
    if not runs or any(run["workload"] != "api-terrain-720p-v1" or len(run["frames"]) != 72 for run in runs):
        raise ValueError("Expected complete api-terrain-720p-v1 traces")
    for run in runs:
        for i, f in enumerate(run["frames"]):
            if f["frame"] != i or not (0 <= f["pixels_over_1"] <= 1280*720 and 0 <= f["max_channel_error"] <= 255):
                raise ValueError("Invalid frame metrics")
            if not 0 <= f["rgb_squared_error"] <= 1280*720*3*255**2:
                raise ValueError("Invalid squared error")
            for key in ("d3d11", "vulkan"):
                if not all(math.isfinite(f[key][k]) and f[key][k] > 0 for k in ("gpu_ms", "wall_ms")):
                    raise ValueError("Invalid timing in trace")
    output = {}
    for key in ("d3d11", "vulkan"):
        percentiles = [sorted(f[key]["gpu_ms"] for f in r["frames"])[math.ceil(.95*len(r["frames"]))-1] for r in runs]
        output[key] = {"mean_gpu_ms": mean(f[key]["gpu_ms"] for f in rows), "p95_gpu_ms": median(percentiles),
                       "mean_wall_ms": mean(f[key]["wall_ms"] for f in rows)}
    pixels = len(rows) * 1280 * 720
    output["rgb_rmse"] = math.sqrt(sum(f["rgb_squared_error"] for f in rows)/(3*pixels))
    output["max_channel_error"] = max(f["max_channel_error"] for f in rows)
    output["pixels_over_1_pct"] = 100 * sum(f["pixels_over_1"] for f in rows)/pixels
    output["change_mean_gpu_pct"] = 100*(output["vulkan"]["mean_gpu_ms"]/output["d3d11"]["mean_gpu_ms"]-1)
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--pair", type=Path, nargs=2, metavar=("D3D11_DIR","VULKAN_DIR"))
    modes.add_argument("--summary", type=Path, metavar="RAW_JSON_DIR")
    args = parser.parse_args()
    result = pair(*args.pair) if args.pair else summary(
        [json.loads(p.read_text(encoding="utf-8-sig")) for p in sorted(args.summary.glob("run-*.json"))])
    print(json.dumps(result, indent=2))
