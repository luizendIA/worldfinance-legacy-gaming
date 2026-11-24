# Build and integrity report

24 November 2026. Windows 10 x64, MSVC 19.44.35229, CMake 4.4.4, SDK 10.0.26100.0, Release x64. The native lab uses Windows system libraries and embedded original HLSL. CMake performs no dependency downloads. The Python summary uses the standard library.

Two separate CMake build trees compiled the same source on the same host. Both Release executables had SHA-256:

```text
5ed932532d0379ca8d177136d309efd8c468f5b58c360a5ddc5357f45c9c25ce  legacy-lab.exe
```

The compiler and linker use `/Brepro`; incremental linking is disabled. This is a same-toolchain reproducibility check. Reproduction on a second machine and across SDK versions is pending. Different tools can produce different hashes. The public distribution is source. Use `Get-FileHash -Algorithm SHA256 build/Release/legacy-lab.exe` to hash your copy.

Local CTest passed both the invalid-argument check and WARP render/readback test. Four hardware runs completed with 256 valid GPU timestamp samples and four RGB image comparisons. CI builds in an ephemeral Windows runner, checks WARP and records its own SHA-256 in the job log. [Raw captures](../benchmarks/README.md) · [Workflow](../.github/workflows/ci.yml)
