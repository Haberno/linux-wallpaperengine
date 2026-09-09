---
type: Performance Concept
title: Load Performance
description: Cold-start and live-switch load cost for linux-wallpaperengine — measured phase breakdown, the texture-cache sizing constraint, and how to re-measure.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine/src/WallpaperEngine/Application/WallpaperApplication.cpp
tags: [linux-wallpaperengine, performance, textures, startup, switching]
timestamp: 2026-08-19T00:00:00-04:00
---

# Load Performance

Two different paths cost time, and they fail differently:

- **Cold start** — process launch to first rendered frame. Dominated by texture
  read+decode and the scene build.
- **Live switch** — `switch <screen> [transition] <path>` on an already-running
  engine. Most of the work is off-thread, but whatever lands on the render
  thread is a visible stall.

Both are instrumented by `WPE_HEALTH_REPORT` (ledger:
`src/WallpaperEngine/Debug/README.md`, usage in [[Debugging Workflow]]). Phase timings below come from that report; socket reply timings use a client
monotonic clock.

## Pipeline

**Cold start** (`WallpaperApplication`): `loadBackgrounds`
(`startup.project_load`) → `prepareOutputs` batch texture prefetch
(`startup.texture_prefetch`, read+decode through the pooled
`TextureParser::decodeMipmaps`) → per-screen scene build
(`startup.scene_build`) → first `render` (`startup.first_frame`, measured from
process start).

**Live switch**: `requestBackgroundSwitch` hands a job to the loader thread.
`switchWorkerMain` (`switch.prepare`) parses the project, reads and decodes its
textures, and — only if `m_videoDriver->makeBuildContextCurrent ()` succeeds —
uploads them on a shared GL context. `processPreparedSwitches` →
`applyPreparedSwitch` (`switch.apply`) then runs on the render thread:
properties, browser, `storeTexture` of the staged set, `CWallpaper::fromWallpaper`
(the scene build), and `setWallpaper` with the transition. A `malloc_trim` is
scheduled ~5 s later.

**Measurement caveat.** Only `WaylandOpenGLDriver` implements
`makeBuildContextCurrent`; the base returns false (`VideoDriver.h:81`). So in
`--window` (GLFW) mode *all* GL upload happens on the render thread and the
loader thread reports `upload ~0 ms`. The July measurements below were taken in a
640×360 GLFW window, which means the render-thread stall figures are an
**upper bound** — on the live Wayland desktop that upload is off-thread. The
GLFW window-mode screen key is `"default"` (`GLFWWindowOutput.cpp:31`), needed
for socket commands.

## 3D model loading (2026-09-08)

- [x] Batch-prefetch all `Model3D` submesh material textures. Pokemon - Deep Sea
  Dive (3562141459) preloads 144 textures instead of 17, reducing render-thread
  cache misses from 129 to 2. Final texture count/bytes match the baseline.
- [x] Read each MDL once in bulk, then parse geometry and animation from the
  same bytes. The former pair of `load()` calls reopened and copied the whole
  file twice using byte iterators. Pokemon's project phase fell from about
  1.10 s to 0.66 s, with no persistent model cache.
- [x] Reuse linked GPU program binaries across material and model-shadow passes.
  Pokemon reuses 764 programs and compiles 92 from source. Complete vertex and
  fragment source keys preserve lighting/bone-count/texture-format variants;
  each pass still has an independent program and uniform state. The cache is
  bounded to 64 MiB of binary/source payload in RAM per render context, uses
  least-recently-used eviction, and writes no files. Unsupported or rejected
  binaries fall back to normal compilation. See [[Shader Translation]].

The original Release executable **and its shared library** were preserved
before edits (base `7d8d11c8`, with the same existing unfinished edits in both
builds). Tests ran serially on a temporary Wayland headless output, 1920×1080
at compositor scale 2, engine render scale 1, 30 FPS, muted. Physical desktop
wallpapers and their render settings were preserved. Wall-clock results are
machine/driver/cache dependent; these are measured checks, not guarantees.

