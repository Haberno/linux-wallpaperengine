---
type: Status Dashboard
title: Current Status
description: Live dashboard — what works, what needs verification, and every issue or parity gap still open, ranked. Start here each session.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, status, parity, dashboard]
timestamp: 2026-08-19T00:00:00-04:00
---

# Current Status

> **Consolidated checklist (2026-09-06):** [Project checklist](../../Organized%20all.md). It reconciles these notes with the current
> source and separates implementation, local work in progress, and pending
> verification. Dated measurements below remain historical snapshots.

**Detailed snapshot from 2026-08-19.** For the source-reviewed state at
`9edaa4dc` on 2026-09-06, use the checklist above. The recorded fork main =
`Haberno/linux-wallpaperengine` at `f826719a`. The debug/telemetry/health-report
tooling, `tools/validate-corpus.py` (eb92a5f), and the load-performance work
(dc11774, eaa72fc) are all committed and pushed. Update this page whenever an
item closes or a new issue appears; deep detail lives in [[Known Issues]],
[[TODO Backlog]], and the concept pages.

> **Working tree was dirty at this snapshot.** `CText.cpp`, `CPass.cpp`, and
> three `Scripting/` files carry uncommitted `engine.registerAsset` work that
> may or may not be kept — see *In flight* below. The binary these numbers were
> measured with (`build-new/`) includes it.

Status terms used here: **implemented/fixed** means the code and focused tests
exist; **verified** means the live result was also confirmed; **pending visual
verification** is not an open implementation gap; **outdated/reverted** means
the old entry must not be used as current guidance.

## Latest full-corpus check (2026-09-07)

All 364 installed wallpapers were tested in 27m 31s: **85 PASS, 218 WARN,
61 FAIL**, plus three non-wallpaper asset packs SKIP. Fifty failures were
shader-check-only and eleven included a runtime/startup/shutdown problem.
The four historical scene/fatal cases below reproduced. New findings include
six CEF shutdown failures and a 90-second startup timeout on 3D Snowflakes.
See [Full Corpus Validation](../../Full%20Corpus%20Validation.md) for every ID
and serial follow-up; the August figures below remain a historical snapshot.

Serial follow-up cleared the Snowflakes timeout (74 frames after 91.3 seconds)
and reproduced all six CEF shutdown failures. The reviewed totals are
**85 PASS, 219 WARN, 60 FAIL**: 50 shader-only and 10 runtime failures.

## What works today (high level)

2D scenes (images, effects, puppets/MDL animation, particles, parallax),
3D perspective scenes (camera objects/path queues, static and GPU-skinned MDLV
models, live MDAT attachments, transparent sorting, fog,
point/directional/spot/tube LightingV1, spotlight/cascaded-directional/
omnidirectional-point shadows, SceneScript property pipeline), text objects (multi-line layout,
alignment/padding, UTF-8, 300-DPI sizing, parent chains, effect chains),
scripting (WEColor/WEMath/WEVector, builtins parity layer, runtime layer API,
enumerateLayers/getLayerByID, applyUserProperties, setTimeout/Interval,
registerAudioBuffers), audio (playback with per-wallpaper dedupe + round-robin
soundtrack rotation across monitors, realtime float-stereo capture, native-compatible
64-band FFT mapping and scene-spectrum smoothing), runtime switching with transitions + live `prop`
socket command, video (MPV), honest crash-tolerance on authored JSON drift,
web wallpapers (CEF 150, in daily use), `--render-scale` supersampling and
final-output color grading (`--contrast`/`--saturation`), and single-mask
puppet clipping.

## Changes since the 2026-07-30 snapshot

32 commits, grouped by subsystem. Kept here rather than in a dated log file per
the [[index]] maintenance rule; fold anything that becomes durable knowledge
into the relevant concept page.

**Shader translation** → [[Shader Translation]]
- `33ece832` **inject `DECLARE_SAMPLER2D_PARAMETER` / `MAKE_SAMPLER2D_ARGUMENT`.**
  WE's shipped `model_{fragment,vertex}_v1.h` use them; the assets define them
  nowhere and the real engine injects them. Every shader reaching
  `ApplyReflection` / `ApplyMorphPosition*` had been failing, surfacing as the
  cascading C5145 `must write to gl_Position`. Largest single-commit change to
  what renders in this batch.
