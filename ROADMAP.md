# Research roadmap

Status at 24 November 2026. Milestones describe experiments and their exit criteria. An unsupported feature or a negative result is a valid outcome.

| Phase | Status | Exit criterion / next task |
| --- | --- | --- |
| 1 · Infrastructure and feasibility | Initial review published | Maintain the pinned upstream review; reopen the fork decision only with evidence of redistribution rights. |
| 2 · RX 570 constraints | First hardware capture complete | Add a Vulkan capability dump and repeat the D3D12 queries on another supported driver. |
| 3 · Independent graphics tests | First fixture complete | D3D11 terrain, readback and software smoke are available. The [indexed fixture](benchmarks/indexed-rx570/README.md) passed its CPU comparison. Next vary transforms, clipping and meshlet size. |
| 4 · Memory and rendering alternatives | Fixed-scale experiment measured | Measure local allocation usage under a bounded budget, then compare indexed and compute-generated geometry. Add dynamic resolution after establishing a fixed baseline. |
| 5 · Authorized validation | Started on one physical GPU | Compare images and raw timings across workload revisions. Test cache cold/warm behavior, asynchronous loading and CPU submission independently. |
| 6 · Publish and reproduce | First raw dataset published | Request independent repeats with driver, OS, workload revision, capture order and output quality. |
| 7 · Other legacy GPUs | Open | Start with a second Polaris board; add other architectures as contributors provide reproducible hardware evidence. |

## Open limitations

The synthetic scene has two fixed scales and one static camera. It does not measure game FPS, peak working set, presentation latency or streaming. Vulkan translation, runtime scaling, shader caching, mesh-stage replacement and GTA VI compatibility remain unvalidated. One driver and one physical machine have been tested. The upstream BCPack rights and any project-specific donation destination remain unresolved. [Hardware matrix](benchmarks/README.md) · [Active issues](https://github.com/luizendIA/worldfinance-legacy-gaming/issues)