| Pokemon - Deep Sea Dive, process start to first frame | Before | After |
|---|---:|---:|
| Driver disk cache disabled for both test processes | 38.286 s | 18.749 s |
| Fully warmed driver cache, fresh engine processes | 3.498 s | 2.996 s |

The uncached comparison uses NVIDIA's documented `__GL_SHADER_DISK_CACHE=0`
only in the test process environment; it neither deletes nor reconfigures the
user's existing cache. The initial unprofiled baseline was 20.377 s and the
first cached implementation was 5.416 s, but later runs warmed NVIDIA's own
cache. Do **not** attribute that whole difference to this patch. In the
controlled uncached optimized run, scene construction took 4.450 s but first
frame took 18.749 s: the driver deferred additional compilation until drawing.
A ready control socket alone would incorrectly report roughly 5.7 s.

A warm Wayland switch from the small `variants` fixture to Pokemon improved
from **3.779 s to 2.318 s** (`switch.apply` request to replacement installation),
with render-thread stall falling from **2.556 s to 1.504 s**. This phase ends
when the new scene is installed; it does not include the fade completing.

A small 2D control, Shin Godzilla [Audio Responsive + Puppet Warp]
(3094637759), stayed around 0.65 s after driver warmup. The largest gains are
in scenes that repeatedly use the same materials and shadow shaders.

Verification: 953 assertions in the 118 default C++ cases, plus 27 assertions
in two opt-in OpenGL cases. The latter verifies a real binary-cache hit,
independent uniforms, source changes in either stage, too-small cache budgets,
and recovery after vertex/fragment compilation and program-link errors.

A focused corpus sample completed in **27.9 s**, two seconds of post-load
rendering per item, one renderer and six shader-validation workers:

| Workshop ID | Exact title | Result |
|---|---|---|
| 3562141459 | Pokemon - Deep Sea Dive | 29 frames, exit 0, no shader failures; existing script warnings |
| 3562150203 | Rayman - River Ride | 29 frames, exit 0, no shader failures; existing script warnings |
| 3589454154 | 土星 &#124; Saturn - Sykm | 30 frames, exit 0, no shader failures |
| 3094637759 | Shin Godzilla [Audio Responsive + Puppet Warp] | 30 frames, exit 0, no shader failures |
| 3107568889 | Moon Lady 4K [OC] [space sci-fi] [AI] | 30 frames, exit 0; same 8 standalone shader failures and one logged exception as the September 7 full corpus |

All five shader-failure file sets match the full-corpus baseline. This is a
startup/render regression sample, not another complete corpus run or a visual
parity claim. For appearance, check Pokemon's fish animation, terrain, lighting
and shadows; Rayman's character animation and river materials; and Saturn's
ring transparency. Measurements and raw health reports were retained under
`/tmp/lwe-3d-load-20260908/` during verification.

## First-draw program sharing (2026-09-09)

- [x] Avoid compiling a separate first-draw executable for every identical
  material pass. A cold NVIDIA profile attributed 74% of sampled CPU time to
  `libnvidia-gpucomp`; hundreds of draws individually spent about 23 ms in the
  driver. Binary-cache hits alone did not avoid that work. Compatible passes
  now share a live program and upload their own complete registered uniform
  layout before drawing. Different uniform layouts, including array lengths,
  remain separate; see [[Shader Translation]] for ownership and fallback rules.
- [x] Keep the binary/source payload budget at 64 MiB in RAM. Live programs
  are owned by materials and released when their last user disappears. Each
  pass retains its private program for registration and fallback. There is no
  new disk cache or retained set of GPU programs after a wallpaper is released.

The baseline is `3de07454`, including the previous loading optimizations and
identical pre-existing unfinished edits. Release executable/library pairs were
compared serially on the same temporary Wayland headless output used above,
1920×1080 at compositor scale 2, engine render scale 1, 30 FPS, muted. The real
desktop engine remained running. These are local single-run measurements,
not latency percentiles; phase timings vary with desktop and system activity.

| Pokemon - Deep Sea Dive (3562141459), process start to first frame | Before | After |
|---|---:|---:|
| NVIDIA disk cache disabled for each test process | 19.240 s | 7.425 s |
| Warm driver disk cache, fresh engine processes | 3.635 s | 3.425 s |

