---
type: Refactor Backlog
title: Candidate Refactors
description: User-supplied cleanup backlog for linux-wallpaperengine, captured as OKF knowledge before implementation.
resource: file:///home/admin/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, refactor, cleanup, backlog]
timestamp: 2026-07-06T20:00:00-04:00
---

# Scope

These are candidate refactors for the current linux-wallpaperengine fork. They are notes, not completed changes. Inspect the dirty worktree before implementing anything.

# Candidates

| Kind | Item | Replacement | Target |
|---|---|---|---|
| delete | `parseMdlvHeader` strict-layout path never matches real MDL files and logs a spurious error before scan fallback. | Use the scan fallback unconditionally. | `src/WallpaperEngine/Render/Objects/CImage.cpp` |
| native | Two vendored FFT copies are in-tree; only one is linked. | Keep the WallpaperEngine fork and drop vanilla `kissfft`, including bench/test binaries such as `bm_*`, `fastconv*`, `ffr`, and `st-float*`. | `src/External/kissfft` |
| yagni | `loadPuppetMesh` has dual read paths: `BinaryReader+MemoryStream` over a full vertex buffer copy and raw `puppetRead*` helpers over the same vector for bones/animations. | Use the puppet read helpers everywhere and delete the copy/reader allocation. | `src/WallpaperEngine/Render/Objects/CImage.cpp` |
| stdlib | `Maths::lerp` duplicates `glm::mix`, which is already common in the repo. | Replace fade interpolation with `glm::mix(sv, ev, glm::clamp(...))`; delete `Maths.cpp/h` if random helpers move to their single caller, `CParticle`. | `src/WallpaperEngine/Maths.cpp` |
| shrink | `puppetReadUInt32` and `puppetReadFloat` are the same function twice. | Use one `template <typename T> T puppetRead(...)` helper with `memcpy`. | `src/WallpaperEngine/Render/Objects/CImage.cpp` |
| shrink | Two identical `ObjectData{...}` initializer blocks exist in `ObjectParser` try/catch; only id/name differ. | Resolve id/name first, then build once. | `src/WallpaperEngine/Data/Parsers/ObjectParser.cpp` |
| yagni | `ShaderUnit` constructs 26 `std::regex` values per shader unit per pass. | Prefer `static const` regexes, or plain string scanning for fixed-literal one-liners. | `src/WallpaperEngine/Render/Shaders/ShaderUnit.cpp` |
| delete | `getMousePositionLast` plumbing is exposed by `CScene`/`CWeb`; single consumer `CPass` may be able to diff positions itself. | Check before cutting; remove if no behavior changes. | `src/WallpaperEngine/Render/Wallpapers/CScene.h` |
| yagni | `loadPuppetMesh` has duplicated inline section-scan lambdas for MDLS and MDLA. | Use one `findSection(data, tag)` helper. | `src/WallpaperEngine/Render/Objects/CImage.cpp` |

# Implementation Order

1. Start with narrow, low-risk parser cleanup: `findSection`, `puppetRead<T>`, strict `parseMdlvHeader` deletion.
2. Then remove the `BinaryReader+MemoryStream` puppet vertex copy if tests and case-study wallpapers still pass.
3. Treat vendored `kissfft` deletion separately because it can affect build files and packaging.
4. Do `Maths` and `ShaderUnit` cleanups after checking all callers and current local edits.

# Related Concepts

- [Puppet Warp Pipeline](Puppet%20Warp%20Pipeline.md)
- [MDL File Format](MDL%20File%20Format.md)
- [Shader Translation](Shader%20Translation.md)
- [Known Issues](Known%20Issues.md)
