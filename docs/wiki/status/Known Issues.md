---
type: Issue Register
title: Known Issues
description: Open bugs, deferred rendering features, and house rules for the linux-wallpaperengine fork.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, bugs, deferred-work, rendering]
timestamp: 2026-08-19T00:00:00-04:00
---

# Known Issues

> **Consolidated checklist (2026-09-06):** [Project checklist](../../Organized%20all.md). It reconciles these notes with the current
> source and separates implementation, local work in progress, and pending
> verification. Dated measurements below remain historical snapshots.

The [2026-09-07 full corpus report](../../Full%20Corpus%20Validation.md) records
all 364 installed wallpapers, including the three recurring scene SIGSEGVs,
legacy TEXV0004 rejection, six CEF shutdown failures and one slow-start timeout.
The 50 shader-check-only failures are listed separately from runtime failures.

Serial follow-up cleared the Snowflakes timeout (74 frames after 91.3 seconds)
and reproduced all six CEF shutdown failures. The reviewed totals are
**85 PASS, 219 WARN, 60 FAIL**: 50 shader-only and 10 runtime failures.



For the live consolidated dashboard (open issues + parity gaps + verification
queue), see [[Current Status]]. This page keeps the durable issue records.

## Open bugs
- **`applyUserProperties` startup dispatch throws across many wallpapers, and
  four now crash** (found 2026-08-19 by the corpus re-run). `f826719a` added
  `dispatchAllUserProperties`, which hands every script the full property object
  once at startup — correct behavior, matching Wallpaper Engine, and it fixed
  2244339517. But `applyUserProperties` had previously only ever run on a live
  `prop` edit, so its failure modes were never exercised. Across 363 items:

  | count | error |
  |---|---|
  | 8 | `TypeError: cannot set property 'X' of undefined` |
  | 7 | `TypeError: cannot read property 'X' of undefined` |
  | 6 | `ReferenceError: thisObject is not defined` |
  | 4 | `TypeError: Cannot assign to read-only property` |
  | 2 | `TypeError: not a function` |

  16 items log at least one; 11 hit `thisObject`.

  **The missing alias is fixed (`181c752e`, 2026-09-07).** A live SceneScript
  fixture verified `thisObject === thisLayer` and access to the owning layer's
  name during updates. The wider property-error/crash set remains open; this
  does not close missing animation APIs or invalid authored assignments.
  → [[SceneScript Runtime]], [Asset Texture Verification](../../Asset%20Texture%20Verification.md)

  Separately, **three wallpapers now SIGSEGV and one exits fatally**
  (`1979606285`, `3644280276`, `3768356757`, `764162681`), where the
  2026-07-30 sweep recorded no remaining hard crashes. `1979606285`'s log ends
  mid-burst of these errors, immediately after
  `[FATAL] Property 'instance' not found on object 'runtime-layer-314'`.
  **Correlation, not yet causation** — nobody has bisected against the parent
  commit, and the corpus also grew by 22 items in this window, so some may be
  newly-subscribed wallpapers that were never tested. Confirm by running these
  four against a build of `878a4ca4` before concluding.

  Do not "fix" this by reverting the startup dispatch: it is the native
  behavior and other wallpapers depend on it.
- **Media-update segfault — TO BE TESTED** (pre-existing): a D-Bus album-art/track change
  fires `ScriptEngine::notifyMediaUpdate` → crash in the QuickJS
  `ObjectAdapter::instantiate` / `VectorAdapter<3>::instantiate` path.
  Crashes any wallpaper with media widgets (Gojo 3100265648, coredump
  captured 2026-07-06). Rendering-independent. *Possibly fixed 2026-07-08*:
  ported beingsuz's `af82084` — `~ScriptEngine` leaked the album-art listener
  (captures `this`), so any track change after a wallpaper swap called through
  a dangling pointer. The user has not confirmed this yet; keep it pending and
  use the media-change workflow in [[Debugging Workflow]].