The independent managed-implementation cold run was 7.916 s; earlier baseline
profiles were 17.954–18.546 s. In the paired cold run scene construction was
4.386/4.639 s, so the large gain comes after construction. Project parsing was
1.161/0.628 s in that pair, an unrelated source of timing variation. Neither
shader quality nor texture resolution was reduced. The new path created 64
live material programs and served 535 additional material passes from them;
the existing binary cache still recorded 92 source misses and 764 hits.
Warm post-load rendering averaged 29.56 FPS in both runs under the 30 FPS cap;
this is a short capped smoke check, not an uncapped GPU throughput benchmark.

Warm fixture→Pokemon switching took 2.617/2.566 s from request to scene
replacement; render-thread apply stalls were 1.811/1.783 s. These exclude the
first draw and transition after replacement and show little improvement with
already-warm shaders. Worst frames in those runs were 1.972/1.863 s.

Validation: 953 assertions in 118 default C++ cases and 69 assertions in three
hidden GL cases passed. A five-wallpaper corpus sample completed in 16 s;
Shin Godzilla (3094637759) and Saturn (3589454154) passed, Pokemon and Rayman -
River Ride (3562150203) retained existing script warnings, and Moon Lady
(3107568889) retained the same eight of 506 standalone shader failures. All
five rendered and exited normally. Raw timings and reports were retained under
`/tmp/lwe-shader-load-20260908/` during verification. This is a targeted rerun;
the previously completed full installed corpus was not repeated for this patch.
A fixture→Pokemon→fixture→Pokemon round trip also completed all three switches
and exited normally. On switching away, referenced textures returned to three
and live FBOs to ten; GL tests directly verified shared program deletion.

Retrieving the program binary after its first draw did **not** reduce the cold
load (18.543 s), nor did removing the disabled original-source trailer from
translated GLSL (18.533 s). Those experiments were discarded. NVIDIA's parallel
compiler hint was already at its implementation maximum; merely enabling that
extension was not an additional optimization.

## Desktop switch regression (2026-09-07)

- [x] Bind native engine getters to their owning instance. During live
  verification, switching to 2639381674 — **Soulless 4k {Artwork by Ilona
  Mencner}** captured SIGSEGV in `engine_get_user_properties`: the receiver's
  native opaque pointer was null. Layer scripts use `Object.create(engine)`,
  so inherited getters receive a JavaScript wrapper without that pointer.
  `userProperties`, `canvasSize`, and `screenResolution` now capture the
  engine instance ID instead. A minimal scripted-text fixture reproduces
  SIGSEGV in the old build and passes through two replacements with the fix.
- [x] Make the local desktop launcher prefer optimized, CEF-complete, loadable
  builds. Its newest-file selection had picked `build/` with `-O0`; the same
  source built in `build-release/` uses optimization. Explicit
  `LWE_ENGINE_BINARY` overrides still work, including Debug builds.
- [x] Keep submitted IPC switches pending when the reply times out. Previously
  a 30-second timeout, retry, and cold-start fallback could SIGKILL a live
  engine and discard its caches. The launcher now waits up to five seconds for
  acknowledgment and preserves the process when a submitted request has a
  delayed reply. This does not claim that an unacknowledged switch succeeded.
- [x] Process IPC and prepared replacements while fullscreen-paused; newly
  built wallpapers inherit the paused state. Bound Wayland event waits so
  missing output callbacks cannot indefinitely block the application loop.
- [x] Accept Waypaper's `--no-full-screen-pause` spelling as an alias for
  `--no-fullscreen-pause`; the previous engine ignored the former.

Matching sequential Wayland runs used an isolated 1920×1080 headless output
(compositor scale 2), engine render scale 1, 30 FPS, muted audio, and the
`inksplash` transition. Each process started on the same small texture fixture,
then switched to these two wallpapers in order. The ordinary desktop engine
was still running in both runs. Times exclude the transition animation after
scene replacement. These are single-run comparisons, not latency percentiles;
GPU/disk caches can influence absolute timing.

