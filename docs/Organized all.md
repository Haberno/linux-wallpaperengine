---
type: Project Checklist
title: Project Checklist
description: Consolidated, checkbox-based status of project documentation, checked against the current source where statuses conflict.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, checklist, status, backlog]
timestamp: 2026-09-07T00:00:00-04:00
---

# Project checklist

Initial consolidation reviewed **2026-09-06** against fork `main` at
`9edaa4dc`. Updated **2026-09-07** after the focused asset/texture fixes and
verification described in [Asset Texture Verification](Asset%20Texture%20Verification.md). This replaces the old A–J box tables and gathers
the wiki, standalone audits, debug findings, and setup/tooling docs into one
place. Detailed explanations and historical measurements stay in their sources.

**How to check things off:**

- `[x]` means the stated implementation or investigation is complete, supported
  by source, a commit, or explicitly recorded verification. It does not imply
  every wallpaper has been visually verified.
- `[ ] Open` means implementation or diagnosis remains.
- `[ ] Verify` means code exists but the stated runtime/visual check remains.
- `[ ] In progress` means an uncommitted implementation exists and needs review
  and validation before closure.
- `[ ] Audit` means coverage is unknown, partial, or conflicting; investigate
  before assuming a missing implementation. `Deferred` means optional work.

The initial consolidation was a documentation/source review. The focused follow-up
rebuilt the engine, ran the full regression suite, decoded the five-wallpaper
texture corpus, and checked isolated GPU screenshots and live property changes.
The full installed corpus also completed on 2026-09-07; its failures remain
open as recorded below. The 2026-08-19 figures are historical. The checklist, maintained wiki and texture verification note are tracked;
local-only runbooks and generated artifacts remain outside the documentation commit.
Vendored manuals and generated build files are outside this checklist.

## Contents

