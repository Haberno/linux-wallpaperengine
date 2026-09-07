---
type: Refactor Backlog
title: Candidate Refactors
description: User-supplied cleanup backlog for linux-wallpaperengine, captured as OKF knowledge before implementation.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, refactor, cleanup, backlog]
timestamp: 2026-07-26T00:00:00-04:00
---

# Scope

> **Consolidated checklist (2026-09-06):** [Project checklist](../../Organized%20all.md). It reconciles these notes with the current
> source and separates implementation, local work in progress, and pending
> verification. Dated measurements below remain historical snapshots.

These are candidate refactors for the current linux-wallpaperengine fork. They are notes, not completed changes. Inspect the dirty worktree before implementing anything.

# Candidates

| Kind | Item | Replacement | Target |
|---|---|---|---|
| native | Two vendored FFT copies are in-tree; only vanilla `kissfft` is linked. | Drop the unlinked `kissfft-WallpaperEngine` submodule (and the also-unreferenced `source-parsers`), or move them out of the build tree if kept as reference. | `src/External/` |
| stdlib | `Maths::lerp` duplicates `glm::mix`, which is already common in the repo. | Replace fade interpolation with `glm::mix(sv, ev, glm::clamp(...))`; delete `Maths.cpp/h` if random helpers move to their single caller, `CParticle`. | `src/WallpaperEngine/Maths.cpp` |
| shrink | Two identical `ObjectData{...}` initializer blocks exist in `ObjectParser` try/catch; only id/name differ. | Resolve id/name first, then build once. | `src/WallpaperEngine/Data/Parsers/ObjectParser.cpp` |
| yagni | ~~`ShaderUnit` constructs 26 `std::regex` values per shader unit per pass.~~ **Corrected 2026-07-26**: the fixed patterns are already `static` (48 statics vs 9 name-interpolated locals). | Only the trivial fixed-literal one-liners are left to convert to plain scans, and it is not where the time goes — see [[Load Performance]]. | `src/WallpaperEngine/Render/Shaders/ShaderUnit.cpp` |
| delete | `getMousePositionLast` plumbing is exposed by `CScene`/`CWeb`; single consumer `CPass` may be able to diff positions itself. | Check before cutting; remove if no behavior changes. | `src/WallpaperEngine/Render/Wallpapers/CScene.h` |
| stdlib | Vendored `Debugging/CallStack` (392 lines incl. Windows paths) backs two crash-log sites. | `backtrace_symbols` + `__cxa_demangle` (~25 lines) or C++23 `std::stacktrace`. | `src/WallpaperEngine/Debugging/` |
| yagni | CEF downloads + links unconditionally even for scene-only builds. | Gate web wallpapers behind a `WITH_WEB` CMake option. | `CMakeLists.txt` |
| yagni | Single-implementation interfaces `AudioDriver` (SDL only) and `AudioPlayingDetector` (PulseAudio only). | Fold base into impl until a second driver exists. | `src/WallpaperEngine/Audio/Drivers/` |
| shrink | ~9 `rfind("x", 0) == 0` sites on C++20. | `starts_with`. | app/scripting/render files |

Completed 2026-07-08 by the MdlParser extraction: strict `parseMdlvHeader`
path, removal of duplicate `puppetRead*` helpers and section-scan lambdas, and
removal of the `BinaryReader+MemoryStream` puppet vertex copy. This closure is
kept here so the cleanup is not proposed again.

# Implementation Order

1. Low-risk deletes first: unreferenced submodules, dead includes, `starts_with`.
2. `Maths`, `ObjectParser` dedupe, `getMousePositionLast` after checking callers.
3. CallStack replacement when touching that file anyway. (The `ShaderUnit` regex
   item is largely obsolete — see the corrected row above.)
4. `WITH_WEB` gating separately (packaging impact).

# Related Concepts

- [Puppet Warp Pipeline](../rendering/Puppet%20Warp%20Pipeline.md)
- [MDL File Format](../rendering/MDL%20File%20Format.md)
- [Shader Translation](../rendering/Shader%20Translation.md)
- [Known Issues](Known%20Issues.md)