- ~~**MyGO eyelid skin missing on blink**~~ (3558034522) — **fixed and
  user-verified 2026-07-30.** The parser consumes the auxiliary vertex array,
  draw ranges, and clipping descriptors, and the ordinary single-mask renderer
  produces the eyelid skin correctly. Nested/multi-mask `CLIPPINGCOMPOSE` and
  nonzero descriptor flags remain a separate parity gap.
- **AirPods/default-sink hot-swap during capture — TO BE TESTED**: the recorder disconnect
  path was fixed, but a live default-sink switch while audio capture is active
  has not been verified since the crash cascade observed on 2026-07-08. Use
  the sink-hot-swap workflow in [[Debugging Workflow]].
- **Sonic Rooftop material/transparency mismatch**: authored material metadata,
  project-scoped textures, upload validation, and tangent-basis handling are
  fixed in code, but the user still reports missing-looking floor textures and
  a translucent mask/bubble around Sonic. This remains an open visual/rendering
  bug until an extracted material/pass comparison identifies the remaining
  state or shader mismatch.
- **Stock preview texture lookup fixed 2026-09-07 (`7002a065`).** The loader
  now permits explicit texture-only lookups in stock previews, then PNG sources
  with `.tex-json` metadata. It never mounts whole preview trees. Sonic VS Eggman
  (2743274752) rendered without the missing `effects/waterripplenormal` lookup.

- ~~**The installed missing-workshop-effect wallpaper set**~~ — **all 10 render,
  re-verified 2026-07-30.** The list referenced effects published as
  separate workshop assets the user had not subscribed to
  (`/effects/workshop/2084198056/Simple_Audio_Bars/effect.json` ×6,
  `2107481179/tint`, `2674029580/...`, `2873023340/textshadow`). A sweep of all
  ten rendered nine of them for a full 30 s (1625–1706 frames each):
  Psychonauts (2732852492), Ratchet and Clank (2745603760), Glover (2781837208),
  Crash Bandicoot (2787541254), Zelda Triforce (2826022697), Super Monkey Ball
  (2832978200), Build-a-Kirby (2963361426), Space Channel 5 (3042809339),
  and Metroid (3043063155). The tenth, **Banjo-Kazooie - Banjo-Sleepie**
  (2754056002), was then fixed through the independent NVIDIA texture-upload
  correction below. Generic graceful handling is also fixed: matching
  `wallpaper64.exe`, a truly unavailable effect is logged and omitted while
  the loader continues with the next authored effect. A deliberately damaged
  Zelda Triforce fixture rendered 136 frames with zero shader failures instead
  of aborting.
- ~~**Blink/mask regressions**~~ — **user-verified 2026-07-30.** Sonic
  Frontiers - Starfall (2915841260), Build-a-Kirby (2963361426), and Chainsaw
  Man-Reze (3577990983) now blink correctly; MyGO's independently implemented
  clipping path is recorded above.