- `1be9f6bf`, `8109c6c8` include handling — macros stay at the authored include
  site while declarations/helpers stay in `m_includes`, and relocated macros
  stay visible.
- `e76307ec` preserve combo-conditional texcoord width variants instead of
  widening an inactive `vec2` declaration.
- `67c483bb` bind `g_Frametime` for effect passes. Unbound it defaulted to zero
  and froze every feedback simulation (cursor ripple and friends) outright.

**Camera** → [[Camera Path Playback]]
- `878a4ca4` proper camera path handling — substantial `CameraPath` /
  `CameraPathParser` / `CScene` rework with expanded test cases.
- `f826719a` **scripts never received the startup user-property dispatch**, so
  script-gated cameras (2244339517) silently ran their default branch. Added
  `dispatchAllUserProperties`. → [[SceneScript Runtime]]
- `c8d0e5cd` play authored 2D opening animations; `2675ee09` bind projection to
  the active layer.

**Animation / puppet** → [[Puppet Warp Pipeline]]
- `ba29823e` resolve additive MDL clips from the **bind pose**, not frame zero.
- `1e23f676` share one puppet reference pose across layers.
- `6040e79e` compose additive bones per component.
- `2c1ba783` sample keyframed transforms.
- `c6536551` decode and render puppet clipping masks (the single-mask path).

**Composition / render targets**
- `62f8a680` isolate composition layer subtrees — per-image `FBOProvider`,
  `_rt_FullFrameBuffer` slot aliasing, and child alpha preserved so a parent can
  blend the group.
- `7bf76dff` preserve flat composition stack effects.
- `d3c781d0` restrict mipmap generation to the dedicated reflection target.
- `5790f65d` `--render-scale` supersampling; `b9a8b8aa` final-output color
  grading (`--contrast`, `--saturation`).

**Text**
- `d9d555c0` match native glyph alignment; `ae0ac9c0` match native effect line
  boxes; `c4b06e41` reserve padding in effect surfaces.

**Particles**
- `ff29820c` honor instance simulation rate; `94418161` fade rope trails with
  the live particle.

**Web / CEF** → see *Web wallpapers* below
- `610720af` Chromium percent-encodes spaces and non-ASCII in filenames during
  URL canonicalization; `WPSchemeHandler` now decodes before hitting the asset
  loader.

**Video / textures / platform**
- `aa1336f2` keep replaced monitor textures updating — non-owning provider
  registry retired only when the last scene reference drops, so a cache-key
  replacement can't freeze a still-rendered video, and libmpv renders once per
  driver frame rather than once per output.
- `fc41164f` RG88 unpack alignment; `4e32d4f3` restore Wayland background cursor
  tracking; `9895189b` drop queued scripts when their object is destroyed.

**Parser / tooling**
- `55d53526` skip unavailable image effects; `b04eba86` validate explicit corpus
  paths; `7417ee19` record clipping/effect behavior in `tools/reversing/README.md`.

## In flight (uncommitted, may not be kept)

Reviewed 2026-09-07: the `{file}` handle in `EngineObject`/`SceneObject` is
still incomplete: `precache`, actual registration/loading and asset lifetime
remain. `CText`'s effect-surface publication needs plain-text and resize/lifetime
work. `CPass`'s user-texture property lookup covers only part of the binding path.
These and temporary script tracing are left uncommitted.

Finished portions were isolated into commits: the `thisObject = thisLayer`
alias (`181c752e`), live `Float32Array` audio construction (`dc60c13e`), and
shader-default sampler format metadata (`b189b7ad`). A real SceneScript fixture
verified the layer alias and shared 16/32/64-band audio views. See
[Asset Texture Verification](../../Asset%20Texture%20Verification.md) for the focused test evidence.

## Parity/debug tooling (added 2026-07-19)

