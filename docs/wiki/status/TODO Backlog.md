---
type: Backlog
title: TODO Backlog
description: Consolidated audit of open bugs, pending verifications, unported fixes, missing features, and code-health items for the linux-wallpaperengine fork.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, backlog, todo, audit]
timestamp: 2026-08-19T00:00:00-04:00
---

# TODO Backlog

> **Consolidated checklist (2026-09-06):** [Project checklist](../../Organized%20all.md). It reconciles these notes with the current
> source and separates implementation, local work in progress, and pending
> verification. Dated measurements below remain historical snapshots.

Originally compiled 2026-07-08 and reconciled with the shipped code and recent
commits on 2026-07-17. Sources include session root-cause analyses
([[Known Issues]]), the beingsuz port audit, the old standalone TODO and OS
Waves investigation, a repo-wide cleanup audit, and an in-code TODO/FIXME scan.

## P0 — Active user-facing bugs

1. ~~CText 2D path ignores the `parent` chain~~ — **fixed 2026-07-08**
   (8cc5ad2): 2D branch composes `resolveWorldMatrix`. Pending visual check
   on MyGO 3558034522.
2. ~~CText origin scripts never tick~~ — **not a bug**: property scripts run
   via `registerProperty` → `queueScript` every frame; `engine.canvasSize`
   is exposed. (Analysis corrected 2026-07-08.)
3. ~~Parent visibility does not cascade~~ — **fixed 2026-07-08** (8123d17):
   `CObject::isVisibleThroughParents` gates CImage and CText.
4. ~~CText renders non-ASCII as mojibake~~ — **fixed 2026-07-08** (d8173ee):
   UTF-8 codepoint decoding + `Testing/Cases/Utf8Text.cpp`. CJK font
   *fallback* (fonts lacking the glyphs show .notdef boxes) remains a P2
   nicety.
5. **Close audio-visualizer parity** — the old hand-tuned auto-gain/noise-gate
   path was replaced 2026-07-16 with the recovered Wallpaper Engine float-stereo
   FFT, frequency mapping, magnitude scaling, normalization envelopes, and
   time smoothing. Remaining:
   - [ ] Recover or independently validate the exact closed-provider 64→32/16
     reduction. Peak-preserving pooling is currently used because the inspected
     executable and SceneScript DLL expose the resolutions but not that provider
     implementation.
   - [ ] Run a live A/B comparison on familiar 16/32/64-band wallpapers against
     Wallpaper Engine, checking peak placement, levels, attack/decay, stereo
     separation, and silence behavior. Confirm the local PulseAudio/PipeWire
     monitor supplies nonzero samples during the comparison.
6. **Verify the media-update segfault fix** — the ported album-art listener
   deregistration (af82084) very likely fixes the Gojo (3100265648)
   track-change crash. Play music, swap wallpapers, change tracks. If it
   still crashes, dig the `notifyMediaUpdate` → `ObjectAdapter::instantiate`
   path. Status: **to be tested** using [[Debugging Workflow]]. → [[Known Issues]].
7. **Resolve Sonic Rooftop material/transparency parity** — recent fixes cover
   authored texture formats/components, alpha-to-coverage, per-project texture
   lookup, malformed uploads, and scaled tangent bases, but the user still sees
   missing-looking floor materials and a translucent bubble/mask around Sonic.
   Capture a clean frame, extract the exact model materials/textures, then walk
   pass-log texture bindings, combos, blend/depth/cull state, and draw order.
   Keep HDR/bloom out of this diagnosis until ordinary material parity is ruled
   out.
- ~~**Mount the stock effect containers**~~ — **fixed 2026-07-26**: every
  `assets/effects/<name>/` container is now mounted at `/` after the assets
  root. The abort is gone from all 15 affected wallpapers; 5 render, 7 advanced
  to a different missing asset, 3 hit a newly reachable SIGSEGV. Full detail and
  the collision analysis in [[Known Issues]].
- ~~**Do not abort on a missing effect dependency**~~ — **fixed and
  runtime-verified 2026-07-30.** Matching `wallpaper64.exe`, a failed effect
  file logs `Failed loading effect`, is omitted from the layer's effect list,
  and parsing continues with the next effect. Ordinary image/text content and
  valid neighboring effects survive; a passthrough layer with no valid effects
  naturally has nothing to draw.