- [Priority issues and work in progress](#priority-issues-and-work-in-progress)
- [SceneScript and input](#scenescript-and-input)
- [Assets, JSON, and textures](#assets-json-and-textures)
- [Particles](#particles)
- [2D composition, puppets, and parallax](#2d-composition-puppets-and-parallax)
- [3D cameras, models, lighting, and post-processing](#3d-cameras-models-lighting-and-post-processing)
- [Text](#text)
- [Audio, video, and memory](#audio-video-and-memory)
- [Runtime controls, web, and desktop](#runtime-controls-web-and-desktop)
- [Performance and validation tools](#performance-and-validation-tools)
- [Live verification queue](#live-verification-queue)
- [Recorded regression results](#recorded-regression-results)
- [Optional refactors and inherited TODOs](#optional-refactors-and-inherited-todos)
- [Remaining upstream coverage to assess](#remaining-upstream-coverage-to-assess)
- [Documentation and source coverage](#documentation-and-source-coverage)

## Priority issues and work in progress

Sources: [Known Issues](wiki/status/Known%20Issues.md),
[Current Status](wiki/status/Current%20Status.md),
[original compatibility audit](TODDO.md),
[corpus findings](../src/WallpaperEngine/Debug/WALLPAPER_FINDINGS.md).

- [ ] **Open — triage remaining runtime failures in the full 2026-09-07 corpus.**
  The three earlier scene SIGSEGVs (`1979606285`, `3644280276`, `3768356757`)
  reproduced; `764162681` rejects the older **TEXV0004** outer header (distinct
  from the fixed **TEXB0004** body format). Six web wallpapers failed during
  CEF shutdown. Serial rechecks reproduced all six web failures; `3378346807`
  rendered after 91.3 seconds, closing its startup-timeout failure but leaving
  slow parsing and script/model warnings. Full results and all IDs are in
  [Full Corpus Validation](Full%20Corpus%20Validation.md).
- [x] **Validate and commit the `thisObject` binding (`181c752e`).** A live
  SceneScript fixture confirmed that `thisObject` and `thisLayer` reference the
  same owning layer during updates. Other startup property-hook errors remain below.
- [x] **Validate and commit full-length live audio arrays (`dc60c13e`).** Real
  SceneScript checked 16/32/64-band left/right/average arrays, `reduce()`, and
  shared backing memory across repeated registrations.
- [ ] **Open — resolve remaining startup `applyUserProperties` errors:** missing
  objects/properties, read-only assignments, and non-functions. The alias alone
  does not establish that the whole failure set is fixed.
- [ ] **In progress — finish/review `engine.registerAsset` and handle consumers.**
  `EngineObject` and `SceneObject` still contain the unfinished `{file}` handle
  implementation, which ignores `precache`. Validate real loading and lifecycle.
- [ ] **In progress — finish text-composite publication and user-texture binding.**
  `CText` publishes effect surfaces locally, but resize/lifetime and plain-text
  composition need review. `CPass` resolves only part of the user-texture path;
  override and shader metadata paths still need consistent property resolution.
  These changes and temporary script tracing remain uncommitted.
- [ ] **Open — Sonic Rooftop material/transparency parity (`3708206626`).**
  Extract the actual passes and trace textures, combos, blend/depth/cull state,
  and draw order. Earlier fixes do not close the later report of a missing
  floor or translucent bubble around Sonic. Keep HDR diagnosis separate.
- [ ] **Open — replace Saturn's blanket double-sided-lighting heuristic with
  the native behavior.** `CModel::setup` still injects `DOUBLESIDEDLIGHTING`
  for translucent `generic4` passes without an authored value. Identify the
  normal/lighting/culling mismatch before removing the workaround.
- [ ] **Verify — recheck the unresolved `2444472355` switch stall.** One handoff
  to `1545949115` passed, but the longer unique-wallpaper run stopped servicing
  control requests. No later explicit closure was found for that scenario.
- [x] **Implement the null layer-type / missing particle-material crash guard
  (`765030095`).** HEAD `9edaa4dc` rejects null `particle`/`text`/`shape`
  discriminators and skips material-less particles; `JsonTolerance.cpp`
  contains the null-key regression. The full sweep rendered 30 frames and
  exited cleanly; unrelated warnings remain.

## SceneScript and input

Sources: [SceneScript Runtime](wiki/rendering/SceneScript%20Runtime.md),
[3D Scene Support](wiki/rendering/3D%20Scene%20Support.md).
Code: [ScriptableObjectAdapter.cpp](../src/WallpaperEngine/Scripting/Adapters/ScriptableObjectAdapter.cpp),
[InputObject.cpp](../src/WallpaperEngine/Scripting/InputObject.cpp),
[SceneObject.cpp](../src/WallpaperEngine/Scripting/SceneObject.cpp).

- [x] Implement runtime layer creation, layer indexing/sorting, enumeration,
  `getLayer`, and `getLayerByID`.
- [x] Implement `getParent` and `getChildren` by resolving the live scene graph
  (`a1e1a186`); these are no longer no-op stubs.
- [x] Implement `getTransformMatrix`, `rotateObjectSpace`, `lookAt`, and
  `lookAtYaw` (`a1e1a186`).
- [x] Implement `setParent`, attachment selection, parent-cycle rejection, and
  optional transform preservation (`a1e1a186`).
- [x] Expose attachment transforms and image `getTextureAnimation` controls:
  play/pause/stop, frame get/set, join, duration, frame count, and rate.
- [x] Connect `input.cursorLeftDown` to `MouseInput::leftClick`; expose pixel
  screen coordinates and world-space cursor projection.
- [x] Implement both camera-transform APIs: live eye/center/up/FOV reads and
  writable script overrides with non-finite pose rejection.
- [x] Preserve script camera ownership across frames; apply overrides after the
  authored camera/path state.
- [x] Dispatch the complete user-property object at startup (`f826719a`), plus
  per-property updates on live edits.
- [x] Defer layer-dependent initialization until registration is complete;
  execute all `init` hooks before the first `update` pass.
- [x] Preserve current property values when a hook returns null/undefined;
  handle angle units and invalid vector operands at the script boundary.
- [x] Correct vector operand order and keep the builtins math constants aligned
  with module semantics; prevent zero first-frame `engine.frametime`.
- [x] Provide WEColor/WEMath/WEVector compatibility, shared scene state,
  `setTimeout`/`setInterval`, and live 16/32/64-band audio-buffer views.
- [x] Provide namespaced **in-memory** `localStorage` in `builtins.js`.
- [ ] **Open — persist `localStorage` across process restarts.** The in-memory
  implementation does not satisfy the upstream persistence requirement.
- [ ] **Open — implement engine context queries** such as `isWallpaper`,
  `isDesktopDevice`, and `isPortrait`; no bindings found in the scripting tree.
- [ ] **Open — rebuild structural scene state when a visibility/combo property
  changes.** The `prop` command currently updates a value and dispatches the
  hook without reconstructing the scene.
- [ ] **Audit — compare remaining property-binding/JSON-coercion ports by
  behavior** (`1990748`, `d1e26cb`, `0ccc6c8`), accounting for existing fixes.
- [ ] **Deferred — consider using native JS vector classes** instead of the
  C++ adapters when revisiting the scripting architecture.

## Assets, JSON, and textures

Sources: [Texture File Format](wiki/reference/Texture%20File%20Format.md),
[Known Issues](wiki/status/Known%20Issues.md).
Code: [JSON.cpp](../src/WallpaperEngine/Data/JSON.cpp),
[TextureParser.cpp](../src/WallpaperEngine/Data/Parsers/TextureParser.cpp).

- [x] Accept JSON line/block comments and trailing commas with a string-aware
  sanitizer (`149cefb2`); retain errors for unrelated malformed JSON.
- [x] Route project/scene/model/material/effect, shader metadata, and
  `.tex-json` reads through `parseCompatible`. The old direct-parse bypasses
  named in `TODDO.md` have been replaced; focused JSON cases exist.
- [x] Tolerate authored type drift and both numeric/object dependency entries.
- [x] Mount stock effect containers and every wallpaper `*.pkg` file, including
  `audiophile.pkg`.
- [x] Resolve package paths case-insensitively with an indexed lookup.
- [x] Log and skip an unavailable effect while preserving the base layer and
  other effects (`55d53526`); deliberately missing-effect fixture verified
  in the 2026-07-30 record.
- [x] Parse TEXV/TEXI, TEXB0001–0004, and TEXS0001–0003 animation tables;
  handle LZ4/compressed texture data and frame rectangles.
- [x] Decode image payloads in a thread pool and validate GPU upload sizes.
- [x] Fix tightly packed RG88 upload alignment (`fc41164f`), including odd widths.
- [x] Scope texture cache identity to each asset locator and preserve authored
  format/component flags and user-texture overrides.
- [x] **Decode TEXB0004 conditional mip interleaving and select variants
  (`f7d97740`).** Consume patch groups after every base mip; apply authored
  conditions and partial DXT/RGBA/PNG patches in group priority order. Each
  scene binds its own properties, and live changes update the GPU texture.
  All 41 variants across 412 TEXB4 textures and 486 mip levels decoded; GPU
  minification and live Mario style switching passed.
- [x] **Audit `models/Sky.json` on `2742564457`.** The package contains
  `models/sky.json`, `materials/sky.json`, and `materials/sky.tex`. Existing
  case-insensitive package lookup resolves the mixed-case references. The
  original package rendered and saved a screenshot; no asset is absent.
- [x] **Resolve stock preview `.tex` and PNG + `.tex-json` sources (`7002a065`).**
  The old loader required `.tex` before consulting optional spritesheet metadata.
  Explicit texture-only preview fallbacks and metadata-backed PNG imports now
  work through the shared loader. Sonic VS Eggman rendered with no missing
  `effects/waterripplenormal`; preview scene/material collisions are regression-tested.
- [x] **Guard short workshop shader paths (`dea7758f`).** Check iterators before
  dereference/increment, retaining ordinary lookup errors and valid compatibility overrides.
- [ ] **Open — support legacy TEXV0004 outer headers (`764162681`).** The full
  sweep isolated this fatal parse error; it is separate from TEXB0004 variants.
- [x] **Include shader-default sampler formats (`b189b7ad`).** R8 toon gradients
  now advertise their format without overriding authored material slot metadata;
  validated in 54 generated Kirby fragment units and live 3D screenshots.

## Particles

Sources: [original audit](TODDO.md),
[Upstream Feature Inventory](wiki/reference/Upstream%20Feature%20Inventory.md).
Code: [CParticle.cpp](../src/WallpaperEngine/Render/Objects/CParticle.cpp),
[ObjectParser.cpp](../src/WallpaperEngine/Data/Parsers/ObjectParser.cpp).

- [x] Implement the existing emitter/initializer/operator simulation and
  sprite/rope/trail rendering paths; broader upstream parity remains to audit.
- [x] Apply instance rate to the complete simulation (`ff29820c`) and keep
  count scaling independent; focused particle semantics cases exist.
- [x] Make rope trails follow the live particle's alpha/color/size (`94418161`).
- [x] Correct `mapsequencearoundcontrolpoint` defaults to count 32, bounds
  `[0,1]`, zero min/max speed, and repeat behavior (`d3ec5626`).
- [x] Preserve fractional counts, normalized sequence phase, repeat/mirror
  behavior, control-point basis/radius, and additive velocity in that initializer.
- [ ] **Open — resolve remaining sequence flags/operation semantics.** `flags`
  is parsed but not consumed by the initializer, and an operation path is not
  present. Do not mark the entire original mismatch closed.
- [ ] **Open — implement shared CPU particle audio processing** for emitters,
  initializers, and operators. Emission still has a TODO; turbulence reads are
  commented out; the vortex uses constant-zero amplitude and skips audio mode.
- [ ] **Verify — compare `3320489297` and fractional-count presets** against
  the reference after the initializer changes; no live parity closure was found.

## 2D composition, puppets, and parallax

Sources: [Puppet Warp Pipeline](wiki/rendering/Puppet%20Warp%20Pipeline.md),
[MDL File Format](wiki/rendering/MDL%20File%20Format.md),
[Parallax System](wiki/rendering/Parallax%20System.md).

- [x] Render image/effect layers with blend modes, offscreen targets, parent
  transforms, visibility cascade, and layer-as-texture inputs.
- [x] Isolate composition subtrees and framebuffer aliases (`62f8a680`),
  preserve child alpha, and retain flat composition stack effects (`7bf76dff`).
- [x] Parse the documented MDLV puppet layouts, MDLS skeleton versions,
  MDAT attachments, and MDLA animation versions using shared parsers.
- [x] Render puppet effects in image space and warp only the final composite;
  flatten puppet depth and handle the flipped winding without culling the mesh.
- [x] Evaluate animation fps, loop/mirror/single playback, live rate/blend/
  visibility, and blend transitions; sample keyframed object transforms.
- [x] Use one shared reference pose and compose additive translation, rotation,
  and scale per component (`1e23f676`, `6040e79e`). **Current code uses the
  first available embedded bone sample, falling back to bind pose**; the older
  bind-only note was superseded. See `referenceBone` in `MdlAnimation.cpp`.
- [x] Resolve nested attachment chains from the parent's current bone pose.
- [x] Parse auxiliary clipping positions, draw ranges, and descriptors; render
  the ordinary zero-flags single-mask path (`c6536551`).
- [x] Parse blend/scalar-track boundaries without inventing bogus animation
  records; recover Reze's authored blink clip.
- [x] Drive the single-row puppet texture-channel overlay with `g_BlendMap`.
- [x] Implement native parallax defaults, root origin/depth ownership, half-canvas
  displacement, delay response, and shader input semantics; editor
  `locktransforms` no longer suppresses parallax.
- [x] Honor 2D `general.zoom` overscan and the root-origin parallax term;
  reproduce and remove gray edge strips on Stratospheric Twilight (3768356757),
  Dark Leaf (3755078205), and Gojo (3100265648) in fixed-cursor captures.
- [ ] **Verify — user confirmation of those three live wallpapers** at all
  corners and the left/right extremes; full Windows visual parity remains open.
- [x] Supply rotation-aware effect projection matrices for depth parallax.
- [x] Implement orthographic camera auto-size and authored 2D opening animations.
- [ ] **Open — compose nested/multiple clipping masks and nonzero descriptor
  flags.** Unsupported cases still fall back to unmasked puppet draws.
- [ ] **Open — implement puppet bone constraints (`tp`/`tm`).**
- [ ] **Open — complete puppet effect flags:** `clampuvs`, `copybackground`, `solid`.
- [ ] **Open — implement image/model `autosize` semantics**, distinct from
  fullscreen sizing and camera auto-size.
- [ ] **Open — recover passthrough-image behavior without effects**, using
  OS Waves' `cust` layer as the regression case.
- [ ] **Open — extend puppet channel maps beyond `BLENDROWCOUNT=1`** and consume
  the remaining per-bone scalar/event/constraint metadata once recovered.
- [ ] **Verify — check depth-parallax ray marching and rotated-layer projection
  against the reference.** Input semantics are recovered; full visual parity
  is not established by those inputs alone.

## 3D cameras, models, lighting, and post-processing

Sources: [3D Scene Support](wiki/rendering/3D%20Scene%20Support.md),
[Camera Path Playback](wiki/rendering/Camera%20Path%20Playback.md),
[WE Reference Mining](wiki/reference/WE%20Reference%20Mining.md).

- [x] Render perspective scenes using the active camera layer's live world
  transform, parent hierarchy, correct output orientation, and model depth defaults.
- [x] Parse and evaluate legacy and Bezier camera paths, FOV/zoom, sequence/
  random queues, visible camera selection, fades, loop/mirror/single modes,
  start-paused state, and wraploop closure (`878a4ca4`).
- [x] Bind each layer's projection to the active camera (`2675ee09`).
- [x] Render static and GPU-skinned MDLV models with shared MDLS/MDLA poses,
  `g_Bones`, nested MDAT attachments, and independent submesh flag `0x400`.
- [x] Sort transparent model slots back-to-front without moving non-model
  dependencies; honor authored `customsortorder`.
- [x] Implement distance/height fog with scripted values and native combo/uniform packing.
- [x] Implement point/directional/spot/tube LightingV1, native defaults,
  spot cone/cookie plumbing, and animated tube endpoints.
- [x] Render spotlight, three-cascade directional, and six-face point shadows
  in the shared depth atlas for static/skinned **opaque model** casters.
- [x] Preserve authored alpha-to-coverage material state and normalize the
  tangent basis so object scale does not amplify normal maps.
- [x] Run LDR bloom on 3D scenes and parse authored bloom tint.
- [x] Implement `--render-scale` for scene/effect targets while keeping fixed
  shadow/cookie targets at their required sizes (`5790f65d`).
- [x] Implement final-output `--contrast` and `--saturation` (`b9a8b8aa`).
- [x] Restrict reflection mipmap generation to its dedicated target (`d3c781d0`).
- [ ] **Open — implement the real HDR downsample/blur/upsample/final-combine
  pipeline** (`hdr`, `bloomhdr*`), after the ordinary Sonic material work.
  The reverted global RGBA16F experiment is not an active implementation.
- [ ] **Open — implement morph/model extensions** (`MDMP`/`MDLE`, `g_Morph*`)
  and texture-backed morph evaluation.
- [ ] **Open — orchestrate reflection and volumetric post-processing**, including
  alternate transforms and the mipmapped framebuffer inputs. Mipmap support
  alone does not close the reflection pipeline.
- [ ] **Open — support `perspectiveoverridefov`.**
- [ ] **Open — dispatch camera-path `options.events` callbacks.** No authored
  installed case was recorded when the camera-path document was written.
- [ ] **Open — support alpha-cutout/translucent and non-model shadow casters.**
  `CModel::renderShadow` currently accepts only `BlendingMode_Normal` passes;
  material alpha-to-coverage support is not foliage-shadow support.
- [ ] **Audit — assess conventional versus reversed depth and exact bias/filter
  parity.** Current rendering uses conventional GL depth with `REVERSEDEPTH=0`.
- [ ] **Audit — bind remaining native globals where content needs them:** view/
  orientation bases, `g_BonesAlpha`, HDR/morph/alternate matrices, legacy light
  arrays, pointer/daytime, post-pass variables, viewport matrices, and texture
  reduction scale. The reference inventory is historical, so check each consumer.

## Text

Sources: [TODO Backlog](wiki/status/TODO%20Backlog.md),
[Wallpaper Case Studies](wiki/reference/Wallpaper%20Case%20Studies.md).
Code: [CText.cpp](../src/WallpaperEngine/Render/Objects/CText.cpp).

- [x] Implement multiline layout, horizontal/vertical alignment, padding,
  embedded-font loading, UTF-8 decoding, 300-DPI point sizing, and y-up placement.
- [x] Compose parent transforms/visibility and run text property scripts.
- [x] Render text through effect chains with correct brightness and neutral
  intermediate values; fix GL clear-state and overflow regressions.
- [x] Match glyph alignment, effect line boxes, and effect-surface padding
  (`d9d555c0`, `ae0ac9c0`, `c4b06e41`).
- [ ] **Open — implement word wrapping and width/row/ellipsis limits:**
  `limitwidth`, `maxwidth`, `maxrows`, `limitrows`, `limituseellipsis`.
- [ ] **Open — implement text backgrounds:** `opaquebackground`,
  `backgroundcolor`, `backgroundbrightness`.
- [ ] **Open — implement dynamic screen anchoring (`anchor`).**
- [ ] **Open — add CJK/missing-glyph font fallback.**
- [ ] **Open — support command passes and scene-sampling effects on text.**
- [ ] **Audit — assess full shaping and emoji modifiers.** Current source uses
  FreeType glyph layout; no HarfBuzz calls were found, despite the coarse
  upstream inventory grouping shaping with implemented multiline text.
- [ ] **Open — assess/implement MSDF text rendering and letter spacing.**
  Ordinary blur/shadow effect chains and padding already exist; do not reopen
  those as missing merely because the upstream MSDF feature bundles them.

## Audio, video, and memory

Sources: [Audio Capture Silence Investigation](wiki/investigations/Audio%20Capture%20Silence%20Investigation.md),
[Video Texture Memory Growth](wiki/investigations/Video%20Texture%20Memory%20Growth.md),
[corpus findings](../src/WallpaperEngine/Debug/WALLPAPER_FINDINGS.md).

- [x] Drain capture fragments each frame to avoid sustained overruns.
- [x] Implement float-stereo capture, recovered 64-band frequency/magnitude
  mapping, normalization envelopes, and scene-spectrum smoothing.
- [x] Close the historical flat-capture symptom: basic response to music was
  user-confirmed on 2026-07-17. Preserve the old silence investigation as reference.
- [x] Make automute aware of corked/muted streams; provide audio enable/mute/volume controls.
- [x] Implement per-wallpaper audio deduplication, one-soundtrack-at-a-time
  rotation, and ownership migration when switching away.
- [x] Correct stereo resampler allocation, per-stream packet state, queue
  cleanup, and synchronized stream teardown; audio-enabled regressions recorded.
- [x] Remove the stale album-art listener on ScriptEngine destruction (af82084 port).
- [x] Fix the recorder disconnect/rebuild path (b52091d); hot-swap verification remains.
- [x] Play video wallpapers and embedded video textures through mpv.
- [x] Keep replaced video texture providers updating while still referenced,
  and render libmpv once per driver frame (`aa1336f2`).
- [x] Fix native layer-wrapper finalization and DBus error-payload cleanup.
- [x] Give passes/shaders/uniforms owned lifetimes; bound shader caches by bytes.
- [x] Fix scoped-texture eviction accidentally pinning every `$`-prefixed cache
  key; account for all image/mip bytes and trim after transition teardown.
- [x] Identify the dominant video leak as unconsumed libmpv GL fences and
  record a patched-library harness result: 5260 → 74 bytes/render (2026-07-29).
  This records completed diagnosis/validation, not the current installed library.
- [x] Identify the residual Wayland growth as undrained NVIDIA EGL surface
  event queues; measure and reject disabling explicit sync because of its FPS cost.
- [x] Explain the measured dual-monitor Radiant frame cost through serialized
  rendering; retain it as a parked investigation.
- [ ] **Verify — confirm the deployed libmpv actually contains the fence fix**
  and recheck long-run heap/RSS plus advancing/looping video on the live path.
  Current installed versions and mappings were not inspected in this review.
- [ ] **Deferred/upstream — track NVIDIA's residual event-queue leak.** Diagnosis
  is complete; a deployed driver fix has not been established here.
- [x] Preserve a pending Wayland frame callback during an early redraw instead
  of overwriting its handle. A headless smoke test rendered 152 frames and
  survived two output sleep/wake cycles; this is separate from the NVIDIA leak.
- [ ] **Open — recover or independently validate exact 64→32/16 spectrum
  reduction.** Current peak-preserving pooling is inferred.
- [ ] **Open — implement `--audio-device` capture-source override.**
- [ ] **Audit — reconcile simultaneous soundtrack ports with the intentional
  rotation design** before considering `faeb889`.
- [ ] **Deferred — profile Radiant's effect chain with RenderDoc** if pursuing
  more FPS at the same quality; the documented compute investigation is parked.

## Runtime controls, web, and desktop

Sources: [README](../README.md), [Current Status](wiki/status/Current%20Status.md),
[Debugging Workflow](wiki/workflow/Debugging%20Workflow.md).

- [x] Provide Wayland layer-shell, X11/XRandR, and GLFW window output;
  per-screen selection, screen spanning, scaling/clamping, FPS limits, and screenshots.
- [x] Provide wallpaper properties via listing/CLI overrides and live socket
  `prop`; support socket switching, transitions, `memstats`, and `fbostats`.
- [x] Make wallpaper replacement transactional and unwind partial scene builds safely.
- [x] Bind native engine getters to their owning instance so inherited layer
  script objects cannot crash on `engine.userProperties`, `canvasSize`, or
  `screenResolution` (Soulless switch core and replacement regression verified).
- [x] Service switch IPC while fullscreen-paused and bound Wayland event waits;
  preserve a running desktop engine when a submitted switch reply is delayed.
- [x] Prefer optimized builds for normal desktop launches; verify the Debug
  slowdown and its correction with matching Wayland switch measurements
  ([[Load Performance]]).
- [x] Restore Wayland background cursor tracking (`4e32d4f3`).
- [x] Provide fullscreen-pause controls, active-window filtering, application
  exclusions, and mouse/parallax disable switches.
- [x] Provide playlist loading, sequential/randomized timed playback, and
  transition selection (`buildPlaylistOrder` shuffles when configured).
- [x] Rework CEF lifecycle/switching (`b404e48f`) and helper exit (`1d193ef1`).
- [x] Decode percent-encoded asset URLs for spaces and non-ASCII filenames (`610720af`).
- [x] Derive web wallpaper theme/accent color from album art; expose CEF backend knobs.
- [x] Report unrecognized CLI arguments instead of silently discarding them.
  Accept both `--no-fullscreen-pause` and Waypaper's `--no-full-screen-pause` alias.
- [ ] **Open — add control-socket screenshots and live render-scale/audio-device updates.**
- [ ] **Audit — reconcile the upstream Web/CEF batch and property/audio listener
  injection with the current implementation.** Web wallpapers already run;
  the old “only if web enters use” condition is obsolete.
- [ ] **Audit — review CLI offsets, border color, orthographic scaling,
  structured JSON output, and properties-file/SIGUSR1 control** against this
  fork's existing socket interface.
- [ ] **Audit — port camera/orthographic branch findings individually**, accounting
  for the current camera, parallax, and supersampling implementation.

## Performance and validation tools

- [x] Speed up full-corpus validation with bounded renderer/shader concurrency,
  source/stage shader deduplication, incremental reports and a separate scene
  loading timeout. Four validator regression tests pass.
- [x] **Run the full installed corpus (2026-09-07).** Every one of 364 wallpapers
  was tested in 27m 31s: 85 PASS, 218 WARN, 61 FAIL; three asset packs SKIP.
  Fifty failures were shader-check-only, eleven included runtime/startup/shutdown
  problems. Follow-up cleared the slow-start timeout: **85 PASS, 219 WARN,
  60 FAIL** remain (50 shader-only, 10 runtime).
  [Full results and follow-up](Full%20Corpus%20Validation.md).

Sources: [Load Performance](wiki/rendering/Load%20Performance.md),
[Shader Translation](wiki/rendering/Shader%20Translation.md),
[debug hook ledger](../src/WallpaperEngine/Debug/README.md).

- [x] Instrument startup, texture prefetch/upload, scene build, first frame,
  switch prepare/apply, synchronous texture loads, frame timings, and errors.
- [x] Increase the texture-cache budget to 1536 MiB for overlapping heavy
  wallpaper switches (`eaa72fc`); the old 512 MiB figure is historical.
- [x] Cap glibc arenas and defer `malloc_trim` after switching.
- [x] Memoize includes/preprocessing, compatibility output, and translated GLSL
  **in memory**; a persistent disk cache remains missing.
- [x] Inject sampler-parameter macros (`33ece832`), preserve authored include
  macro visibility/order and combo-conditional texcoord widths, and cast
  float arguments for int parameters.
- [x] Bind `g_Frametime` on effect passes so feedback simulations can advance (`67c483bb`).
- [x] Dump composed shaders, including cache-hit variants; record unknown JSON
  keys and per-run health reports behind environment switches.
- [x] Provide isolated corpus validation, explicit input-path checks, and
  private control sockets.
- [x] Filter benign GLFW/GLEW startup errors from corpus classification (old J1).
- [x] Provide persistent engine logging, script/camera/audio traces, and
  `tools/lwe-monitor` for process/socket/CEF/coredump/resource inspection.
- [x] Provide the MDL clipping inspector, D3D compiler shim, RenderDoc capture
  setup, and reusable local Ghidra workflows.
- [ ] **Open — add a persistent content-addressed shader translation cache.**
  Reuse the existing in-memory caches rather than planning them from scratch.
- [x] Extend texture prefetch to every 3D model submesh material, including
  authored pass/user texture references (2026-09-08). Pokemon - Deep Sea Dive
  (3562141459) falls from 129 synchronous texture loads to 2.
- [x] Read each 3D model once in bulk for both geometry and animation
  (2026-09-08), eliminating duplicate byte-by-byte file copies.
- [x] Reuse linked GPU shader binaries for material and model-shadow passes
  (2026-09-08). The 64 MiB cache keys both complete sources, evicts least-recently
  used entries, and creates independent program/uniform state for every pass;
  unsupported/rejected binaries fall back to source compilation.
- [ ] **Open — prefetch remaining shader/effect-generated texture references.**
  Image, particle, text, 3D model materials and puppet clipping masks are covered;
  shader-declared defaults can still load during the scene build.
- [ ] **Open — diagnose Saturn's long project parse** (historically ~2.4 s),
  and the unexplained worst frame outside measured switch phases.
- [ ] **Open — profile representative steady-state CPU/GPU frame cost.**
  The specific Radiant dual-monitor measurement is not a general renderer profile.
- [x] Remeasure two desktop wallpaper switches on Wayland with shared-context
  uploads (2026-09-07; [[Load Performance]]).
- [x] Expand Wayland cold-start and switch measurements to heavy 3D scenes
  (2026-09-08); see [[Load Performance]] for cache-controlled Pokemon timings
  and the five-wallpaper, 28-second regression sample.
- [ ] **Verify — measure web wallpaper transitions** on Wayland.
- [ ] **Open — make texture budgeting appropriate to available memory and
  large/overlapping scenes**, beyond the current fixed 1536 MiB limit.
- [ ] **Audit — compare cold-build optimization port `739e9c6` with current
  phase timings** before adopting it.
- [ ] **Open — make standalone shader validation reproduce the engine's
  compilation context.** Dumps contain composed source, but the validator
  invokes glslang on individual units without reproducing the linked-unit
  preamble/context. Reproduce a false FAIL before choosing the exact fix.
- [ ] **Open — add deterministic clock, RNG seed, and pointer controls** for
  repeatable reference/golden-image testing (old J2).

## Live verification queue

Implementation checkboxes above and these verification checkboxes are separate
milestones. Follow [Debugging Workflow](wiki/workflow/Debugging%20Workflow.md);
use the user's live appearance/audio confirmation for perceptual parity.

- [x] **Verify — `765030095` loads and renders after the null-key guard.** The
  2026-09-07 full sweep rendered 30 frames and exited cleanly (WARN: 13 logged
  errors). This closes startup survival, not complete visual parity.
- [ ] **Verify — audio visualizers against native 16/32/64-band output:** levels,
  peaks, attack/decay, stereo separation, silence, and latency after 10 ms fragments.
- [ ] **Verify — Gojo `3100265648` survives repeated track/album-art changes**,
  including a change after switching away; check for new cores.
- [ ] **Verify — AirPods/default-sink hot-swap resumes capture without a crash
  or permanently flat spectrum.**
- [ ] **Verify — two different music wallpapers alternate at track end and
  duplicate wallpapers stay echo-free.**
- [ ] **Verify — repeated socket rebuilds preserve effects and final grading**
  without white, blank, or ghosted frames.
- [ ] **Verify — live 4K switch feel and RSS over 4–5 switches** on the compositor path.
- [ ] **Verify — camera paths/fades on Rayquaza `3045001236`, Deep Sea Dive
  `3562141459`, and Ocarina `3737268876`.** The later Klonoa `2885298446`
  22-shot trace adds a pacing regression; the old “only three paths” corpus
  count is a dated snapshot.
- [ ] **Verify — transparent sorting** on `3562141459`, `3589454154`,
  `3706286085`, `3708206626`, and `3759507080`; obtain an authored
  `customsortorder` case separately.
- [ ] **Verify — distance/height fog** on Deep Sea Dive and the three Sonic
  scenes above; include Rooftop's scripted day/sunset/night colors.
- [ ] **Verify — spot/tube lighting** on `3562141459` and `3708206626`.
- [ ] **Verify — animated Stage/attached lights and skinned BoostModel** on
  Rooftop and Track Boost Sliding.
- [ ] **Verify — spotlight, directional, and point-shadow bias/edges** on Deep
  Sea Dive, Saturn, and a visible point-shadow case.
- [ ] **Verify — LDR 3D bloom and authored non-white bloom tints.** HDR parity
  remains a separate implementation task.
- [ ] **Verify — RIMLIGHTING/SHADINGGRADIENT require-chain promotion** with
  consistent LIGHTING combos across linked units.
- [ ] **Verify — WEColor/WEMath/WEVector imports** on camera/color-cycle scripts.
- [ ] **Verify — interactive layer/texture-animation APIs, cursor dragging,
  and Sykm dock overlay** using `thisScene.getLayer`/`cursorWorldPosition`.
- [ ] **Verify — web wallpaper long uptimes/CEF child lifetimes**, screensaver
  interaction if applicable, and operation when `--render-scale` is set
  (web/video targets intentionally remain native-sized).

## Recorded regression results

These checkboxes record the explicit past result, not a new test today.
Sources: [Wallpaper Case Studies](wiki/reference/Wallpaper%20Case%20Studies.md),
[Known Issues](wiki/status/Known%20Issues.md),
[corpus findings](../src/WallpaperEngine/Debug/WALLPAPER_FINDINGS.md).

- [x] User-confirm MyGO's clock/date/text stack after parent, UTF-8, DPI, and y-up fixes.
- [x] User-confirm ordinary MyGO eyelid clipping (`3558034522`, 2026-07-30).
- [x] User-confirm Starfall (`2915841260`), Build-a-Kirby (`2963361426`), and
  Reze (`3577990983`) blink/mask fixes (2026-07-30).
- [x] Record Silksong `3563790700`, One Piece `3135984503`, SAO `3463520581`,
  Midra `3294687155`, Halloween `2639381674`, and Gojo `3100265648` as
  puppet format/effect/attachment regression cases.
- [x] Record honeycomb `3758354038`, pinned scene `2665939987`, Last Train
  `2488626583`, depth-parallax `3005674933`, Saturn, Sonic, and OS Waves
  with their specific test purpose and remaining caveats.
- [x] Close the **recorded July hard-failure set** in the 2026-07-30 sweep,
  including the earlier JSON/SIGFPE entries, RG88 upload crashes, dangling
  script registrations, and the Rinn-Flou SIGINT hang. This does not close the
  later August failures or `2444472355`'s longer switch-stress scenario.
- [x] Record live survival/switch results for `3318541129`, `3061226599`,
  `3562141459`, `3737268876`, `3465215190`, `3462491575`, `3417957645`,
  and audio-enabled `3759799379`/`3509578940`/`3696819731`.
- [x] Record the 49-unique-wallpaper cache regression: bounded texture use and
  restoration of baseline FBO state after the scoped-key eviction fix.
- [x] Record all ten missing-workshop-effect wallpapers rendering and the
  deliberately unavailable-effect fixture continuing successfully (2026-07-30).
- [x] Record the **2026-08-19** corpus snapshot: 87 PASS, 219 WARN, 54 FAIL,
  3 SKIP over 363 items; 50 FAILs were classified as shader-only false positives
  and four as runtime failures. Different corpus sizes prevent direct count comparisons.

- [x] Record the **2026-09-07** full corpus result: 85 PASS, 218 WARN, 61 FAIL,
  3 SKIP over 367 items in 27m 31s. All 364 wallpapers were attempted. Preserve
  the 50 shader-only failures separately from 11 runtime/startup/shutdown failures;
  see [the report and serial follow-up](Full%20Corpus%20Validation.md).

## Optional refactors and inherited TODOs

Sources: [Candidate Refactors](wiki/status/Candidate%20Refactors.md),
[TODO Backlog](wiki/status/TODO%20Backlog.md). These are a backlog, not an
instruction to edit the renderer during documentation work.

- [x] Extract strict MDL header parsing; remove duplicate puppet readers,
  section scanners, and the BinaryReader/MemoryStream vertex copy.
- [x] Correct the obsolete “construct fixed regexes per shader” diagnosis:
  fixed patterns are already static; translation disk caching is separate.
- [ ] **Deferred — remove/relocate unlinked `kissfft-WallpaperEngine` and
  `source-parsers` submodules.**
- [ ] **Deferred — replace `Maths::lerp` with `glm::mix` and move single-caller
  random helpers**, then remove `Maths.cpp/h` if unused.
- [ ] **Deferred — deduplicate ObjectParser's repeated `ObjectData` initializer.**
- [ ] **Deferred — migrate the remaining prefix `rfind(...,0)` checks to `starts_with`.**
- [ ] **Deferred — remove unused BinaryReader/MemoryStream includes in CImage.**
- [ ] **Deferred — simplify `getMousePositionLast` plumbing after checking
  CPass and CWeb consumers.**
- [ ] **Deferred — replace vendored CallStack with a suitable native stacktrace API.**
- [ ] **Deferred — fold single-implementation audio interfaces if still worthwhile.**
- [ ] **Deferred — gate CEF behind `WITH_WEB`**, with packaging/build coverage.
- [ ] **Deferred — replace trivial literal regex scans only where worthwhile.**
- [x] Validate shader include filenames within their own line; ignore commented
  includes and support whitespace after `#`. Cover root/nested malformed lines,
  EOF headers, comments, and successful GLSL translation (2026-09-08).
- [x] Ignore empty/malformed combo names and prevent invalid macro definitions
  from metadata, material overrides, and linked units. Keep absent defaults at 0.
- [ ] **Open — implement solid-color texture parameters and resolve the
  first-`#if` include-placement question.** These remain separate shader TODOs.
- [ ] **Open — derive framebuffer formats from authored material settings** as
  part of the proper HDR pipeline.
- [ ] **Audit — resolve ObjectParser constant shader references, grouping, and
  property limits**, separating old comments from current composition support.
- [ ] **Deferred — split monolithic script updates and replace hardcoded scheme
  colors.** The web album-art accent implementation is a different path.
- [ ] **Audit — resolve the script-without-conditions UserSetting TODO.**
  Property scripts already exist; determine the uncovered case.
- [ ] **Audit — validate camera preview defaults** without reverting current
  runtime camera selection.
- [ ] **Audit — review remaining CImage TODOs:** dummy texture sizing, autosize/
  alignment, layer-composite targets, passthrough coordinates/order, effect
  visibility, and paired iteration cleanup.
- [ ] **Deferred — remove stale CText “Phase 1/Phase 2” comments** that contradict
  implemented embedded fonts, effects, and scripting.

## Remaining upstream coverage to assess

Source: [Upstream Feature Inventory](wiki/reference/Upstream%20Feature%20Inventory.md).
These gather runtime-relevant rows that are not already covered above. They are
coverage work, not a promise to implement every upstream feature. A coarse
inventory checkmark alone is not proof of complete runtime parity.

- [ ] **Audit — standalone GIF and WebM behavior**, distinct from implemented
  TEX animation tables and mpv video playback.
- [ ] **Audit — transform-layer and composition-layer variants**, beyond the
  implemented parent graph and subtree isolation.
- [ ] **Audit — mip streaming, detail-first loading, frustum culling, and
  dynamic texture reduction.** Threaded full decode/cache budgeting is separate.
- [ ] **Audit — particle control-point inheritance/rotation, all renderer
  variants, sprite-sheet orientation, and perspective behavior.**
- [ ] **Audit — script particle emission and one-shot animation callbacks:**
  `emitParticles`, `playSingleAnimation`, animation-end events.
- [ ] **Open/deferred — particle lighting, collisions, boids, parent-to-child
  value inheritance/remaps, HSV/color-list initializers, velocity caps, and
  image-layer emission** from the newer upstream particle feature set.
- [ ] **Audit — physics/jiggle bones, IK/bone blending, bone-alpha animation,
  animated depth ordering, puppet depth maps, and character-sheet behavior.**
  Existing skeletal playback alone does not prove runtime physics support.
- [ ] **Open/deferred — puppet rope physics with gravity and wind.**
- [ ] **Audit — model hitboxes/click events, vegetation/fur/chroma shaders,
  and model/GPU-buffer sharing.**
- [ ] **Open/deferred — script-generated models (`createModelData`, `IModelData`).**
- [ ] **Audit — nearest-N light selection, authored texture projection/cookies,
  and light size/radius/falloff controls** beyond the existing four light paths.
- [ ] **Audit — timeline animation coverage across all property types**, plus
  `applyGeneralSettings` and language settings.
- [ ] **Open/deferred — per-frame blend-shape and local-bone manipulation.**
- [ ] **Audit — audio threshold/noise-gate requirements.** The old local gate
  was replaced by the recovered DSP; do not restore it from historical notes.
- [ ] **Open/deferred — sound spatialization.**
- [ ] **Audit — texture/video user-property coverage and playback-rate control.**
- [ ] **Open/deferred — shortcut user properties, horizontal scene flip,
  remaining global color controls, and upstream image-filter presets.**
- [ ] **Audit — per-application playback-rule coverage**, beyond existing
  pause/mute exclusions.
- [ ] **Open/deferred — application-triggered wallpaper/profile selection,
  per-virtual-desktop wallpapers, and VRAM-exhaustion stop behavior.**
- [ ] **Audit — web keyboard/mouse passthrough and monitor groups/splits/bezel
  correction**, beyond `--disable-mouse` and `--screen-span`.
- [ ] **Open/deferred — cloned outputs with source selection and clone flipping.**
- [ ] **Audit — true independent per-monitor mute and monitor-clamped parallax.**
  The inventory contradicts itself on mute; global volume/silence and soundtrack
  deduplication do not prove per-output audio control.
- [ ] **Audit — native playlist randomization parity, time-of-day/day-of-week execution,
  intro entry, freeze/manual advance, duplicates, shared timers, persistence,
  and transition parity.** Existing parsing and basic playback are checked above.

Out of runtime scope: the upstream editor/import tools, Workshop browser and
publishing UI, GUI/property-preset management, Windows-only backends and shell
integration, hardware LED ecosystems, Android companion/export, and executable
application wallpapers. Their source descriptions remain in the inventory;
they are not unchecked renderer tasks. The standalone GUI is also explicitly
out of scope in the project README.

## Documentation and source coverage

- [x] Replace the duplicated/truncated A–J task tables with this checklist.
- [x] Reconcile the five-item `TODDO.md` audit with source: layer/input APIs
  and JSON compatibility implemented; sequence semantics partial; CPU particle
  audio and the Saturn lighting heuristic still open.
- [x] Separate completed code, recorded live results, pending verification,
  local work in progress, and unassessed upstream coverage.
- [x] Collect the project docs below and link the checklist from the wiki's
  entry/status pages; retain technical explanations and historical evidence.
- [x] Supply portable debug/release configure and build presets, ignore their
  output directories, and correct the CLion setup guide's preset names and commands.
  Preset discovery, Debug compilation and Release configuration were checked.
- [ ] **Verify — confirm the IDE configurations work in the actual IDE.**
  File existence does not establish a successful build/debug/profile session.

| Source document | Collected here |
|---|---|
| [Full Corpus Validation](Full%20Corpus%20Validation.md) | Every installed wallpaper, fast-sweep results and failure follow-up |
| [Asset Texture Verification](Asset%20Texture%20Verification.md) | Completed texture tasks, test evidence, wallpaper checks and remaining WIP |
| [Root README](../README.md) | Requirements, build/usage, runtime controls, desktop support, GUI scope |
| [IntelliJ setup](../INTELLIJ_SETUP.md) | Portable presets, build commands and pending IDE verification |
| [Project context](../CLAUDE.md) | Fork scope, wiki workflow, preservation and verification rules |
| [Local terminal runbook](../.claude/lwe-terminal-runbook.md) | Extraction, build/runtime triage, reference/Ghidra workflow |
| [Documentation entry](README.md) | Documentation navigation |
| [Original five-item audit](TODDO.md) | Reconciled API, particles, JSON, and lighting tasks |
| [Wiki index](wiki/index.md) | Concept map and source/status conventions |
| [Current Status](wiki/status/Current%20Status.md) | Implementations, dated corpus result, visual queue |
| [Known Issues](wiki/status/Known%20Issues.md) | Active issues and historical regression closures |
| [TODO Backlog](wiki/status/TODO%20Backlog.md) | Priorities, ports, performance, inherited TODOs |
| [Candidate Refactors](wiki/status/Candidate%20Refactors.md) | Optional cleanup and completed MDL extraction |
| [3D Scene Support](wiki/rendering/3D%20Scene%20Support.md) | Camera/model/material/light/fog/shadow/post state |
| [Camera Path Playback](wiki/rendering/Camera%20Path%20Playback.md) | Parser/evaluator/playback and missing events |
| [Load Performance](wiki/rendering/Load%20Performance.md) | Measured bottlenecks, cache fix, pending profiling |
| [MDL File Format](wiki/rendering/MDL%20File%20Format.md) | Supported sections, scalar tracks, clipping and morph gaps |
| [Parallax System](wiki/rendering/Parallax%20System.md) | Recovered semantics and remaining visual checks |
| [Puppet Warp Pipeline](wiki/rendering/Puppet%20Warp%20Pipeline.md) | Skinning, composition, reference pose and open flags |
| [SceneScript Runtime](wiki/rendering/SceneScript%20Runtime.md) | Lifecycle, builtins, committed alias and remaining API gaps |
| [Shader Translation](wiki/rendering/Shader%20Translation.md) | Compatibility fixes, caching and validation context |
| [Texture File Format](wiki/reference/Texture%20File%20Format.md) | Containers, formats, animation, variants and uploads |
| [Upstream Feature Inventory](wiki/reference/Upstream%20Feature%20Inventory.md) | Remaining runtime coverage and explicit scope exclusions |
| [WE Reference Mining](wiki/reference/WE%20Reference%20Mining.md) | Recovered lighting/HDR/API/global state and research workflow |
| [Wallpaper Case Studies](wiki/reference/Wallpaper%20Case%20Studies.md) | Named regression cases and visual caveats |
| [Audio Capture Silence Investigation](wiki/investigations/Audio%20Capture%20Silence%20Investigation.md) | Closed symptom; historical diagnostic leads retained |
| [Video Texture Memory Growth](wiki/investigations/Video%20Texture%20Memory%20Growth.md) | Completed leak diagnoses, dependency/driver follow-up, parked FPS work |
| [Debugging Workflow](wiki/workflow/Debugging%20Workflow.md) | Hooks, corpus tools, checks, instance hygiene and debug methods |
| [OKF format reference](wiki/references/okf-format-source.md) | Frontmatter and navigation conventions; no implementation task |
| [Debug hook ledger](../src/WallpaperEngine/Debug/README.md) | Shipped telemetry/report/socket hooks |
| [Wallpaper corpus findings](../src/WallpaperEngine/Debug/WALLPAPER_FINDINGS.md) | Live crash/audio/cache evidence and unresolved switch stall |
| [Reversing tools README](../tools/reversing/README.md) | Clipping inspector, shader shim/capture, missing-effect behavior |

When code changes, update its implementation checkbox and leave its separate
verification item open until there is matching evidence. Link a commit/test or
record the dated user/runtime result. Do not turn abandoned experiments,
external fixes not confirmed installed, or API registrations without consumers
into completed end-to-end features.
