"""Summarize native-lab JSON captures using the Python standard library."""
import argparse
import json
import math
import statistics
from pathlib import Path


def summary(run):
    if run["schema"] != 1 or run["workload"] != "terrain-v1":
        raise ValueError("Unrecognized capture format")
    data = {"adapter": run["adapter"], "software": run["software"]}
    for key in ("native", "scaled"):
        samples = sorted(run[key]["gpu_ms"])
        if not samples or not all(math.isfinite(x) and x > 0 for x in samples):
            raise ValueError("Expected positive GPU timestamps (smoke runs have none)")
        data[key] = {"count": len(samples), "median_ms": statistics.median(samples),
                     "p95_ms": samples[math.ceil(.95 * len(samples)) - 1]}
    return data


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", nargs="+", type=Path)
    for path in parser.parse_args().capture:
        print(json.dumps(summary(json.loads(path.read_text(encoding="utf-8-sig")))))