Env-gated, zero-cost-when-disabled hooks in `src/WallpaperEngine/Debug/`
(ledger: `src/WallpaperEngine/Debug/README.md`) plus a driver script:
unknown-authored-JSON-key telemetry (`WPE_JSON_TELEMETRY`), a per-run health
report of error/exception counts, frame timing and load-phase timings
(`WPE_HEALTH_REPORT`), and
`tools/validate-corpus.py`, which runs the whole local workshop corpus
through the engine and glslang and classifies each item PASS/WARN/FAIL. See
[[Debugging Workflow]]. The classifier counted this machine's GLFW/GLEW startup
noise as wallpaper errors, so no item could PASS; fixed 2026-07-26.

**Corpus results, 2026-08-19** (363 items, `f826719a` + the uncommitted
registerAsset work, `build-new/`):

| | PASS | WARN | FAIL | SKIP | items |
|---|---|---|---|---|---|
| 2026-07-30 (pre-`33ece832`) | 54 | 189 | 98 | 3 | 341 |
| **2026-08-19** | **87** | **219** | **54** | **3** | **363** |

FAIL nearly halved while the corpus grew — almost entirely the shader prelude
fix (`33ece832`). Counts are not comparable across runs (~170 items on
2026-07-19 → 341 → 363), so read the ratio and the FAIL reasons, not the totals.

**All but four FAILs are the known glslang false positive** (50 of 54): dumped
units compiled without the engine's injected preamble. The real failures are:

- `1979606285` Playstation 2 Clock (3D) — SIGSEGV
- `3644280276` A Solitary Reflection [4K] (2D) — SIGSEGV
- `3768356757` Stratospheric Twilight [4K] (2D) — SIGSEGV
- `764162681` Jake (2D) — fatal exception, exits code 1, no frames

These are **new since the 2026-07-30 sweep**, which recorded zero remaining
hard crashes. See the `applyUserProperties` entry under *Open bugs*.

The determinism-flag task (fixed clock/seed/pointer, for golden-image
comparison against Wallpaper Engine) is still pending.

## Load performance (measured 2026-07-26)

Startup and switch phases are now instrumented through `WPE_HEALTH_REPORT`
(dc11774). Cold start on a heavy 4K wallpaper is ~2 s, split roughly evenly over
texture read+decode, GL upload, and the scene build. Live switches used to stall
the render thread for up to 2.3 s; that was the texture cache budget being
smaller than one 4K wallpaper, so each switch evicted its own freshly staged
textures and the scene build re-decoded them inline. Fixed in eaa72fc (budget
sized for two wallpapers), stall now 0.35–0.79 s. Remaining open work — shader
translation has no disk cache, `collectProjectTextures` misses four asset
classes, Saturn's 2.4 s `project.json` parse, one unexplained ~1 s frame, and
steady-state cost never measured — is listed in [[TODO Backlog]]; full numbers
and method in [[Load Performance]].

## Web wallpapers (CEF)

No longer a dormant path — this is what the live desktop runs. State as of
2026-08-19:

- CEF 150 (`CMakeLists.txt`), downloaded and linked **unconditionally**; the
  `WITH_WEB` gate is still only a proposal in [[Candidate Refactors]].
- `b404e48f` (2026-07-09) reworked the CEF lifecycle so web wallpapers boot and
  survive switching; `1d193ef1` made helper processes exit without unwinding
  main.
- `610720af` (2026-08-18) fixed wallpapers whose filenames contain spaces or
  non-ASCII: Chromium percent-encodes them during URL canonicalization and the
  asset loader needs the real name back.
