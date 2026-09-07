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
`src/WallpaperEngine/Debug/README.md`, usage in [[Debugging Workflow]]). Nothing
here is inferred from code reading; every number below came from that report.

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
loader thread reports `upload ~0 ms`. Every number on this page was taken in a
640×360 GLFW window, which means the render-thread stall figures are an
**upper bound** — on the live Wayland desktop that upload is off-thread. The
GLFW window-mode screen key is `"default"` (`GLFWWindowOutput.cpp:31`), needed
for socket commands.

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
- **`collectProjectTextures` misses whole asset classes**, leaving ~9
  render-thread `texture.sync_load` hits per switch in the recorded run.
  Source checked 2026-09-06: there is still no `Model` (3D) branch; it walks
  `Image`, `Particle`, and `Text`, including puppet clipping-mask assets.
  Reidentify missed stock/shader-generated references instead of treating
  all `masks/*` paths as absent from prefetch.
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
