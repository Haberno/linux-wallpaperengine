---
type: Status Dashboard
title: Current Status
description: Live dashboard — what works, what needs verification, and every issue or parity gap still open, ranked. Start here each session.
resource: file:///home/admin/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, status, parity, dashboard]
timestamp: 2026-07-08T13:30:00-04:00
---

# Current Status

**Start-of-session page.** Snapshot as of 2026-07-08 (fork main = origin
`Haberno/linux-wallpaperengine`, all work committed and pushed). Update this
page whenever an item closes or a new issue appears; deep detail lives in
[[Known Issues]], [[TODO Backlog]], and the concept pages.

## What works today (high level)

2D scenes (images, effects, puppets/MDL animation, particles, parallax),
3D perspective scenes (camera object, static MDLV models, point/directional
LightingV1, SceneScript property pipeline), text objects (multi-line layout,
alignment/padding, UTF-8, 300-DPI sizing, parent chains, effect chains),
scripting (WEColor/WEMath/WEVector, builtins parity layer, runtime layer API,
enumerateLayers/getLayerByID, applyUserProperties, setTimeout/Interval,
registerAudioBuffers), audio (playback with per-wallpaper dedupe + round-robin
soundtrack rotation across monitors, realtime capture, auto-gained visualizer
spectrum, noise gate), runtime switching with transitions + live `prop`
socket command, video (MPV), honest crash-tolerance on authored JSON drift.

## Verification queue (user eyes/ears needed)

1. **Visualizer bar dynamics** after the auto-gain rework (5b548f4) — bars
   should pump with the music, settle in silence. `WPE_AUDIO_DEBUG=1` logs to
   `/tmp/we-audio-debug.log`.
2. **Media-update segfault** (af82084 port) — needs days of track changes on
   a media-widget wallpaper (Gojo 3100265648) without a crash.
3. **HDR RGBA16F render targets** (3c5145e port) — watch familiar wallpapers
   for brightness/blending shifts; revert is one line in `Render/CFBO.cpp`.
4. **Soundtrack rotation** — two different music wallpapers should alternate
   at end-of-track; same wallpaper twice should play once, no echo.

## Open bugs

1. **MyGO eyelid skin on blink** (3558034522) — clipping-mask system
   (second vertex array, per-bone index ranges, `masks/clipping_mask_*`)
   decoded in [[MDL File Format]] but not consumed by the renderer. The last
   known visual defect on that wallpaper.
2. **AirPods/sink hot-swap under capture** — the 2026-07-08 11:18 crash
   cascade coincided with a default-sink switch; the recorder rebuild path
   got a disconnect fix (b52091d) but hot-swap while capturing is untested.

## Parity gaps — 3D scenes (from [[3D Scene Support]], priority order)

1. **HDR bloom** (`bloomhdr*`): downsample/blur/upsample pyramid, chain fully
   documented in [[WE Reference Mining]]; currently logged and skipped.
2. **Shadow mapping** (`castshadow`): uniforms/templates known
   (`g_LFeature_Shadow*`), needs a shadow atlas pass + REVERSEDEPTH decision.
3. **Spot/tube lights** — parsed as point; exe templates documented.
4. **`transparentsorting`/`customsortorder`** — we render authored order.
5. **`camerafade`** — WE fades in via `materials/util/fade.json`; we pop.
6. **Skinned models in 3D scenes** (`g_Bones`, MDLS/MDLA) — only static
   MDLV; puppet skinning lives in the 2D CImage path.
7. **Fog** (`FOG_DIST`/`FOG_HEIGHT`, `g_Fog*`) — unimplemented.
8. **Camera paths** (`scripts/camera_paths_*.json`, `queuemode`) — resting
   pose only.

## Parity gaps — text (from the CText work)

9. Word wrapping + row limits (`limitwidth`/`maxwidth`, `maxrows`,
   `limituseellipsis`) — long text overflows forever.
10. Text backgrounds (`opaquebackground`/`backgroundcolor`/
    `backgroundbrightness`).
11. `anchor` (dynamic screen anchoring: top/bottomright/...).
12. CJK font fallback when the authored font lacks glyphs (.notdef boxes).
13. Command passes & scene-sampling effects on text objects (none authored
    in the case studies yet).

## Parity gaps — 2D/puppet/parallax

14. Puppet bone constraints (`"tp"`/`"tm"`) — mouse-interactive puppets.
15. Animation layer `blend` weights (currently full strength).
16. Puppet effect chain flags: `clampuvs`, `copybackground`, `solid`.

## Parity gaps — scripting/engine API

17. ILayer expansion: getParent/getChildren/rotateObjectSpace, lookAt/
    getTransformMatrix/setParent, engine.registerAsset (beingsuz c209500,
    bb73165).
18. Camera-transform scripting (`get/setCameraTransforms`) — needs a Camera
    base/override API (beingsuz's Camera.cpp changes).
19. IEngine context queries: isWallpaper/isDesktopDevice/isPortrait/...
    (08f2a41).
20. `input.cursorWorldPosition` is a zero stub; `thisScene.getLayer(name)`
    coverage for overlay scripts.
21. Structural rebuild on property change (visibility-gating properties need
    a scene rebuild; `prop` socket command only fires applyUserProperties) —
    beingsuz 61d3528.

## Remaining beingsuz ports (non-scripting)

22. `--render-scale` supersampling (8947ec6).
23. Control-socket extras: live screenshot (a83347a), live renderscale/
    audiodevice apply.
24. `--audio-device` capture-source override for the recorder.
25. Cold-build switch optimizations (739e9c6).
26. Web/CEF fix batch (1150c42, c700c2f, ed032bb, e4ca729, 0d127f9) — only
    if web wallpapers enter use.

## Code health

See [[Candidate Refactors]] (pruned 2026-07-08) — unreferenced submodules,
CallStack replacement, `WITH_WEB` gate, ShaderUnit regexes, `Maths` deletion,
single-impl audio interfaces, `starts_with` migration.

## Recently closed (2026-07-08, detail in [[Known Issues]])

MyGO clock stack (parent chain, cascade, UTF-8, 300 DPI, y-up), multi-line
text + effect chains (+ regression fixes), JSON type-drift crash, audio
capture drain, automute vs corked streams, per-wallpaper sound dedupe +
rotation, visualizer auto-gain, ~17 beingsuz ports incl. runtime layer API /
enumerateLayers / applyUserProperties + `prop` socket command.
