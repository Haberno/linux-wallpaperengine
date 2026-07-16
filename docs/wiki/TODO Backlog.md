---
type: Backlog
title: TODO Backlog
description: Consolidated audit of open bugs, pending verifications, unported fixes, missing features, and code-health items for the linux-wallpaperengine fork.
resource: file:///home/admin/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, backlog, todo, audit]
timestamp: 2026-07-16T00:00:00-04:00
---

# TODO Backlog

Compiled 2026-07-08 from: session root-cause analyses ([[Known Issues]]),
the beingsuz port audit (see memory `beingsuz-branch-ports`), a repo-wide
over-engineering audit, and an in-code TODO/FIXME scan. Ordered by priority.

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
   path. → [[Known Issues]].

## P1 — Verify this session's ports (regression sweep)

7. **HDR RGBA16F render targets** — global FBO format change; sweep familiar
   wallpapers (Last Train 2488626583, MyGO, 3D test 3589454154, Starscape
   3047596375) for blending/brightness shifts. Revert is one line in
   `Render/CFBO.cpp` if it regresses.
8. **In-process rebuild parity** — switch wallpapers repeatedly over the
   control socket; effects must survive rebuilds (no white/ghosted frames,
   no missing color grading).
9. **Audio visualizer latency** — confirm the 10ms-fragment fix removed the
   ~100ms lag on an audio-reactive wallpaper.
10. **Combo require-chain promotion** — check a RIMLIGHTING/SHADINGGRADIENT
    wallpaper (3D scenes) now compiles with LIGHTING promoted; watch shader
    logs for combo mismatches between linked units.
11. **WEColor/WEMath/WEVector imports** — run a wallpaper using a 3D camera
    script or rainbow color-cycle; imports used to throw
    `TypeError: not a function`.

## P2 — CText feature completion

12. ~~Multi-line text~~ — **done 2026-07-08** (50a82c4): \n line splitting +
    box layout. Still open: word *wrapping* (`limitwidth`/`maxwidth`),
    `maxrows`/`limitrows`/`limituseellipsis` enforcement.
13. ~~Alignment and padding~~ — **done 2026-07-08** (50a82c4):
    `horizontalalign`/`verticalalign`/`padding` (both authored forms).
    Still open: `anchor` (dynamic screen anchoring).
14. ~~Effects on text objects~~ — **done 2026-07-08** (9c66ad2): CPass-based
    chain in a box-sized FBO via a value-neutral CRenderable host; brightness
    applied at composite. Unsupported: command passes and scene-sampling
    text effects (none seen authored yet).
15. **Text background** — `opaquebackground`, `backgroundcolor`,
    `backgroundbrightness`.
16. **Cleanups** — stale "Phase 1" comment blocks in CText.h/cpp (embedded
    fonts, effects and scripted text are all implemented now); CJK font
    fallback for fonts missing glyphs.

## P3 — Remaining beingsuz ports (deliberately deferred 2026-07-08)

17. ~~Runtime layer API~~ — **ported 2026-07-08** (731cfff):
    createLayer/getLayerIndex/sortLayer. Camera-transform scripting
    (getCameraTransforms/setCameraTransforms) NOT taken — needs their Camera
    base/override API (their Camera.cpp +49), tracked here instead.
18. **ILayer API expansion** — getParent/getChildren/rotateObjectSpace
    (c209500), lookAt/getTransformMatrix/setParent, engine.registerAsset
    (bb73165). ~~enumerateLayers/getLayerByID~~ ported (b7fd0d6).
19. **IEngine context queries** — isWallpaper/isDesktopDevice/isPortrait/...
    (08f2a41).
20. ~~applyUserProperties event~~ — **ported 2026-07-08** (29b0e1e), with a
    minimal `prop <screen> <key> <value>` socket command as the live trigger
    (structural rebuilds still deferred with 61d3528).
21. **--render-scale supersampling** (8947ec6) — quality/antialias knob.
22. **Control-socket extras** — live screenshot command (a83347a), live
    renderscale/audiodevice apply (53c...), in-process rebuild on any
    bool/combo property change (61d3528) — reconcile with our own socket
    protocol (switching + transitions). *Partial 2026-07-08*: `prop <screen>
    <key> <value>` sets a property live + fires applyUserProperties
    (29b0e1e); structural rebuilds still missing. Also still open:
    `--audio-device` capture-source override (their recorder device param).