- `CWeb.cpp` derives a saturation/brightness-weighted accent color from album
  art for page theming (WE's `primaryColor`).
- Four `WPE_CEF_*` env knobs (Ozone, ANGLE, extra switches, no in-process GPU)
  are the first thing to try on a black or non-booting web wallpaper — see
  [[Debugging Workflow]].

Untested/unknown: web + screensaver interaction, web wallpapers under
`--render-scale` (video and web targets deliberately stay native), and whether
the CEF child tree survives long uptimes — `tools/lwe-monitor` watches that.

## Verification queue (user eyes/ears needed)

1. **Visualizer parity** after the native DSP port — verify familiar 16/32/64-band
   wallpapers against Wallpaper Engine. The recovered 64-band FFT, stereo split,
   normalization envelopes, and time smoothing are implemented; 64→32/16 peak
   reduction is inferred at the closed provider boundary. Audio-reactive
   wallpapers responding to music is now user-verified, but that confirms
   capture health—not exact peak placement or level parity. `WPE_AUDIO_DEBUG=1`
   logs to `/tmp/we-audio-debug.log`.
2. **Media-update segfault — TO BE TESTED** (af82084 port) — needs repeated
   track/album-art changes on the Gojo media-widget wallpaper without a crash,
   including a track change after switching away from it. See
   [[Debugging Workflow]].
3. **Soundtrack rotation** — two different music wallpapers should alternate
   at end-of-track; same wallpaper twice should play once, no echo.
4. **Camera-path playback** — visually compare the three installed wallpapers
   with non-empty path data: Rayquaza 3045001236 (legacy transforms), Pokemon
   Deep Sea Dive 3562141459 (curve paths/random queue), and Ocarina of Time
   3737268876 (visibility-switched camera objects). The other 24 installed
   path references contain empty `paths` arrays and do not animate or fade.
5. **Transparent 3D model sorting** — compare the five installed scenes that
   enable it: Pokémon Deep Sea Dive 3562141459, Saturn 3589454154, Sonic AKIBA
   Boost Run 3706286085, Sonic Rooftop Boost Run 3708206626, and Sonic
   Frontiers Track Boost Sliding 3759507080. Check translucent geometry while
   the camera or objects move. No installed scene enables `customsortorder`, so
   its authored-order override still lacks a live reference case.
6. **3D fog** — compare Pokemon Deep Sea Dive 3562141459, Sonic AKIBA Boost
   Run 3706286085, Sonic Rooftop Boost Run 3708206626, and Sonic Frontiers
   Track Boost Sliding 3759507080. Rooftop is the key dynamic case: its
   distance-fog color follows a day/sunset/night SceneScript.
7. **Spot/tube LightingV1** — compare Pokémon Deep Sea Dive 3562141459 (two
   animated `lspot` objects) and Sonic Rooftop Boost Run 3708206626 (fifteen
   scripted `ltube` objects). Pokémon exercises the complete spot path. Sonic
   exercises the tube shader path and the animated `Stage` attachment path.
8. **3D skeletal animation + attachments** — verify Sonic Rooftop Boost Run's
   animated `Stage` and attached tube lights, plus Sonic Frontiers Track Boost
   Sliding's skinned BoostModel. GPU `g_Bones`, the shared MDLS/MDLA evaluator,
   nested MDAT resolution, and auxiliary submesh flags are implemented.
9. **3D shadows** — verify spotlight shadows in Pokémon Deep Sea Dive,
   cascaded directional shadows on Saturn, and a visible point-light shadow
   scene. The shared depth atlas and all three projection/sampling paths are
   implemented; exact live bias/edge parity remains a visual check.
10. **Sonic material/texture parity** — recheck Sonic Rooftop Boost Run and
    Track Boost Sliding after the authored texture-format/component combos,
    alpha-to-coverage, per-project texture scoping, upload validation, and
    scale-free tangent basis fixes. The user still reported missing-looking
    floor materials and a bubble/transparent-mask appearance around Sonic, so
    this is not considered visually fixed.
11. **Default-sink/AirPods hot-swap — TO BE TESTED** — while an audio-reactive
    wallpaper and music are active, switch away from and back to AirPods and
    confirm capture resumes without a crash or permanently flat response. See
    [[Debugging Workflow]].
12. **Live 4K switch feel after the texture-cache fix (eaa72fc)** — switch
    between several heavy 4K wallpapers on the Wayland desktop and confirm the
    transition looks smooth with no visible hitch or blank/ghosted frame. Then
    watch RSS over 4–5 switches (`M_ARENA_MAX=2` cap plus the deferred
    `malloc_trim`); the measured window was 594–1234 MiB in a 640×360 test
    process. All timings so far came from a GLFW window, not the real
    compositor path — see [[Load Performance]].

## User-verified rendering fixes (2026-07-30)

- ~~**MyGO eyelid skin**~~ (3558034522) — the ordinary single-mask clipping
  renderer is implemented and the blink appearance was confirmed good.
- ~~**Sonic Frontiers - Starfall blink mask**~~ (2915841260) and
  ~~**Build-a-Kirby blink mask**~~ (2963361426) — confirmed good after matching
  Wallpaper Engine's effect-mask behavior.
- ~~**Chainsaw Man-Reze blink**~~ (3577990983) — confirmed good after the
  MDLA clip-boundary/parser correction.
- ~~**Recorded crash corpus**~~ — all former hard-crash and hang entries render
  as of the 2026-07-30 sweep. Keep their signatures in [[Known Issues]] as
  historical regression evidence, not active bugs.

## Open bugs

1. **MDL nested clipping composition** — the parser and ordinary single-mask
    renderer path are implemented. Descriptors with nonzero flags or targets
    requiring multiple masks still fall back to the unmasked puppet draw.
    MyGO 3558034522 reaches the implemented path (`masks=1`, `draws=3`) and its
    blink appearance is user-verified; only the nested/nonzero-flags cases remain.
2. **AirPods/sink hot-swap under capture — TO BE TESTED** — the 2026-07-08 11:18 crash
   cascade coincided with a default-sink switch; the recorder rebuild path
   got a disconnect fix (b52091d) but hot-swap while capturing is untested.
3. **Sonic Rooftop material/transparency mismatch** — the recent material and
   texture fixes improved the scene, but the floor still appears incompletely
   textured and Sonic can look enclosed by a translucent mask/bubble. Needs a
   clean live capture plus extracted-material/pass comparison; do not fold this
   into HDR bloom until ordinary texture/blend/depth parity is ruled out.
4. ~~**A genuinely missing effect dependency aborts the whole wallpaper**~~ —
   **fixed and runtime-verified 2026-07-30.** The binary-matched loader logs and
   omits only the failed effect, then continues with valid neighboring effects.
   A real extracted scene with its effect file deliberately removed rendered
   136 frames, exited cleanly, and emitted 122 shaders with zero validation
   failures.
5. **Sky asset audit closed 2026-09-07** — `models/Sky.json` on 2742564457
   resolves the packaged lowercase `models/sky.json`. The original wallpaper
   rendered; the prior absent-asset label was stale. TEXB4 variants and stock
   texture fallbacks are also implemented and verified; see [Asset Texture Verification](../../Asset%20Texture%20Verification.md).
6. ~~**Remaining hard crashes**~~ — **resolved and corpus-verified
   2026-07-30.** The texture-upload and dangling-scriptable signatures are
   fixed, the SIGINT hang no longer reproduces, and every recorded item renders.
7. **Corpus validator's glslang check yields false FAILs** — the July run
   recorded 61; the August snapshot above records 50. Individual dumped
   units are compiled without reproducing the engine compilation context, so
   `M_PI`/`TEX8FORMAT`/`input` errors fire on wallpapers that render perfectly.
   Shader-only FAILs are unverified until the dumps carry the preamble.

## Parity gaps — 3D scenes (from [[3D Scene Support]], priority order)

1. **HDR post pipeline** (`hdr`, `bloomhdr*`) — **backlogged
   2026-07-17** until the Sonic
   3D material/script compatibility sweep is complete: downsample/blur/upsample
   pyramid and final HDR/LDR combine are documented in [[WE Reference Mining]];
   currently logged and skipped. The earlier global RGBA16F experiment was
   **reverted** in 324039c; current ordinary render targets are RGBA8.
   **LDR bloom on 3D scenes was implemented 2026-07-26** (97 of 110 corpus 3D
   scenes were hitting the old refusal), along with the previously unparsed
   `bloomtint`. Because the threshold runs on RGBA8, bloom is weaker than
   reference WE on very bright sources — see [[3D Scene Support]].
2. **Morph targets / model extensions** (`g_Morph*`, MDMP/MDLE) — skeletal
   animation is implemented, but morph payload semantics and the texture-backed
   morph shader path remain unsupported.
3. **Reflection/volumetric post paths** — alternate model/view matrices,
   mipmapped-framebuffer reflections, and the volumetrics material chain are
   documented but not orchestrated by `CScene`.
4. **Perspective override FOV** (`perspectiveoverridefov`) remains unsupported.

## Parity gaps — text (from the CText work)

5. Word wrapping + row limits (`limitwidth`/`maxwidth`, `maxrows`,
   `limituseellipsis`) — long text overflows forever.
6. Text backgrounds (`opaquebackground`/`backgroundcolor`/
    `backgroundbrightness`).
7. `anchor` (dynamic screen anchoring: top/bottomright/...).
8. CJK font fallback when the authored font lacks glyphs (.notdef boxes).
9. Command passes & scene-sampling effects on text objects (none authored
    in the case studies yet).

## Parity gaps — 2D/puppet/parallax

10. Puppet bone constraints (`"tp"`/`"tm"`) — mouse-interactive puppets.
11. Puppet effect chain flags: `clampuvs`, `copybackground`, `solid`.
12. Image-model `autosize` has no renderer consumer, and passthrough images
    without effects still return early during setup (OS Waves regression case).

## Parity gaps — scripting/engine API

13. **ILayer expansion is implemented** (`a1e1a186`, source checked
    2026-09-06): getParent/getChildren, rotateObjectSpace, lookAt/lookAtYaw,
    getTransformMatrix, setParent, attachment access, and texture-animation
    controls. Interactive verification remains. **`engine.registerAsset`
    is being worked on in the tree — see *In flight* above; do not re-plan it
    until that work lands or is dropped.**
14. ~~Camera-transform scripting is partial~~ — **corrected 2026-08-09**: both
    halves work. `getCameraTransforms` returns live eye/center/up/FOV and
    `setCameraTransforms` reads the pose back, rejects non-finite values, and
    applies it through `setScriptCameraTransform`.
15. IEngine context queries: isWallpaper/isDesktopDevice/isPortrait/...
    (08f2a41).
16. Live regression coverage for overlay scripts using
    `thisScene.getLayer(name)` and `input.cursorWorldPosition`; both APIs are
    implemented, but still need visual verification on interactive wallpapers.
17. Structural rebuild on property change (visibility-gating properties need
    a scene rebuild; `prop` socket command only fires applyUserProperties) —
    beingsuz 61d3528.

## Remaining beingsuz ports (non-scripting)

18. ~~`--render-scale` supersampling~~ — ported 2026-08-01 for scene and
    scene-effect targets, with fixed-size shadow/cookie targets excluded.
19. Control-socket extras: live screenshot (a83347a), live renderscale/
    audiodevice apply.
20. `--audio-device` capture-source override for the recorder.
21. Cold-build switch optimizations (739e9c6) — re-evaluate against our own
    measured switch path first ([[Load Performance]]).
22. Web/CEF upstream batch (1150c42, c700c2f, ed032bb, e4ca729, 0d127f9) —
    **re-scope before porting.** The old "only if web wallpapers enter use"
    framing is outdated: web wallpapers are in active use (the live desktop is
    running one) and this fork has since done its own CEF work. Audit what
    `b404e48f` and `610720af` already cover before taking any of these.

## Code health

See [[Candidate Refactors]] (pruned 2026-07-08) — unreferenced submodules,
CallStack replacement, `WITH_WEB` gate, `Maths` deletion, single-impl audio
interfaces, `starts_with` migration. The ShaderUnit-regex item was **corrected
2026-07-26**: those patterns are already `static`, and the real shader cost is
the missing translation cache ([[Load Performance]]).

## Recently closed (2026-07-17, detail in [[Known Issues]])

Current live audio capture health (audio-reactive wallpapers respond normally),
3D camera paths/fades, transparent model sorting, distance/height fog,
spot/tube lighting, shared skeletal-animation parsing/evaluation, GPU-skinned
3D models, animated MDAT attachments, auxiliary MDLV submesh flags, spotlight
shadows, cascaded directional shadows, omnidirectional point shadows,
per-project texture scoping/upload validation, deferred layer-dependent script
initialization, authored texture/material metadata (including
alpha-to-coverage and packed component combos), and scale-free tangent-space
normal transforms.

## Earlier closed (2026-07-08, detail in [[Known Issues]])

MyGO clock stack (parent chain, cascade, UTF-8, 300 DPI, y-up), multi-line
text + effect chains (+ regression fixes), JSON type-drift crash, audio
capture drain, automute vs corked streams, per-wallpaper sound dedupe +
rotation, visualizer auto-gain, ~17 beingsuz ports incl. runtime layer API /
enumerateLayers / applyUserProperties + `prop` socket command.
