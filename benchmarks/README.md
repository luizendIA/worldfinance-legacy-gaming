# Terrain v1 · measurement log

24 November 2026. Radeon RX 570 Series, driver 31.0.21924.61, Windows 10 Pro 19045, Xeon E5-2666 v3, 16 GB. One physical host. Four separate invocations of the same Release binary. The measured interval covers the terrain draw and composite draw on the GPU. Output is always 1280 × 720.

| Run | First profile | Native median / p95 (ms) | 640 × 360 + linear upscale median / p95 (ms) | Raw JSON |
| --- | --- | --- | --- | --- |
| 1 | Native | 4.353 / 5.483 | 1.510 / 2.025 | [Run 1](rx570-2026-11-24/run-1.json) |
| 2 | Scaled | 4.346 / 5.292 | 1.506 / 2.142 | [Run 2](rx570-2026-11-24/run-2.json) |
| 3 | Native | 4.321 / 5.098 | 1.495 / 2.188 | [Run 3](rx570-2026-11-24/run-3.json) |
| 4 | Scaled | 4.340 / 5.514 | 1.504 / 1.654 | [Run 4](rx570-2026-11-24/run-4.json) |

Each profile executes eight warm-ups and 32 individually synchronized, valid D3D11 timestamp intervals. A disjoint interval or timeout fails the run. The p95 is the nearest rank; the reported cross-run statistic is the median of the four per-run medians. These measurements exclude shader compilation, allocation, CPU frame time, readback and presentation. Power state, thermals and other desktop activity were uncontrolled. Treat the four repetitions as within-machine variation.

Both profiles use the same linear composite pass. CPU readback checks opacity and sufficient contrast. The RGB root mean squared error across all 1280 × 720 pixels was 7.612559 code values for every pair. The generated [native capture](../docs/terrain.png) is the first run's output encoded as lossless PNG. No game asset is involved.

Build using the [instructions](../README.md). Run `legacy-lab.exe` twice and `legacy-lab.exe --reverse` twice in alternating order, saving stdout to separate UTF-8 JSON files. Use `python tools/summarize.py <file1> <file2>` for per-run summaries. A software smoke run contains no timing samples. Report software runs separately. [Build and SHA-256](../reports/build.md)

## Hardware compatibility matrix

| Hardware/runtime | Workload | Status | Scope |
| --- | --- | --- | --- |
| RX 570 4 GB / Windows 10 | terrain-v1, native and scaled | Measured | 256 GPU timing samples and four image comparisons |
| Microsoft WARP / Windows | terrain-v1, small output | Smoke tested | Software rasterization, no hardware timing |
| RX 570 / D3D12 | feature queries | Measured | SM 6.5 and mesh tier 0 on this stack |
| RX 570 / Vulkan | terrain-v1 | Pending | No backend or feature capture yet |
| RX 570 / GTA VI | commercial title | Untested | No files supplied or executed |
| Other physical GPUs | terrain-v1 | Open for contributions | Supply driver, OS, rig details and raw JSON |