- ~~**Remaining hard crashes**~~ — **all resolved as of 2026-07-30.** Every
  wallpaper listed in this section now renders: the script-engine signature was
  fixed by 9895189, the NVIDIA texture-upload signature by fc41164, and the
  Rinn-Flou hang no longer reproduces. The original 2026-07-26 triage is kept
  below because the signatures and the elimination order are worth having if a
  new crash appears.

  Original entry (2026-07-26, after the case-sensitivity fix). Four
  of the eight former segfaults — `March of the Minis` (3042765095),
  `Sonic - VS Eggman` (2743274752), `Pokémon Trainer Red` (2884484489),
  `Spirit Tracks` (2995869332) — now render; they were crashing downstream of
  an asset the case-sensitive lookup could not find. The rest are real and
  split into two unrelated signatures:
  - ~~**Texture upload, inside the NVIDIA driver**~~ — **fixed 2026-07-30**
    (fc41164). Affected `[若叶睦]清夏 - It's MyGO!!!!!/AveMujica` (3558034522)
    and `Banjo-Kazooie - Banjo-Sleepie` (2754056002); the latter had been filed
    under the dependency-abort item until its backtrace was taken.

    It was **our bug, not the driver's**. Texture payloads are tightly packed but
    GL defaults `GL_UNPACK_ALIGNMENT` to 4, so a format whose row is not a
    multiple of four bytes makes the driver read a padded stride and overrun the
    buffer. RG88 at an odd width does exactly that: a 1181x1168 upload has
    2362-byte rows that the driver rounds to 2364, reading 2336 bytes past a
    2758816-byte payload. Whether it faults depends on where the allocation sits,
    which is why it presented as an opaque `libnvidia-eglcore` crash with no
    engine frame to blame.

    The same defect was already fixed for R8 by e377960 ("Fix red textures being
    distorted", 2022) — at one byte per pixel the overrun is at most three bytes
    per row, so it skewed rows visually instead of running off the allocation.
    RG88 support predates that fix (03c4660, 2021) and was never covered. The old
    placement was also sticky: alignment stayed at 1 for the rest of the process
    once any R8 texture loaded, so whether an RG88 texture crashed depended on
    load order. Both wallpapers now render.

    Diagnostic note for next time: the driver dies inside `glTexImage2D`, so the
    culprit is only visible with a `glFinish()` + `glGetError()` after each
    upload, which pins the fault to the offending call instead of a later one.
  - ~~**Script engine, during module init**~~ — **fixed 2026-07-30** (9895189).
    Affected `Misty Sea | Seyul` (3765081478), `Kirby - Gourmet Race`
    (3281559867) and `Croc - Legend of the Gobbos` (3562177956); the latter two
    shared the root cause and had never been backtraced.
    `ScriptableObject::registerProperty` runs from the constructor and calls
    `queueScript`, which stores a raw `ScriptableObject*` and a `DynamicValue&`
    and hands QuickJS a wrapper holding the same reference — and nothing ever
    removed those registrations. `CScene::dispatchObjectType` constructs an
    object and only then calls `setup()`, deleting it if `setup()` throws, which
    three objects in Misty Sea do on a shader compile error. The earlier guess
    that `scriptable_container` was at fault was wrong: the core shows the
    wrapper intact (magic `0xdeadbeef`, both pointers present) while the
    *referenced object* is entirely zeroed, so the faulting instruction loads a
    null vtable pointer to adjust `this` for `CObject::getObject`. Fixed by
    adding `ScriptEngine::unregisterScriptable()` and a real
    `~ScriptableObject` that calls it, which covers the `setup()`-threw path and
    `CScene::destroyObjects` alike. A/B verified: all three exit 139 without the
    fix, render ~2250 frames with it.
  - ~~**Hang, ignored SIGINT, killed**~~: `Rinn-Flou丨R-18丨4K丨DarK`
    (3763384612) — **no longer reproduces (2026-07-30)**: launches and renders
    1670 frames over a 30 s run, and exits on SIGINT normally.
  - The `json.exception.type_error.302` and `LZ4_decompress_safe` entries that
    used to sit here were fixed on 2026-07-26; see below.
- **The 2026-07-19 hard-failure list no longer reproduces** — 3107568889,
  3244466773 and 3318541129 no longer throw `nlohmann::json::parse_error.101`;
  3061226599, 3629379075 and 3320489297 (previously SIGSEGV/SIGFPE) now render
  and only WARN; 3351179520 renders and fails only glslang. Cause not
  investigated — plausibly the commits landed between the two runs. Do not
  treat that list as current.
- **`tools/validate-corpus.py` glslang check produces false FAILs** — figures
  below are from the 2026-07-26 run and predate `33ece832`; see [[Current Status]]
  for the re-run. 61 of the
  98 FAILs in the 2026-07-26 run are "N/M shader units failed glslang" on items
  that rendered completely normally (full frame counts, normal fps). The top
  errors are `'M_PI' : undeclared identifier`, `'TEX8FORMAT' : undeclared
  identifier` and `'input' : Reserved word` — definitions and preamble the
  engine supplies during translation that a standalone `glslang` run on a
  dumped unit never sees. The dumps need the same preamble before this check
  means anything; until then, treat shader-only FAILs as unverified.
- **Latent out-of-range path read in `AssetLocator::shader` — not user-facing**
  (`AssetLocator.cpp:16-19`, found 2026-07-26 while profiling, deliberately left
  unfixed): the `workshop`-shader compat branch dereferences `begin ()` and the
  second path component without checking `end ()`; only the third is guarded.
  A shader path of exactly `workshop` or `workshop/<id>` aborts under libstdc++
  assertions. No authored wallpaper produces such a path, so it has never fired.
  → [[TODO Backlog]].

## Fixed 2026-07-26

- **Stock effect containers were never mounted, killing 15 wallpapers outright.**
  `WallpaperApplication.cpp` mounted the assets directory once at `/`, but
  `assets/materials/effects/` is **empty**: all 46 stock effects ship as
  self-contained trees (`assets/effects/<name>/materials/effects/<name>.json`
  plus their own `shaders/`), none of them mounted. Any wallpaper using
  `scroll`, `spin`, `tint`, `transform`, `vhs`, `twirl`, `caustics`,
  `blendgradient` or `godrays_downsample2` died on `Cannot find requested file
  in any of the mountpoints` for a file that was on disk the whole time.
  Fixed by mounting every `assets/effects/<name>/` container at `/` after the
  assets root and before the cwd mount. The directory name does not always
  match the material name (`watercaustics` → `caustics.json`), so on-demand
  resolution from the requested path is not possible — all 46 are mounted.
  Verified collision-free first: 136 effect shaders / 136 distinct names /
  zero overlap with root `shaders/`; 80 materials files / 80 distinct relative
  paths / zero overlap with root `materials/`. Only each container's `preview/`
  subtree overlaps between effects, and nothing resolves `/preview/...`.

  **Outcome, measured on all 15 affected items — the abort is gone from every
  one of them, but only 5 became usable:**
  - **5 now render**: Rayman - Gemstone Temple (2719499501, WARN), Jak and
    Daxter (2752012990), Pikmin - Rainy Day (2765364591), Captain Toad
    (2816100409), Klonoa and Huepow (2885298446) — the last four "FAIL" only
    on the glslang false-positive below.
  - **7 advanced to a different missing asset**, mostly the unsubscribed
    workshop-dependency issue above: 2721146775, 2722446525, 2886819832,
    2896405857, 2924081598, 3562131953, plus 2742564457 on `/models/Sky.json`.
  - **3 now SIGSEGV further along**, deep in shader translation after hundreds
    of units compile: Sonic - VS Eggman (2743274752), Pokémon Trainer Red
    (2884484489), Spirit Tracks (2995869332). Previously masked by the early
    abort — a pre-existing latent crash now reachable, not caused by the mount.
    Added to the untriaged-crash list above.

  Control check: the 3 then-believed-absent-asset items (2775277607, 2935233995,
  869945315) were unchanged by the fix, as predicted. None of the three was
  actually an absent asset — two were the case-sensitivity bug below and the
  third was the package-filename bug below that.
- **Package lookups were case-sensitive, so Windows-cased assets never resolved.**
  Wallpapers are authored on NTFS, where `sounds/x.mp3` and `Sounds/x.mp3` are
  the same file. `PackageAdapter::exists`/`open` compared `filename` byte-for-byte,
  so a scene referencing `sounds/01 Overworld (MM).mp3` against a package storing
  `Sounds/01 Overworld (MM).mp3` aborted with `Cannot find requested file in any
  of the mountpoints` — for a file sitting in the package. Rayman alone hit this
  on 69 sound references. `PackageAdapter` now builds a lowercased index of the
  package entries once at construction and both lookups go through it (this also
  replaces the per-lookup linear scan over every entry).

  Scope check before fixing: every scene wallpaper in the 341-item corpus ships
  a `scene.pkg` — there are **zero** loose-file scene wallpapers — and the WE
  assets tree is all-lowercase apart from `shaders/HLSL`. So the package side is
  the whole bug; `DirectoryAdapter` was deliberately left case-sensitive.

  **Outcome, measured:**
  - Rayman (2719499501) previously died ~2 s into the desktop path on its first
    sound; it now runs a full 20 s windowed with audio enabled and logs zero
    mountpoint failures. The validator never caught this because it forces
    `--silent`, which skips sound loading entirely — worth remembering when
    reading corpus results.
  - 8 of the 10 remaining missing-asset aborts now render: 2721146775,
    2722446525, 2742564457, 2886819832, 2896405857, 3562131953 (all WARN), plus
    2775277607 and 2935233995 which now render and FAIL only on the glslang
    false-positive. Their "absent" assets were `materials/dk_lights.tex` and
    `models/rayman.json` all along.
  - 4 of the 8 SIGSEGVs disappear (3042765095, 2743274752, 2884484489,
    2995869332) — including all 3 that the effect-mount fix had newly exposed.
    They were faulting downstream of the unresolvable asset.
  - Still failing for unrelated reasons: 2924081598 (LZ4 decompress error) and
    869945315 (`/audiophile.json` unresolvable) — both fixed later the same day,
    below.
- **`dependencies` authored as objects killed the wallpaper outright.** Newer
  scenes write `"dependencies": [{"id": 104, "index": 0, "type":
  "collisionmodel"}]` instead of a list of bare ids. `parseDependencies` pushed
  each entry straight into a `std::vector<int>`, and the implicit conversion
  threw `json.exception.type_error.302 "type must be number, but is object"`.
  The reason this was fatal rather than cosmetic: `ObjectParser::parse` calls
  `parseDependencies` **both** inside its try and inside the catch that is
  supposed to salvage a partially-broken object, so the fallback rethrew the
  same error and the object — and the wallpaper — died. It now accepts both
  forms and takes `id` from the object form, which is all `CScene` uses
  (`dependencies` is only a creation-order list).

  **Outcome, measured**: `Super Mario 64 - Hazy Maze Cave` (2726424530, 82
  frames), `Crash Bandicoot - Wumpa Shower` (2787541254, 99), `Space Balls`
  (3594400060, 103) all render. Covered by a case in
  `Testing/Cases/JsonTolerance.cpp`.
- **TEXB0004 conditional-variant textures aborted on LZ4 decompression.** A
  `.tex` with `conditionalImages > 1` (a theme/variant table) does **not** store
  the base image's mipmap chain contiguously: it interleaves each variant's
  payload between the base levels, grouped by level. `parseContainer` already
  skips the variant *table*, and the base mip0 parses correctly — verified by
  decoding `materials/bg1.tex` of 2924081598 with a standalone Python LZ4 block
  decoder, which produced exactly the header's 45056 bytes. But the parser then
  read mip1 from immediately after mip0's payload, landing in variant data. The
  garbage header (`1x2 comp=1 uncompressed=1 compressed=0`) reached
  `LZ4_decompress_safe`, which returned an error and `sLog.exception`'d the whole
  wallpaper away.

  `parseMipmap` now returns `nullptr` on an implausible header (zero dimensions,
  `compression > 1`, or a non-positive size) instead of throwing, and `parse`
  keeps the levels read so far and stops. This is safe because `CTexture` sets
  `GL_TEXTURE_MAX_LEVEL` from the parsed mipmap count, so a truncated chain is
  still mipmap-complete; the cost is that those textures lose their smaller
  levels. If the first mipmap is the bad one the old exception still fires —
  that is a genuinely broken file, not this layout. **The variant mipmap header
  format was not reverse-engineered at that time**. Superseded on 2026-09-07
  by `f7d97740`: patch groups and live conditional selection are implemented;
  full mip chains and authored variants are verified.

  **Outcome, measured**: 2924081598 (`Super Mario Voxel`, 85 frames) and
  3766299002 (96 frames) render, with zero `LZ4_decompress_safe` errors left in
  the corpus.
- **Only `scene.pkg` and `gifscene.pkg` were ever mounted.** `setupAssetLocator`
  hardcoded those two filenames, but older workshop items name the package after
  their scene file — 869945315 ships `audiophile.pkg` and asks for
  `/audiophile.json`, which therefore resolved nowhere and aborted with zero
  frames. This was previously written up here as "genuinely absent content",
  which was wrong: the file was in the package the whole time. The two hardcoded
  mounts are now one loop over every `*.pkg` in the wallpaper directory, which is
  also a shorter diff. Ordering is not a concern — no directory in the 341-item
  corpus carries more than one package (321 `scene.pkg`, 3 `gifscene.pkg`, 1
  `audiophile.pkg`).

  **Outcome, measured**: 869945315 goes from `fatal.exception`/0 frames to WARN
  at 104 frames with no unresolved-file errors.
- **`tools/validate-corpus.py` counted this machine's GLFW/GLEW baseline noise
  as wallpaper errors**, so no item could ever PASS. `BENIGN_ERRORS` is now
  counted per item from `log.txt` and subtracted from the `log.error` counter
  before classification — `health.json` caps its detail samples at 5, so the
  count cannot be filtered from the report alone. Full corpus after the fix:
  54 PASS / 189 WARN / 98 FAIL / 3 SKIP over 341 items.

- **Wallpaper switches stalled the render thread for up to 2.3 s** — the texture
  cache budget (512 MiB) was smaller than a single 4K wallpaper (~576 MB), so
  every switch evicted the textures the loader thread had just staged and the
  scene build re-decoded them inline. Budget raised to 1536 MiB (sized for two
  wallpapers, since the crossfade still references the outgoing set): stall
  0.35–0.79 s, scene build 0.03–0.46 s, `texture.sync_load` ~50 → ~9 per switch
  (`eaa72fc`). Startup/switch phase instrumentation landed with it (`dc11774`).
  Measurements, caveats, and the remaining open work: [[Load Performance]].
  **Live 4K switch feel and RSS behavior are still pending user verification.**

## Fixed / implemented 2026-07-17 (visual verification noted separately)

- **Live audio capture signal restored/working** — the user confirmed that
  audio-reactive wallpapers currently respond normally while music plays.
  This closes the flat-visualizer/PulseAudio-shim symptom, while the historical
  evidence remains in [[Audio Capture Silence Investigation]]. It does **not**
  prove exact 16/32/64-band DSP parity.
- **3D skeletal models and animated attachments** — MDLS/MDLA parsing and pose
  evaluation are shared between 2D puppets and 3D models; skinned 3D vertices
  upload `g_Bones` on the GPU, and nested MDAT children resolve against the
  parent's current bone pose (dbcc722, 92049ac, 0356a16).
- **BoostModel auxiliary submesh flags** — `0x400` no longer incorrectly changes
  index width; unknown bits remain rejected (cfdcc73).
- **3D shadow mapping** — spotlight, stable three-cascade directional, and
  six-face point shadows render into a shared comparison-depth atlas and feed
  the native shadow shader paths (c936afe, bcad160, fc68d89). Live bias/edge
  matching is pending visual verification, not missing implementation.
- **Authored 3D material/texture state** — alpha-to-coverage, `TEXnFORMAT`,
  packed component flags/combos, user-texture overrides, additional GLSL
  compatibility, and project-scoped texture resolution are implemented
  (6985bbe, a6166cb). Malformed upload payloads now fail safely.
- **Layer-dependent SceneScript initialization** — initializers that need the
  complete object graph are deferred until layer registration is complete;
  vector wrappers no longer lose the intended live object (a4ae582).
- **Scaled-model normal mapping** — the inverse-transpose tangent basis is
  normalized so common `0.01` model scales do not amplify normal-map vectors by
  100× (67ad630).
- **HDR RGBA16F global targets** — **outdated/reverted, not pending**. The
  experimental global format was removed in 324039c; ordinary FBOs are RGBA8.
  The proper HDR post pipeline remains backlogged.
- **Bloom ignored on every 3D scene** — **fixed 2026-07-26, live-verification
  pending**. `CScene` logged `"Bloom is not supported on 3D scenes yet,
  ignoring"` and skipped the helper object entirely; 97 of the 110 corpus 3D
  scenes enable `general.bloom`. The LDR chain is camera-independent, so the
  refusal was removed rather than worked around. Same change parses
  `general.bloomtint`, which was never read on 2D scenes either.

## Fixed 2026-07-08 through 2026-07-16 (verified unless noted)
- **MyGO clock stack invisible/mispositioned** (3558034522) — four root
  causes, all in CText: 2D path ignored the `parent` chain (8cc5ad2), parent
  visibility didn't cascade (8123d17), UTF-8 rasterized per byte → mojibake
  (d8173ee), and sizing missed WE's semantics — pointsize is **points at 300
  DPI** (EM = pointsize × 300/72 px, mined from `lib.sceneScript.d.ts`), with
  scale applied via the world matrix (f9aa10c). Position/order needed the
  follow-up y-axis fix: **scene coordinates are y-up (bottom-left origin)**
  (628b698) — the old path assumed y-down. User-verified on screen.
- **Multi-line text + alignment + padding** (50a82c4) and **text effect
  chains + brightness** (9c66ad2, regressions fixed in 0f3ced8: leaked
  glClearColor blanked scenes; box-clipping cut overflowing text).
- **Type-drift parse abort** (honeycomb 3758354038): `optional<T>` was
  `noexcept` over a throwing conversion → `std::terminate` on authored type
  drift (string `padding`). Fixed 7dbce65 + `Testing/Cases/JsonTolerance.cpp`.
- **Audio capture overruns**: update() dispatched one ~10ms fragment per
  frame — structurally slower than realtime; pipewire spammed
  `overrun recover`. Fixed by draining the event queue per frame (998386a).
- **Wallpaper music silent**: automute counted corked/silent streams
  (skwd-paper's keep-alive, paused players) as "audio playing" and muted
  forever (b52091d requires uncorked+unmuted); the machine's wrapper also
  passes `--noautomute` now since skwd-paper's stream defeats detection.
  User-verified.
- **Same wallpaper on two monitors echoed its music**: per-screen Projects
  meant address-based dedupe failed; replaced with a value-keyed soundtrack
  coordinator in AudioContext (a1693e7) — one wallpaper's audio at a time,
  round-robin rotation at end-of-track across wallpapers with music,
  duplicates silent, migration on switch-away.
- **Visualizer bars saturated** (flicker-only response): the hand-tuned
  log-magnitude/auto-gain/noise-gate pipeline was replaced with the recovered
  Wallpaper Engine capture path: float stereo input, native FFT sizing and
  fourth-root band mapping, native magnitude scaling, per-channel normalization,
  and time smoothing. `WPE_AUDIO_DEBUG=1` logs to
  `/tmp/we-audio-debug.log`. *Basic live response is user-verified. Exact
  parity is still open because the closed native 64→32/16 provider reduction
  remains inferred as peak-preserving pooling.*

## Deferred / not implemented
- Puppet bone constraint JSON (`"tp"`/`"tm"`) — mouse-interactive puppets.
- Puppet effect chain: `clampuvs`, `copybackground`, and the `solid` flag are
  unhandled on puppet objects.

## House rules (from memory)
- `grim -o <monitor> /tmp/lwe-live.png` is allowed for quick visual triage,
  but a live PNG/diff is not final rendering proof; ask the user to confirm the
  animated/composited result. Engine `--screenshot` on an isolated debug window
  remains the deterministic object-level tool.
- ~~Claude never launches the engine or a window itself.~~ **Outdated by user
  instruction 2026-07-17.** Claude may launch/restart it when needed for the
  requested work. After an implementation, stop stale/transient instances and
  restore with `/home/admin/.local/bin/waypaper --restore` so the newest binary
  is live; preserve Waypaper's saved monitor choices.
- Do not create/reuse a transient systemd wallpaper unit by default. A leftover
  unit can create duplicate engines and make a normal kill look like an
  automatic restart. Waypaper restore is the persistent startup authority.
- Data-driven rendering: no magic numbers; derive from scene.json/assets or
  the mined WE reference ([[WE Reference Mining]]); DSP constants get a
  `ponytail:` tuning note.
- Watch for stale engine instances after rebuilds → [[Debugging Workflow]].