- **DONE 2026-07-26 — package lookups are now case-insensitive.** Wallpapers ship
  Windows-cased paths (`Sounds/` vs the scene's `sounds/`), which the byte-exact
  `PackageAdapter` comparison could never resolve. This was the real cause of
  most of the "absent asset" list: `materials/DK_lights.tex` (2775277607) and
  `models/Rayman.json` (2935233995) were on disk as `dk_lights.tex` and
  `rayman.json`. Fixed 8 of 10 remaining missing-asset aborts and 4 of 8
  SIGSEGVs. Detail in [[Known Issues]].
- **DONE 2026-07-26 — `dependencies` accepts the structured authoring form.**
  `{"id": 104, "index": 0, "type": "collisionmodel"}` threw `type_error.302`,
  and because `parseDependencies` runs in both `ObjectParser::parse`'s try and
  its salvage catch, the wallpaper died. Fixed 2726424530, 2787541254,
  3594400060. Detail in [[Known Issues]].
- **DONE 2026-07-26 — a bad mipmap header truncates the chain instead of
  aborting.** TEXB0004 textures with a variant table interleave the variants'
  payloads between the base image's mipmap levels, so mip1 was read out of
  variant data and its garbage size reached `LZ4_decompress_safe`. Fixed
  2924081598 and 3766299002.
- **DONE 2026-09-07 — decode TEXB0004 patches and select live variants**
  (`f7d97740`). All 41 variants and 486 mip levels in the five-wallpaper corpus
  decoded. GPU minification, authored screenshots and live Mario property
  changes passed. See [Asset Texture Verification](../../Asset%20Texture%20Verification.md).
- **DONE 2026-07-26 — every `*.pkg` in the wallpaper directory is mounted.**
  `setupAssetLocator` hardcoded `scene.pkg`/`gifscene.pkg`; 869945315 names its
  package `audiophile.pkg`, so nothing in it resolved. Was previously mislabelled
  in these docs as absent content.
- **AUDITED 2026-09-07 — Sky is present** in 2742564457 as lowercase
  `models/sky.json`; package lookup resolves it and the wallpaper rendered.
- **DONE 2026-09-07 — stock texture fallbacks** (`7002a065`): texture-only
  preview lookup and PNG + `.tex-json` loading; no preview tree mounts.
- ~~**Triage the remaining hard crashes**~~ — **done and corpus-verified
  2026-07-30.** The RG88 upload crash was fixed by setting the correct unpack
  alignment (fc41164), the dangling ScriptableObject registrations by 9895189,
  and the Rinn-Flou SIGINT hang no longer reproduces. Every wallpaper in the
  recorded crash corpus now renders. The 2026-07-19 and 2026-07-26 failure
  lists are historical evidence only; do not reopen them as current failures.
- **Give the validator's shader dumps the engine preamble** — 61 of 98 FAILs are
  glslang errors (`M_PI`, `TEX8FORMAT`, `input` undeclared/reserved) on items
  that rendered completely normally. The dumps lack the definitions the engine
  injects during translation, so the check currently proves nothing.
- **Determinism flag** (fixed clock/seed/pointer) — not started. CLI flag to
  pin the frame clock to a deterministic timeline, seed particle RNG, and fix
  the mouse position, as the foundation for golden-image testing and a
  side-by-side comparison against Wallpaper Engine.

## P1 — Verify this session's ports (regression sweep)

8. ~~**HDR RGBA16F render targets**~~ — **outdated/reverted 2026-07-14**
   (324039c). Ordinary FBOs are RGBA8 again. Do not verify this as an active
   port; the correct HDR/bloom post pipeline is separately backlogged.
9. **In-process rebuild parity** — switch wallpapers repeatedly over the
   control socket; effects must survive rebuilds (no white/ghosted frames,
   no missing color grading).
10. **Audio visualizer latency** — confirm the 10ms-fragment fix removed the
   ~100ms lag on an audio-reactive wallpaper.
   Basic audio response is user-verified; this item is specifically latency and
   exact native parity, including the inferred 64→32/16 reduction.
11. **Combo require-chain promotion** — check a RIMLIGHTING/SHADINGGRADIENT
    wallpaper (3D scenes) now compiles with LIGHTING promoted; watch shader
    logs for combo mismatches between linked units.
12. **WEColor/WEMath/WEVector imports** — run a wallpaper using a 3D camera
    script or rainbow color-cycle; imports used to throw
    `TypeError: not a function`.
- ~~**Blink/mask regression sweep**~~ — **user-verified 2026-07-30.** The MyGO
  eyelid skin (3558034522), Sonic Frontiers - Starfall (2915841260),
  Build-a-Kirby (2963361426), and Chainsaw Man-Reze (3577990983) now blink
  correctly. Do not leave these in the visual-verification queue.

## Load and switch performance (measured 2026-07-26)

Ranked between P1 and P2: measured, reproducible, and user-visible. Full phase
breakdown, baselines, and the measurement commands are in [[Load Performance]];
only the open work is listed here. The switch regression itself (texture-cache
budget sized below one 4K wallpaper) is **fixed** in `eaa72fc` — do not reopen
it, and do not "improve" it by pinning staged textures.

- **Cache shader translation to disk** — 239 ms average per wallpaper
  (median 229, worst 925): ~175 ms compatibility regex passes, ~52 ms
  glslang→SPIR-V→spirv-cross. Redone from scratch every run. A
  content-addressed disk cache is the remaining work. Include/preprocess,
  compatibility, and GLSL translation memos already exist in memory (source
  checked 2026-09-06); this supersedes the micro-optimization in item 34.
  → [[Shader Translation]].
- **Close the `collectProjectTextures` gaps** — ~9 render-thread
  `texture.sync_load` hits were measured per switch. Source review 2026-09-06
  confirms no `Model` (3D) branch: it walks `Image`, `Particle`, and `Text`.
  It now collects puppet clipping masks too. Trace the remaining stock/shader-
  generated references rather than assuming every `masks/*` path is missed.
- **Saturn 3589454154 spends ~2.4 s parsing `project.json`** on the loader
  thread — request-to-visible 2.5 s vs ~0.9 s for comparable wallpapers. Off
  the render thread, so it delays without stuttering; find out what is
  pathological about that document.
- **One ~1 s frame is unexplained** — `worst_frame_ms` 1788 against a maximum
  measured `switch.apply` stall of 792 ms. Something outside the instrumented
  phases blocks the render thread.
- **Steady-state frame cost has never been measured** — all work so far is load
  cost only.
- **Re-measure on Wayland.** Every figure was taken in a GLFW `--window`, where
  `makeBuildContextCurrent` is unavailable (`VideoDriver.h:81`) so GL upload also
  lands on the render thread. The stall numbers are an upper bound; the desktop
  path should be better and has not been quantified.
- **The 1536 MiB texture budget is a fixed constant**, not derived from
  available VRAM/RAM. A three-way overlap or an 8K wallpaper thrashes again.

## P2 — CText feature completion

13. ~~Multi-line text~~ — **done 2026-07-08** (50a82c4): \n line splitting +
    box layout. Still open: word *wrapping* (`limitwidth`/`maxwidth`),
    `maxrows`/`limitrows`/`limituseellipsis` enforcement.
14. ~~Alignment and padding~~ — **done 2026-07-08** (50a82c4):
    `horizontalalign`/`verticalalign`/`padding` (both authored forms).
    Still open: `anchor` (dynamic screen anchoring).
15. ~~Effects on text objects~~ — **done 2026-07-08** (9c66ad2): CPass-based
    chain in a box-sized FBO via a value-neutral CRenderable host; brightness
    applied at composite. Unsupported: command passes and scene-sampling
    text effects (none seen authored yet).
16. **Text background** — `opaquebackground`, `backgroundcolor`,
    `backgroundbrightness`.
17. **Cleanups** — stale "Phase 1" comment blocks in CText.h/cpp (embedded
    fonts, effects and scripted text are all implemented now); CJK font
    fallback for fonts missing glyphs.

## P3 — Remaining beingsuz ports (deliberately deferred 2026-07-08)

18. ~~Runtime layer API~~ — **ported 2026-07-08** (731cfff):
    createLayer/getLayerIndex/sortLayer. `getCameraTransforms` now returns the
    live eye/center/up/FOV, and `setCameraTransforms` applies a sticky script
    override with non-finite pose rejection. Both halves are implemented
    (source checked 2026-09-06); interactive verification remains.
19. **ILayer API expansion — implemented** (`a1e1a186`, source checked
    2026-09-06): getParent/getChildren/rotateObjectSpace, lookAt/lookAtYaw,
    getTransformMatrix/setParent, attachments, and texture-animation controls.
    enumerateLayers/getLayerByID are also ported (b7fd0d6). Live parity
    verification remains. **`engine.registerAsset` (bb73165) is in progress in the
    working tree** as of 2026-08-19 and may not be kept — do not schedule it
    until it lands or is reverted.
20. **IEngine context queries** — isWallpaper/isDesktopDevice/isPortrait/...
    (08f2a41).
21. ~~applyUserProperties event~~ — **ported 2026-07-08** (29b0e1e), with a
    minimal `prop <screen> <key> <value>` socket command as the live trigger
    (structural rebuilds still deferred with 61d3528).
22. ~~**--render-scale supersampling**~~ — **ported 2026-08-01** with a
    safer current-renderer adaptation: scene/effect targets inherit the scale,
    while the protocol-sized shadow atlas and 1x1 light cookie stay fixed.
    Video/web targets remain native because scaling decoded pixels is not AA.
23. **Control-socket extras** — live screenshot command (a83347a), live
    renderscale/audiodevice apply (53c...), in-process rebuild on any
    bool/combo property change (61d3528) — reconcile with our own socket
    protocol (switching + transitions). *Partial 2026-07-08*: `prop <screen>
    <key> <value>` sets a property live + fires applyUserProperties
    (29b0e1e); structural rebuilds still missing. Also still open:
    `--audio-device` capture-source override (their recorder device param).
24. **Cold-build optimizations** for faster wallpaper switches (739e9c6) —
    complements our transition system. Read [[Load Performance]] before porting:
    our own switch path has since been measured and the dominant cost (texture
    re-decode on the render thread) is already fixed, so compare against current
    numbers instead of assuming this port is still a win.
25. **Web/CEF fixes batch** (1150c42, c700c2f, ed032bb, e4ca729, 0d127f9) —
    **re-scope first.** Web wallpapers are in daily use now and this fork has
    done its own CEF work since (`b404e48f` lifecycle rework, `1d193ef1` helper
    exit, `610720af` percent-decoded asset URLs). Audit overlap before porting
    any of these; the "only if web wallpapers enter use" condition is met and
    no longer a reason to defer. Current web state: [[Current Status]].

Items retained from the old standalone port-review note:

- **Multiple wallpaper audio sources** (faeb889) — reconcile with this fork's
  deliberate one-soundtrack-at-a-time rotation before porting.
- **Web property/audio listener injection** (03cee44, 4bc30e7) — audit what
  the current CEF layer already injects before taking either patch.
- **Property bindings and JSON coercion** (1990748, d1e26cb, 0ccc6c8) —
  partial overlap with the current dynamic-value and type-drift fixes; compare
  behavior, not diffs.
- **Camera/orthographic branch findings** (e890a57, 0f869be, 3b3bec2,
  c2dc15b, 1b0dcc6, 1a8b7a9, 3c8859d) — the camera stack now diverges after
  the parallax and 3D work, so port findings individually.
- **CLI niceties** — contrast/saturation were ported 2026-08-01 in the
  universal final-output composite. `--offset-x/y`, border
  color, and orthographic scaling remain to review. Also review JSON output and a
  properties-file/SIGUSR1 workflow against the existing control-socket
  property command.

## P4 — Missing rendering features (long-standing)

- **3D HDR pipeline** (`hdr`, `bloomhdr*`) — **backlogged 2026-07-17** until
  the Sonic 3D material/script compatibility sweep is complete. The recovered
  downsample/blur/upsample chain remains documented in [[WE Reference Mining]].
  The **LDR** bloom chain now runs on 3D scenes (2026-07-26); what is left here
  is the HDR variant, which is what makes bright sources bloom correctly.

26. **MDL nested clipping composition** — the MDLV0021/23 auxiliary positions,
    draw ranges, and descriptors are parsed, and the ordinary zero-flags,
    single-mask render-target path is implemented and user-verified on MyGO
    3558034522. It produces one mask and three ordered draws in the live
    renderer, and the eyelid skin now looks correct during the blink. Still needed:
    `CLIPPINGCOMPOSE`/intermediate targets for overlapping masks and nonzero
    descriptor flags; unsupported cases currently fall back to the unmasked
    puppet draw.
27. **Puppet bone constraints** (`"tp"`/`"tm"`) — mouse-interactive puppets.
28. ~~**Animation layer blend weights**~~ — **fixed before 2026-07-17**:
    CImage and CModel both read the live authored `blend` value; the shared
    evaluator weights base/non-additive layers and additive deltas, while the
    2D path also applies blend-in/out visibility transitions.
29. ~~Parallax response-curve calibration~~ — **done 2026-07-15**: exact
    delay response, half-canvas magnitude, root depth ownership/default, and
    shader input semantics recovered from `wallpaper64.exe`.
    → [[Parallax System]].
30. **Puppet effect chain flags** — `clampuvs`, `copybackground`, `solid`
    unhandled on puppet objects. → [[Puppet Warp Pipeline]].

- ~~**Camera-path follow-up**~~ — **done 2026-07-16**: both legacy transforms
  and current curve channels play back, sequence/random queues advance without
  immediate random repeats, visible camera objects select their path source,
  FOV/zoom animate, and the recovered 0.5-second fade runs at every boundary.
  Only three installed wallpapers contain non-empty path data; 24 additional
  path references are empty and intentionally remain static.
- ~~**Transparent 3D model sorting**~~ — **done 2026-07-16**: scene-level
  `transparentsorting` and `customsortorder` are parsed; model render slots are
  grouped opaque/translucent/additive and blended models sort back-to-front in
  camera space without moving non-model dependencies. Live verification remains
  on the five affected installed scenes (3562141459, 3589454154, 3706286085,
  3708206626, 3759507080); none of the installed scenes use `customsortorder`.
- ~~**3D distance/height fog**~~ — **done 2026-07-16**: native scene combos,
  material `FOG_COMPUTED` gating, stock uniform packing, and squared density
  ramps are supported. Scene-level property scripts update fog values live.
  Visual verification remains on 3562141459, 3706286085, 3708206626, and
  3759507080.
- ~~**Spot/tube LightingV1 paths**~~ — **done 2026-07-16**: native light type
  IDs/defaults, cone cosine packing, dynamic uniforms, spotlight cookie, and
  tube segment endpoint math are implemented. Visual verification remains on
  3562141459 (2 spots) and 3708206626 (15 tubes).
- ~~**3D skeletal animation and MDAT attachments**~~ — **implemented
  2026-07-17** (dbcc722, 92049ac, 0356a16): shared MDLS/MDLA parsing and pose
  evaluation, GPU `g_Bones`, skinned shadow casters, and live nested attachment
  transforms. Visual verification remains on Sonic Rooftop's animated `Stage`
  and attached tube lights plus Track Boost Sliding's BoostModel.
- ~~**Auxiliary MDLV submesh flags**~~ — **fixed 2026-07-17** (cfdcc73):
  `0x400` is accepted independently from bit 0's 32-bit-index selector;
  unsupported bits remain fatal.
- ~~**Spot, directional, and point shadow maps**~~ — **implemented
  2026-07-17** (c936afe, bcad160, fc68d89): comparison-depth atlas, spotlight
  projection, stable three-cascade directional projections, six-face point
  blocks, native shader selection/sampling, and skinned model casters. Live
  bias/edge parity is still a verification item.
- ~~**Authored material/texture metadata compatibility**~~ — **implemented
  2026-07-17** (6985bbe, a6166cb, 67ad630): alpha-to-coverage,
  format/component combos, user textures, asset-scoped texture cache keys,
  payload validation, and normalized tangent basis. The broader Sonic visual
  mismatch remains P0 because these fixes did not completely close it.
- ~~**Orthographic camera auto-size**~~ — **implemented**: `CScene` derives a
  projection extent from image origins/sizes and falls back to the captured
  output size. The old OS Waves note describing an empty TODO was outdated.
- **Passthrough images without effects** — `CImage::setup` still returns
  early, so OS Waves' `cust` layer may be absent. Recover the native
  passthrough behavior before removing the guard.
- **Model/image `autosize` behavior** — the field is parsed and dumped but has
  no renderer consumer. Determine how it differs from `fullscreen` and camera
  auto-size, then implement it with an OS Waves regression check.

## P5 — Code health (over-engineering audit, 2026-07-08)

31. Deregister/relocate unreferenced submodules `src/External/kissfft-WallpaperEngine`
    and `src/External/source-parsers` (zero CMake references; keep as
    reference material outside the build tree if wanted).
32. Replace vendored `Debugging/CallStack.{cpp,h}` (392 lines incl. Windows
    paths) with `backtrace_symbols` + `__cxa_demangle` (~25 lines) or C++23
    `std::stacktrace`.
33. Gate CEF behind a `WITH_WEB` CMake switch — currently downloaded and
    linked unconditionally.
34. ShaderUnit regexes — **mostly already done, and mis-scoped.** Measured
    2026-07-26: the fixed patterns are already `static` (48 statics vs 9
    name-interpolated locals that cannot be hoisted as-is). The remaining 175 ms
    is the compatibility *passes* running at all, not regex construction, so the
    payoff is a disk cache (see the load-performance section above), not this
    micro-optimization. Plain scans for the trivial literal one-liners are still
    a small, optional win.
35. Fold single-implementation interfaces `AudioDriver` (SDL only) and
    `AudioPlayingDetector` (PulseAudio only).
36. Delete `Maths.{cpp,h}` — `lerp` = `glm::mix`; random helpers inline into
    their only caller (CParticle).
37. Deduplicate the two identical `ObjectData{...}` blocks in
    `ObjectParser.cpp:35,62`.
38. Remove `getMousePositionLast` plumbing from CScene (single consumer
    CPass can diff positions itself).
39. `rfind("x", 0) == 0` → C++20 `starts_with` (~9 sites).
40. Drop dead `BinaryReader.h`/`MemoryStream.h` includes in `CImage.cpp`
    (orphaned by the MdlParser extraction).
- **Latent out-of-range read in `AssetLocator::shader`** (`AssetLocator.cpp:16-19`,
  found 2026-07-26, deliberately not fixed): line 16 dereferences
  `shader.begin ()` without comparing against `end ()`, and line 17 dereferences
  the second component unchecked — only the third component is guarded by
  `++it != end ()`. A shader path of exactly `workshop` or `workshop/<id>`
  therefore reads past the end and aborts under libstdc++ assertions. No
  authored wallpaper produces such a path today, which is why it has never
  fired. One `it != end ()` guard per dereference fixes it.
- ~~**`tools/validate-corpus.py` baseline-noise filter**~~ — **done 2026-07-26**:
  `BENIGN_ERRORS` (the `Failed to initialize GLEW: No GLX display` and
  `GLFW error 65548` lines this machine's Wayland/GLFW stack logs on every run)
  is counted from each item's `log.txt` and subtracted from the `log.error`
  counter before classification — `health.json` caps its detail samples, so the
  count cannot be filtered from the report alone. Full corpus after the fix:
  **54 PASS / 189 WARN / 98 FAIL / 3 SKIP over 341 items** (the corpus has
  roughly doubled since the ~170-item run of 2026-07-19, so raw counts are not
  comparable to it).

## P6 — Notable inherited in-code TODOs (scan 2026-07-08)

41. ShaderUnit: malformed/commented include handling and empty/invalid combo
    names are fixed with regression coverage (2026-09-08). Solid-color texture
    creation and the first-`#if` include-placement question remain open.
42. FBOProvider: derive FBO format from the material string (12) — handle this
    as part of the real HDR/render-target format pipeline, not the reverted
    global RGBA16F experiment.
43. ObjectParser: parse constant shader value refs (298); re-evaluate
    "objects containing objects" grouping (95); parse property limits
    (`Object.h:627`).
44. ScriptEngine: split monolithic JS updates into meaningful ones (246);
    stop hardcoding scheme colors (797).
45. **InputObject cursor APIs — implemented.** `cursorLeftDown` reads the
    real mouse input; world-space cursor projection also exists (source
    checked 2026-09-06). Live interaction remains a verification task.
46. UserSetting: script-driven values without conditions (`UserSetting.h:25`).
47. WallpaperParser: validate camera preview defaults (41).
48. CImage: 13 inherited TODOs (passthrough texcoords, effect visibility,
    zip-iteration, etc.) — several sit next to freshly ported code; sweep
    while context is warm.

## P7 — Wiki/backlog maintenance

49. ~~Prune [[Candidate Refactors]] after the MdlParser rewrite~~ — **done
    2026-07-17**: resolved parser cleanup is recorded separately and the FFT
    item correctly identifies the unlinked `kissfft-WallpaperEngine` copy.
50. ~~Add a [[Wallpaper Case Studies]] entry for MyGO's clock/audio bar~~ —
    **done**; the case study records the CText and audio fixes. The ordinary
    eyelid clipping path is implemented and user-verified; only the
    nested-composition edge case remains.
