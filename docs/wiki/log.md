# linux-wallpaperengine Wiki Update Log

## 2026-07-08 (afternoon)

* **Creation**: [Current Status](Current%20Status.md) — live dashboard (open issues, parity gaps, verification queue); linked as the start-here page from the index.
* **Update**: [Known Issues](Known%20Issues.md) — moved the day's verified fixes (clock stack, capture drain, automute, sound dedupe/rotation, visualizer auto-gain, type-drift crash) into a Fixed section; media segfault stays pending-verify.
* **Update**: [Candidate Refactors](Candidate%20Refactors.md) — pruned the four items resolved by the MdlParser extraction, fixed the kissfft direction (the WE fork is the unlinked copy), merged in the 2026-07-08 audit findings.
* **Update**: [TODO Backlog](TODO%20Backlog.md) — audio-bar item closed, control-socket `prop` partial noted.
* **Update**: [Wallpaper Case Studies](Wallpaper%20Case%20Studies.md) — MyGO text/audio case notes; new honeycomb 3758354038 entry.

## 2026-07-08

* **Update**: [3D Scene Support](3D%20Scene%20Support.md) — documented that the runtime 3D camera is the camera *object* in `objects[]` (origin = eye, −Z forward), not the top-level editor-viewport `camera` block; plus the world-space UI-canvas pattern and the CText 3D mirror.
* **Creation**: Added [3D Scene Support](3D%20Scene%20Support.md) documenting the perspective-scene pipeline (camera, MDLV models, LightingV1 codegen, script property semantics) and its gap list.
* **Creation**: Added [WE Reference Mining](WE%20Reference%20Mining.md) with the findings extracted from `wallpaper64.exe` and the WE assets (generated lighting templates, engine combos, uniform inventory, HDR bloom chain, SceneScript d.ts/baseclasses.js locations).

## 2026-07-06

* **Initialization**: Converted the existing Obsidian wiki into an OKF-style bundle by adding frontmatter to concept documents and a root `index.md`.
* **Creation**: Added [Candidate Refactors](Candidate%20Refactors.md) from the user's cleanup backlog.
* **Creation**: Added OKF source reference under [references](references/okf-format-source.md).