| Wallpaper | Debug request → replacement | Release request → replacement | Debug / Release render-thread stall |
|---|---:|---:|---:|
| 3094637759 — Shin Godzilla [Audio Responsive + Puppet Warp] | 1915 ms | 319 ms | 1745 / 180 ms |
| 3107568889 — Moon Lady 4K [OC] [space sci-fi] [AI] | 8206 ms | 1513 ms | 6726 / 845 ms |

Both runs completed two switches and exited normally. They reported the same
pre-existing shader/object diagnostics and seven synchronous texture loads;
this comparison measures switching, not visual parity. The optimized build's
worst recorded frame was 883 ms versus 6787 ms for Debug.

A mapped fullscreen test window reproduced the old IPC failure: `memstats`
timed out after three seconds and the reply arrived only after unpausing.
With the fix it answered in 1 ms while paused, acknowledged a switch in 10 ms,
and applied the replacement in 81 ms while still paused. A powered-off
headless-output check also answered in 1 ms with the fix; that check did not
reproduce a baseline failure. No hours-long idle soak is implied by these tests.

Validation: 914 assertions in 115 C++ cases; nine launcher regression tests
cover build selection/overrides/fallbacks, delayed replies without retry or
process replacement, and the separately committed global-flag deduplication.
The scripted-text integration regression also passes, including both live
replacements and normal shutdown; run it with `LWE_TEST_BINARY` set using
`python3 -m unittest discover -s tools/tests -p test_engine_getters.py`.
Live deployment preserved the saved monitor assignments and engine render
scale 2. A normal launcher switch to **Soulless 4k** returned in 45 ms, kept
PID 1118343, and answered a subsequent socket probe in 3 ms. The 45 ms is an
acknowledgment, not the complete rendering latency.

Raw reports and probe scripts for this run are under
`/tmp/lwe-switch-20260907/` (temporary local artifacts).

## Measured cold start (2026-07-26)

Six heavy 4K workshop wallpapers, `--window 0x0x640x360 --fps 15`, all values ms:

| total | project | prefetch read+decode / upload | scene build | wallpaper |
|---|---|---|---|---|
| 2893 | 1786 | 44 / 67 | 531 | Saturn 3589454154 |
| 2207 | 13 | 514 / 269 | 476 | Yangyang 3760837492 |
| 2113 | 28 | 397 / 281 | 541 | WLOP CNY7 3754630802 |
| 2111 | 2 | 293 / 404 | 754 | Sunflower Girl 3755071989 |
| 2047 | 34 | 409 / 312 | 607 | Red Warbler 3233141951 |
| 1810 | 30 | 276 / 168 | 517 | Clock Town 3429481890 |

So ~2 s is normal for a heavy wallpaper, spread roughly evenly over
read+decode, upload, and scene build — no single dominant cost, except on
Saturn, whose `project.json` parse alone is 1.8 s.

Texture load itself is **~92 % `stb_image` decode**. Threading the decode was
worth ~7×; a Rust rewrite of the surrounding code would add only ~1.25× on top,
which is why the decode is pooled and the parser is not being ported.

## Texture cache sizing (the switch regression)

A single 4K wallpaper fills the texture cache with **~576 MB**
(`memstats` → `tex_bytes`), which was **more than the whole
`TEXTURE_CACHE_BUDGET_BYTES`** (512 MiB) it used to run under.

During a switch the outgoing wallpaper is still referenced by the crossfade, so
`TextureCache::trim` — which can only evict entries with `use_count () == 1` —
had exactly one set of eviction candidates: the incoming wallpaper's
freshly staged textures. The scene build immediately after then re-resolved and
re-decoded them (PNG, one at a time) **on the render thread**.

Raising the budget to **1536 MiB**, sized to hold two wallpapers at once:

| | before | after |
|---|---|---|
| render-thread stall (`switch.apply`) | 512–2340 ms | 353–792 ms |
| of which scene build | 483–2020 ms | 26–464 ms |
| request to visible | 1685–3034 ms | 642–2534 ms |
| `texture.sync_load` per switch | ~50 | ~9 |
| `worst_frame_ms` over the run | 2578 | 1788 |
| RSS across 6 heavy switches | — | 594–1234 MiB |

