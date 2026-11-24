# Indexed meshlet fallback on a Radeon RX 570

WordFinance · 24 November 2026 · Physical hardware · Synthetic fixture indexed-v1

A small meshlet can be lowered on the CPU to a conventional triangle list and drawn on this Mesh Shader Tier 0 adapter. All four physical runs matched the analytic CPU reference for coverage and primitive identity at every pixel. This experiment implements one known geometry-emission pattern. General mesh shader translation, dynamic amplification, shader-side culling, performance against a mesh-capable GPU and commercial game compatibility remain open.

| GPU readback, palette applied | Independent CPU reference, same palette |
| --- | --- |
| ![RX 570 primitive IDs](rx570.png) | ![CPU primitive IDs](reference.png) |

Amber is the near triangle. Green and blue identify the two triangles that form the square. Dark pixels are the clear value. These PNGs encode the ID comparison. The depth comparison uses the separate D32_FLOAT attachment.

## Fixture and pass criteria

Ten vertices form one synthetic meshlet. A non-identity vertex table translates twelve local indices into four global index triples for one D3D11 DrawIndexed call. The vertex shader transforms screen coordinates to clip space with w=1. The 256 × 256 single-sample draw uses the D3D11 default clockwise front faces, back-face culling, LESS depth test and depth writes. The near triangle is submitted before the farther square. A deliberately reversed triangle reaches beyond the square and should be culled.

The Python oracle classifies pixel centres analytically. It tests the near triangle with the top-left rule, classifies the square by its diagonal, and evaluates the visible plane's depth from its own equation. It does not read the GPU vertices, indices or shaders. Pass criteria are zero coverage differences, zero primitive-ID differences and absolute depth error at most 0.000001 at every pixel, including background. Invalid sizes and nonfinite values fail.

## Physical result

Radeon RX 570 Series 4 GB, driver 31.0.21924.61, Xeon E5-2666 v3, 16 GB RAM, Windows 10 Pro 19045. Each process independently reported Mesh Shader Tier 0 and Shader Model 6.5. In each capture 36,864 of 65,536 pixels were covered. Coverage and primitive-ID mismatches were both **0**. Maximum absolute depth error was **5.96046448 × 10⁻⁸**. The ID and depth attachments had identical respective hashes across the four processes. WARP independently passed the same comparison in smoke mode.

| Run | Median GPU ms per batch | p95 ms per batch | Raw batch times and comparison |
| --- | ---: | ---: | --- |
| 1 | 1.33856 | 2.07312 | [JSON](run-1.json) |
| 2 | 1.33784 | 1.68192 | [JSON](run-2.json) |
| 3 | 1.33864 | 1.34608 | [JSON](run-3.json) |
| 4 | 1.33728 | 1.34464 | [JSON](run-4.json) |

After eight warm-up draws, each process timed 32 batches of 32 repetitions. Each repetition clears both attachments and submits one four-triangle draw. The median of the four run medians was **1.33820 ms per batch**, or **41.82 µs per clear-and-draw repetition**. p95 uses nearest rank. Timestamp queries exclude CPU remapping, resource creation, shader compilation, readback and the CPU comparison. GPU clocks and background load were uncontrolled. This is a microbenchmark on one graphics stack; no mesh-shader baseline was timed.

## Reproduce

Build the x64 Release target using the [project instructions](../../README.md), then use a new capture directory per run.

```powershell
.\build\Release\legacy-lab.exe --indexed capture-1 | Out-File -Encoding utf8 timings.json
python tools/indexed_reference.py capture-1
```

To verify the published buffers without a GPU, extract [attachments.zip](attachments.zip) to an empty directory and run the same Python comparison on it. The archive contains the first physical capture in top-down row order, one unsigned byte per pixel for IDs and one little-endian float32 per pixel for depth. SHA-256 digests for the uncompressed attachments appear in every JSON. The verifier exits with an error on any failed criterion. Source and embedded shaders are original. [GPU implementation](../../src/indexed.cpp) · [CPU oracle](../../tools/indexed_reference.py)

Two local Release build directories using MSVC 19.44.35229 and SDK 10.0.26100.0 produced the same executable SHA-256: `7c449f3779513e8ca360cc23c9892c5c5c7e9da659f14c48c25a9c2e157edd24`. This hash identifies the version used for the physical captures.
