---
type: Verification Report
title: Asset Texture Verification
description: Completed asset and texture work, reproducible checks, and remaining unrelated work.
tags: [linux-wallpaperengine, textures, validation]
timestamp: 2026-09-07T00:00:00-04:00
---

# Asset and texture verification

The four requested asset tasks are complete. The [project checklist](Organized%20all.md)
collects the broader documentation and keeps unrelated work open.

- [x] **TEXB0004 conditional mipmaps and variant selection — `f7d97740`.**
  Conditional descriptors precede the images; patch groups follow each base
  mip's payload. The loader now consumes all levels, applies the selected
  patches, and responds to the owning scene's properties. Texture cache bindings
  include the scene's property objects so separate wallpaper instances retain
  independent selections. A failed replacement preserves the previous GPU texture.
- [x] **Kirby's Sky asset audit — no missing asset.** In `2742564457`, the
  authored reference is `models/Sky.json` and the packaged entry is
  `models/sky.json`. The existing case-insensitive package index resolves it;
  the matching material and texture are present. The original package rendered
  successfully. The initial camera faced blue sky and stars; this closes the
  missing-asset claim, without asserting full camera/composition parity.
- [x] **Stock preview/source texture lookup — `7002a065`.** Compiled fallbacks
  search explicit `effects/<effect>/preview/materials` directories only for
  texture requests. Scene/project/material JSON does not enter the root lookup
  namespace. When the compiled texture is absent, a PNG with required `.tex-json`
  metadata can be imported with its channels, sampler flags and mip settings.
  Corrupt compiled files remain errors rather than silently using source images.
  Sonic's `waterripplenormal` now resolves through this path.
- [x] **Short workshop shader paths — `dea7758f`.** Both iterator advances
  check the end before dereferencing. Regression coverage includes short paths,
  ordinary shaders, compatibility overrides and fallback behavior.

## Wallpapers to check

Use the original installed Workshop items. Variant controls below are the actual
project properties used by conditional textures; changing a different scene
setting does not test the same path.

| Workshop ID | Wallpaper | Control and expected result |
|---|---|---|
| `2924081598` | Super Mario Voxel | **Scene Style** (`groundcolor`): `0` Ground, `1` Underground, `2` Snow. Ground and Snow should change brick/pipeline textures; distant small bricks should retain their authored mip detail. |
| `3766299002` | 麻匪 皓风琦修罗 双主题切换 | **人物面具 mask** (`newproperty`), off/on: facial mask and eye details change. `newproperty2` is a separate theme control. |
| `3737268876` | Ocarina of Time | **Link's Tunic Color** (`tuniccolor`): `0` Green, `1` Red, `2` Blue, `3` Dark. Hat and tunic textures should change together. |
| `2742564457` | Kirby - Dream Stroll | Sky renders instead of a missing-model failure; confirm the intended camera/composition during normal playback. |
| `2743274752` | Sonic the Hedgehog - VS Eggman | Scene loads with the water-ripple normal texture; inspect water/reflection effects as the camera advances for a flat or black replacement. |

For a windowed property test, for example:

```bash
build/output/linux-wallpaperengine --window 0x0x640x360 --silent \
  --set-property groundcolor=2 \
  "$HOME/.local/share/Steam/steamapps/workshop/content/431960/2924081598"
```

## Completed checks

- [x] Debug engine build and full C++ suite: **914 assertions, 115 test cases**.
- [x] Focused asset tests: **270 assertions, 12 test cases**.
- [x] Real texture corpus: **412 TEXB0004 textures, 41 authored variants,
  486 mip levels** across the five wallpapers above; **190 assertions passed**.
- [x] Eleven isolated GPU screenshot runs produced images and exited cleanly,
  covering the five wallpapers, property variants, a script fixture and synthetic
  mip diagnostics. Screenshots were visually inspected.
- [x] GPU mip diagnostic: the base texture renders red at large size and blue
  when minified; the alternate renders green and yellow respectively. This
  verifies smaller mip upload and selection in actual rendering.
