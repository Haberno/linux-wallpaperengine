# linux-wallpaperengine Wiki Update Log

## 2026-07-08

* **Update**: [3D Scene Support](3D%20Scene%20Support.md) — documented that the runtime 3D camera is the camera *object* in `objects[]` (origin = eye, −Z forward), not the top-level editor-viewport `camera` block; plus the world-space UI-canvas pattern and the CText 3D mirror.
* **Creation**: Added [3D Scene Support](3D%20Scene%20Support.md) documenting the perspective-scene pipeline (camera, MDLV models, LightingV1 codegen, script property semantics) and its gap list.
* **Creation**: Added [WE Reference Mining](WE%20Reference%20Mining.md) with the findings extracted from `wallpaper64.exe` and the WE assets (generated lighting templates, engine combos, uniform inventory, HDR bloom chain, SceneScript d.ts/baseclasses.js locations).

## 2026-07-06

* **Initialization**: Converted the existing Obsidian wiki into an OKF-style bundle by adding frontmatter to concept documents and a root `index.md`.
* **Creation**: Added [Candidate Refactors](Candidate%20Refactors.md) from the user's cleanup backlog.
* **Creation**: Added OKF source reference under [references](references/okf-format-source.md).