23. **Cold-build optimizations** for faster wallpaper switches (739e9c6) —
    complements our transition system.
24. **Web/CEF fixes batch** (1150c42, c700c2f, ed032bb, e4ca729, 0d127f9) —
    only if web wallpapers enter use.

## P4 — Missing rendering features (long-standing)

25. **MDL clipping-mask system** — MyGO eyelid "skin" (second vertex array,
    per-bone index ranges, `masks/clipping_mask_*` refs between MDLV/MDLS).
    Data decoded in [[MDL File Format]]; renderer support pending.
26. **Puppet bone constraints** (`"tp"`/`"tm"`) — mouse-interactive puppets.
27. **Animation layer blend weights** — layers currently apply at full
    strength.
28. ~~Parallax response-curve calibration~~ — **done 2026-07-15**: exact
    delay response, half-canvas magnitude, root depth ownership/default, and
    shader input semantics recovered from `wallpaper64.exe`.
    → [[Parallax System]].
29. **Puppet effect chain flags** — `clampuvs`, `copybackground`, `solid`
    unhandled on puppet objects. → [[Puppet Warp Pipeline]].

## P5 — Code health (over-engineering audit, 2026-07-08)

30. Deregister/relocate unreferenced submodules `src/External/kissfft-WallpaperEngine`
    and `src/External/source-parsers` (zero CMake references; keep as
    reference material outside the build tree if wanted).
31. Replace vendored `Debugging/CallStack.{cpp,h}` (392 lines incl. Windows
    paths) with `backtrace_symbols` + `__cxa_demangle` (~25 lines) or C++23
    `std::stacktrace`.
32. Gate CEF behind a `WITH_WEB` CMake switch — currently downloaded and
    linked unconditionally.
33. ShaderUnit: stop constructing 26 `std::regex` per unit compile — static
    const for fixed patterns, plain scans for trivial literals.
34. Fold single-implementation interfaces `AudioDriver` (SDL only) and
    `AudioPlayingDetector` (PulseAudio only).
35. Delete `Maths.{cpp,h}` — `lerp` = `glm::mix`; random helpers inline into
    their only caller (CParticle).
36. Deduplicate the two identical `ObjectData{...}` blocks in
    `ObjectParser.cpp:35,62`.
37. Remove `getMousePositionLast` plumbing from CScene (single consumer
    CPass can diff positions itself).
38. `rfind("x", 0) == 0` → C++20 `starts_with` (~9 sites).
39. Drop dead `BinaryReader.h`/`MemoryStream.h` includes in `CImage.cpp`
    (orphaned by the MdlParser extraction).

## P6 — Notable inherited in-code TODOs (scan 2026-07-08)

40. ShaderUnit: malformed `#include` handling (143,182), empty combos (551),
    solid-color texture creation (660), first-`#if`-block question (302).
41. FBOProvider: derive FBO format from the material string (12) — now
    interacts with the RGBA16F change.
42. ObjectParser: parse constant shader value refs (298); re-evaluate
    "objects containing objects" grouping (95); parse property limits
    (`Object.h:627`).
43. ScriptEngine: split monolithic JS updates into meaningful ones (246);
    stop hardcoding scheme colors (797).
44. InputObject: unimplemented input APIs (13, 34).
45. UserSetting: script-driven values without conditions (`UserSetting.h:25`).
46. WallpaperParser: validate camera preview defaults (41).
47. CImage: 13 inherited TODOs (passthrough texcoords, effect visibility,
    zip-iteration, etc.) — several sit next to freshly ported code; sweep
    while context is warm.

## P7 — Wiki/backlog maintenance

48. Prune [[Candidate Refactors]]: four items resolved by the MdlParser
    rewrite (strict `parseMdlvHeader`, `puppetRead*` dupes, section-scan
    lambdas, BinaryReader vertex copy); fix the kissfft item's direction
    (the *WE fork* is the unlinked copy, vanilla is linked).
49. Add a [[Wallpaper Case Studies]] entry for 3558034522 (clock/audio-bar)
    once fixed, with before/after evidence.