- [x] Live property updates: synthetic `alternate` toggled three times;
  Mario's `groundcolor` changed `0 → 1 → 2 → 0`. The health report recorded
  **48 texture variant updates** for Mario, with no variant-update errors.
- [x] A real SceneScript fixture verified 16/32/64-band left/right/average
  `Float32Array` views, array reduction, shared backing memory across repeated
  registrations, and `thisObject === thisLayer` during updates.
- [x] Kirby shader dumps advertise `TEX4FORMAT 9` for the R8 toon gradient.
  Kirby/Ocarina screenshots no longer show the shader-default format mismatch's
  red lighting cast.
- [x] Debug/release preset discovery, Debug compilation and Release configuration.
  Release configuration reused the downloaded CEF distribution from `build/`;
  the first download attempt left an empty archive after a DNS failure.
- [x] Full installed corpus: **364 wallpapers** (329 scenes, 25 web, 10 video)
  tested in **27m 31s**: **85 PASS, 218 WARN, 61 FAIL**. Three non-wallpaper
  asset packs were recorded as SKIP. **50 failures were shader-check-only;
  11 included runtime/startup/shutdown failures.** All IDs were checked for
  complete, unique coverage. See [every result](Full%20Corpus%20Validation.md).
  Shader deduplication avoided **63,984** duplicate checks; **10,140** unique
  stage/source pairs were compiled. A 3m 05s serial follow-up confirmed
  Snowflakes renders after 91.3 seconds; all six web shutdown failures reproduced.
  **Reviewed totals: 85 PASS, 219 WARN, 60 FAIL** (50 shader-only, 10 runtime).

Focused-test logs, synthetic fixtures and screenshots are local transient artifacts
under `/tmp/lwe-assets-20260906/`. The parser's optional real-corpus test can be
repeated after extracting the named wallpapers:

```bash
LWE_TEXTURE_CORPUS=/tmp/lwe-assets-20260906/extracted \
  build/output/tests '[texture-corpus]'
python3 -m unittest discover -s tools/tests -v
```

The binary was rebuilt from the current working tree, including the unfinished
local changes described below. The short renderer tests do not establish long-term
stability, audio playback (`--silent` skips sound loading), or complete visual parity.

## Format evidence

The [texture format reference](wiki/reference/Texture%20File%20Format.md) records
field order and patch semantics. Native readers `14015c8d0` and `14015c480` were
inspected in `wallpaper64.exe`, SHA-256
`40e2ce021e9352324fadb3b8f72b8ba2a7ee95b71cc571d5b9f84be75cd993b0`.
All 41 real conditional descriptors in this corpus had flags zero; additional
flag handling has native-code evidence, not a claim of authored corpus coverage.

## Commit boundaries and remaining work

Finished preexisting changes were committed separately by hunk:

| Commit | Completed work |
|---|---|
| `dc60c13e` | Construct live audio arrays with the `Float32Array` constructor. |
| `181c752e` | Bind `thisObject` to the owning layer. |
| `b189b7ad` | Include shader-default textures in sampler format metadata. |
| `aaed9854` | Portable host-toolchain presets and corrected CLion setup guide. |
| `911a37d2` | Parallel corpus validator, shader deduplication and startup-aware timing. |

The following remains uncommitted because it is incomplete:

- [ ] `EngineObject.cpp` / `SceneObject.cpp`: `registerAsset` and `{file}` handle
  consumers need real asset loading, `precache` and lifetime semantics.
- [ ] `CText.cpp`: effect-surface publication needs plain-text targets and a
  review of resize/rebuild pointer lifetime and render scale.
- [ ] `CPass.cpp`: user-texture property lookup must consistently cover overrides
  and shader metadata, not only the current binding loop.
- [ ] `ScriptEngine.cpp`: temporary queue-script trace instrumentation.

Separate runtime gaps also remain. Ocarina rendered but logged reserved-word
`input` shader errors (8 of 1,042 dumped units), missing model-animation scripting
APIs, and missing plain-text composite targets. Mario also logged existing model
animation API errors. Those findings do not reopen the verified texture variants,
but prevent calling the wallpapers completely error-free.
