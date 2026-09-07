# Original compatibility audit

**Historical findings; source review on 2026-09-06 supersedes the statuses below.**
Use the [project checklist](Organized%20all.md) for the maintained task list.

- [x] Layer hierarchy/transform/texture-animation APIs and `cursorLeftDown`
  are implemented (`a1e1a186`). Live interaction still needs verification.
- [ ] CPU particle audio processing remains unimplemented.
- [x] Sequence defaults, fractional counts, bounds, repeat/mirror, and additive
  velocity are corrected (`d3ec5626`).
- [ ] Sequence flags/operation semantics and visual parity still need work.
- [x] JSON comments/trailing commas and the named metadata call sites now use
  the compatible reader (`149cefb2`).
- [ ] The translucent `generic4` double-sided-lighting heuristic remains.

The test totals and file line numbers in the original note are historical.

---

 I found five concrete mismatches against the installed Wallpaper Engine 2.8.42 binary. No source code, commits, or wallpapers were changed.

  1. SceneScript APIs are missing or stubbed — highest priority

  Wallpaper 3320489297 actively calls:

  - getTransformMatrix()
  - getParent()
  - getTextureAnimation()
  - input.cursorLeftDown

  Wallpaper Engine registers these APIs. Our implementation returns undefined for getParent, lacks the transform/texture-animation APIs, and
  always reports the left mouse button as false.

  Evidence: src/WallpaperEngine/Scripting/Adapters/ScriptableObjectAdapter.cpp:212, src/WallpaperEngine/Scripting/InputObject.cpp:38

  This explains content that loads but has broken interaction, transforms, or animation.

  2. CPU particle audio processing is parsed but not implemented

  We correctly read the audio-processing properties, but particle emitters/operators do not apply them. One vortex path actually disables
  itself whenever audio mode is enabled.

  Wallpaper 3320489297 uses audio mode 3 for its particles, so its emission timing and effects cannot match Wallpaper Engine.

  Evidence: src/WallpaperEngine/Data/Parsers/ObjectParser.cpp:726, src/WallpaperEngine/Render/Objects/CParticle.cpp:1373, src/
  WallpaperEngine/Render/Objects/CParticle.cpp:1453

  3. mapsequencearoundcontrolpoint is substantially incorrect

  The official binary uses these defaults:

  - count = 32
  - bounds = "0 1"
  - speedmin = "0 0 0"
  - speedmax = "0 0 0"
  - limitbehavior = "repeat"
  - It retains fractional counts and supports additional operation/flags behavior.

  Our implementation:

  - Defaults count to 1 and speed maximum to 100.
  - Truncates count to an integer.
  - Ignores bounds, limit behavior, operation, and flags.
  - Replaces an existing particle velocity with a random velocity.

  The bundled official DNA preset uses count: 2.2, proving integer truncation is wrong. More importantly, wallpaper 3320489297 uses count: 0
  after an authored velocity initializer. Our crash guard prevents division by zero, but then the incorrect default speed range can overwrite
  that authored velocity.

  Evidence: src/WallpaperEngine/Data/Parsers/ObjectParser.cpp:786, src/WallpaperEngine/Data/Model/Object.h:294, src/WallpaperEngine/Render/
  Objects/CParticle.cpp:1048

  4. JSON compatibility is incomplete

  Wallpaper Engine’s reader explicitly permits comments and trailing commas. Our compatibility parser only repairs trailing commas, and
  several shader/texture metadata paths bypass it completely.

  Evidence: src/WallpaperEngine/Data/JSON.cpp:59, src/WallpaperEngine/Render/Shaders/ShaderUnit.cpp:1378, src/WallpaperEngine/Data/Parsers/
  TextureParser.cpp:427

  This remains a plausible cause of wallpapers being rejected before their content appears.

  5. The Saturn-ring lighting fix is a non-native heuristic

  We currently force DOUBLESIDEDLIGHTING=1 onto every translucent generic4 model. The official binary does not automatically inject this
  combo.

  Evidence: src/WallpaperEngine/Render/Objects/CModel.cpp:114

  It fixes wallpaper 3589454154 visually, but can change unrelated translucent models. It is masking a deeper normal, lighting, culling, or
  transform mismatch.

  Recommended implementation order:

  1. Complete the SceneScript object, texture-animation, and cursor APIs.
  2. Implement the shared CPU particle audio-processing calculation.
  3. Rebuild mapsequencearoundcontrolpoint from the official schema and bytecode behavior.
  4. Route all wallpaper JSON through one comment/trailing-comma-compatible reader.
  5. Find the real Saturn-ring lighting discrepancy before removing the blanket heuristic.

  The existing tests still pass: 345 assertions across 49 test cases. That test coverage currently does not detect these compatibility gaps.