**The lesson is diagnostic, not just numeric:** "switches are slow" looked like
shader translation or scene-build cost, and it was neither — it was cache
thrash. Check the `texture.sync_load` counter before profiling a scene build.

**Do not "fix" this by pinning the staged textures** with a `shared_ptr` held
across the scene build. That makes both wallpapers unevictable and OOM-kills the
process on 4K switches; the comment at the `storeTexture` loop in
`applyPreparedSwitch` records the failure. The budget is the correct knob.
It is a fixed constant, not derived from available VRAM/RAM — a known ceiling:
a three-way overlap or an 8K wallpaper would thrash again.

## Where the remaining time goes

- **Shader translation has no disk cache.** In-memory include/preprocess,
  compatibility, and GLSL translation caches do exist (source checked
  2026-09-06); they can serve repeated work within a process. The historical
  per-wallpaper translation measurement averages
  239 ms (median 229 ms, worst 925 ms): ~175 ms in the compatibility regex
  passes, ~52 ms in glslang→SPIR-V→spirv-cross (`GLSLContext::toGlsl`). Per-pass
  preprocessing averages only 1.4 ms but reaches 424 ms on the 2555-pass
  wallpaper. Process restarts lose the in-memory caches. → [[Shader Translation]].
- **Shader/effect-generated textures can still miss prefetch.** The 3D model
  material gap is fixed (2026-09-08): `collectProjectTextures` walks `Model3D`
  submesh materials as well as `Image`, `Particle`, and `Text`, including
  puppet clipping-mask assets. Pokemon - Deep Sea Dive drops from 129 sync
  loads to 2 (`gradient/gradient_toon_smooth` and `util/black`). The remaining
  shader defaults are discovered during shader preprocessing, after prefetch.
- **Saturn 3589454154 spends ~2.4 s parsing `project.json`** on the loader
  thread (request-to-visible 2.5 s vs ~0.9 s for comparable wallpapers). It is
  off the render thread, so it delays the switch without stuttering.
- **One ~1 s frame is still unexplained**: `worst_frame_ms` 1788 against a
  maximum measured `switch.apply` stall of 792 ms. Something outside the
  instrumented phases blocks the render thread.
- **Steady-state (post-load) frame cost has never been measured.**

## Allocator behavior

Heavy switches leave glibc arena slack that `malloc_trim` cannot return because
it sits mid-arena — 232 MiB stuck per switch. `main.cpp:47-49` caps this with
`mallopt (M_ARENA_MAX, 2)`, and `applyPreparedSwitch` schedules a
`malloc_trim (0)` ~5 s after a switch settles. Watch RSS over 4–5 switches when
touching either.

## How to measure

```bash
# cold start: startup.* metrics, one process per wallpaper
WPE_HEALTH_REPORT=/tmp/lwe.json ./build-new/output/linux-wallpaperengine \
    --window 0x0x640x360 --silent --fps 15 <workshop-id>

# switches: private socket so the live desktop instance is not hijacked
WPE_HEALTH_REPORT=/tmp/lwe.json WPE_CONTROL_SOCKET=/tmp/lwe.sock \
    ./build-new/output/linux-wallpaperengine --window 0x0x640x360 --silent --fps 30 <id> &
echo "switch default /path/to/workshop/content/431960/<other-id>" | socat - UNIX-CONNECT:/tmp/lwe.sock
echo memstats | socat - UNIX-CONNECT:/tmp/lwe.sock   # heap + tex_bytes/tex_ref_bytes + FBOs
```

Read `counters["texture.sync_load"]` and the `details` samples for
`switch.prepare` / `switch.apply` out of the JSON. `--fps` throttling does not
distort the phase timings, which are wall-clock spans, but it does change
`avg_fps`.

## Related Concepts

- [[Debugging Workflow]] — the env-gated hooks and the corpus validator.
- [[Shader Translation]] — what the 239 ms is spent on.
- [[Texture File Format]] — what is being decoded.
- [[TODO Backlog]] — the open items listed above.
