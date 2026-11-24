# VIctory source, security and redistribution review

24 November 2026 · Initial static review · Decision: independent source repository.

## Identity and chain of custody

The public GitHub API identified Marvisak/VIctory as a public repository with forking enabled. Git over HTTPS and the GitHub commit API agreed on `ffe310eb2098e52169f4c44db5ffdabf43a72622`. The local bare clone passed `git fsck --full`. Its main history contained 213 commits and the pinned tree contained 154 files. GitHub reported the tip as unsigned. These checks establish the repository and bytes inspected. The author's real-world identity and the authenticity of performance claims remain unverified. [Pinned tree](https://github.com/Marvisak/VIctory/tree/ffe310eb2098e52169f4c44db5ffdabf43a72622) · [Commit record](https://api.github.com/repos/Marvisak/VIctory/commits/ffe310eb2098e52169f4c44db5ffdabf43a72622)

## Redistribution decision

The workspace manifest and README explicitly specify **GPL-3.0-or-later**. The file LICENSE contains the GNU GPL version 3; its SHA-256 is `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`. GitHub's general license label does not resolve per-file exceptions. [Manifest](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/Cargo.toml) · [Full license](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/LICENSE)

| Material | Evidence and disposition |
| --- | --- |
| BCPack tables | The upstream policy expressly excludes `crates/victory-formats/src/bcpack-tables.bin` from its license. The 77,982-byte blob has SHA-256 `1f1d4760419202bb5672f0c428ce51e456b5d08fbaec8f371d043fd94e01c36b`. It entered history at `2849477415a46f5a079c85cf3e64a3dff32f713c`. We found no separate redistribution grant. |
| Texture layout | The source credits AMD addrlib via Mesa and includes an MIT notice. Anyone importing this work must check the precise source revision and preserve the notice. |
| XMA code and tables | Upstream identifies FFmpeg and LGPL-2.1-or-later material, used under GPL terms. An import would require a per-file origin and notices review. |
| Third-party Rust libraries | Cargo.lock identifies registry packages and checksums. Their individual licenses and build dependencies would need separate verification before any bundled redistribution. |
| World Finance laboratory | Original implementation and synthetic imagery, distributed under GPL-3.0-or-later. The GNU license text is reproduced verbatim under its copying permission. |

A native GitHub fork would publish the excluded blob. Deleting a file in a later commit would leave it accessible in history. The public lab therefore starts with its own root commit. The original history, authors and notices are available at the pinned upstream link. No upstream implementation, data table, game file or Git ancestor was imported. Reconsider a native fork only when redistribution is documented for **every** reachable component or GitHub offers a clean, rights-cleared upstream history. [Asset policy](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/docs/asset-policy.md) · [AMD attribution](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/crates/victory-formats/src/texture.rs) · [FFmpeg attribution](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/crates/victory-formats/src/xma/tables.rs)

### What GPLv3 requires when applicable

- §§0–2 define the covered work, corresponding source, permissions to run and modify, and when conveying triggers obligations. Internal experimentation and giving others copies have different obligations.
- §§3–5 address anti-circumvention rights for covered works, keeping licenses, copyright and warranty notices; modified source needs prominent change and date notices and licensing of the covered whole, subject to the aggregation rules.
- §6 specifies ways to provide corresponding source with object code, including the appropriate installation information where User Products qualify. Build and installation scripts can be part of corresponding source.
- §§7–10 govern additional terms, termination and reinstatement, acceptance through propagation/modification and automatic downstream grants without further restrictions.
- §§11–14 address contributor patent grants, conflicting obligations, combination with AGPLv3 and selection of later license versions where authorized. A third-party owner's rights do not pass through the project's license.
- §§15–17 cover warranty, liability and local interpretation. A license to compatibility research code supplies no rights to a commercial title, keys, SDK, trademarks or excluded data.

[GNU GPLv3, complete terms](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/LICENSE)

## Security findings

| Priority | Finding | Handling |
| --- | --- | --- |
| High | The native loader executes supplied PE code with the host process's authority. File wrappers map virtual drives and pass other paths to host APIs. | Static inspection only. Any future dynamic upstream analysis belongs in a disposable VM, ordinary account, blocked egress, no personal mounts or credentials, with Defender active. |
| High | The upstream README reports driver resets on its own GPU when entering the world. | No upstream GPU workload was run. Use the bounded original fixture for physical-device measurements. |
| Medium | Winsock and WinHTTP wrappers are intended to restrict imported network paths to loopback. They do not establish process or OS containment. | No native-title execution or independent packet capture was performed. Build dependency retrieval is a separate network path. |
| Medium | Caches, save data, crash logs and optional frame, memory and audio captures can write outside the source tree. Diagnostic helpers can launch child processes. | Keep captures out of submissions. Use dedicated scratch storage for future isolated work. |
| Medium | The DXIL validator can load a DLL via environment-specified or searched paths. The hook installer changes local Git config. The fixture build script supplies linker directives. | Inspected as text; no hooks installed, dependencies built, validator loaded or guest executable opened. |
| Supply chain | The lockfile contains 90 registry entries with checksums. OSV batch lookup of the exact crates.io names and versions returned zero advisory matches on 2026-11-24. Upstream CI uses version tags for checkout and a third-party Rust cache; it installs the pinned Rust toolchain. | This is a dated database result. Transitive source behavior, registries, macros, build hooks and historical commits were not exhaustively audited. |

[Loader design](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/docs/architecture.md) · [File handling](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/crates/victory-hle/src/modules/files.rs) · [Network handling](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/crates/victory-hle/src/modules/net.rs) · [Validator](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/crates/victory-shader/src/emit/validator.rs) · [Build workflow](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/.github/workflows/ci.yml) · [OSV API](https://google.github.io/osv.dev/api/)

## Laboratory boundary

The new renderer accepts command-line switches, compiles embedded HLSL with the Windows SDK runtime, creates local Direct3D devices and writes JSON to stdout. An explicit `--capture PATH` writes a PPM to that path. It does not enumerate personal files, access wallets, fetch assets or require elevated privileges. Source, CMake, CI and the pages are available for review. No upstream code was executed on the reference PC. Static inspection cannot certify the absence of malicious behavior in the reference project. [Build report](build.md) · [Reporting concerns](../SECURITY.md)
