---
type: Investigation
title: Video Texture Memory Growth
description: Sustained host-memory growth on wallpapers with embedded video textures, traced to an upstream libmpv GL fence leak fixed for mpv 0.42.0, plus a smaller residual traced to NVIDIA EGL event queues; deployed dependency fixes still need verification.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, mpv, video, memory, leak, wayland, nvidia, bug]
timestamp: 2026-07-29T00:00:00-04:00
---

# Video Texture Memory Growth

**Symptom:** wallpapers containing an embedded video texture grow host memory
until the engine freezes and crashes. Reported against Radiant Emerald
(`2722446525`) on two monitors; user-observed RSS reached roughly 1.5–1.7 GiB
before the crash.

**Recorded investigation status (reconciled 2026-09-06):**

- **Dominant cause diagnosed: upstream libmpv GL fences.** The July record
  identifies a fix targeted at mpv 0.42.0 and a locally validated patch.
  No claim is made here about today's latest release or installed library.
- **Residual ~1.7 MiB/min diagnosed: NVIDIA EGL surface event queues.**
  The later evidence below supersedes the original unresolved libwayland
  attribution. Disabling explicit sync was measured and rejected for FPS cost.
- **Still to verify:** deployed libmpv/driver fix status, live memory slope,
  and advancing video. See the [project checklist](../../Organized%20all.md).

## Measured rates

Radiant Emerald, both monitors, 240 s runs with a 60 s warm-up, fitted by least
squares. Only one variable changes per row.

| configuration | heap slope | RSS slope |
| --- | --- | --- |
| baseline, `hwdec=auto` (nvdec) | +9.659 MiB/min | +12.414 MiB/min |
| `mpv_render_context_update` gate | +9.422 MiB/min | +12.533 MiB/min |
| `hwdec=no` (software decode) | +9.521 MiB/min | +14.287 MiB/min |
| mpv render call skipped entirely | **+1.676 MiB/min** | +4.139 MiB/min |

Textures stayed at 172 entries / 102.2 MiB and FBOs at 44 / 549.0 MiB in every
run, so this was never texture or FBO accumulation. Skipping the mpv render call
removes 83% of the growth, and the remainder matches the no-video control
wallpaper Starfall (`2915841260`) at ~1.7 MiB/min.

## Root cause: leaked GL fences in vo_libmpv

`ra_gl_ctx_submit_frame` in `video/out/opengl/context.c` appends a `GLsync` to
`p->vsync_fences` for every submitted frame. The only code that drains that
array — `ClientWaitSync`, `DeleteSync`, `MP_TARRAY_REMOVE_AT` — lives inside
`ra_gl_ctx_swap_buffers`, which is **never called for the render API**. Both the
fence objects (driver-side, hence the libnvidia-eglcore attribution) and the
array itself grow without bound.

Confirmed independently of this engine: a ~150-line EGL-pbuffer + libmpv harness
with zero engine code leaks at **5260 bytes per `mpv_render_context_render`
call**, against **5208 bytes/call** measured by heaptrack inside the engine — a
1% match. The harness lives at `.claude/`-adjacent scratch during the
investigation; recreate it from this document if needed.

Upstream: <https://github.com/mpv-player/mpv/pull/17303>, merged 2026-01-23,
milestone Release v0.42.0. Closes mpv issues #15099, #8516, #17301, #17217.

### Why 0.41.0 is affected

The regression came from removing the external swapchain API, which had
previously suppressed the `FenceSync` call:

| commit | date | in v0.41.0 |
| --- | --- | --- |
| `8854c742` remove pointless external_swapchain call | 2025-02-21 | yes |
| `5ae0e0ff` remove remnants of the external swapchain API | 2025-02-23 | yes |
| the fix | 2026-01-22 | no |

v0.41.0 was released 2025-12-21, so it contains both regression commits and not
the fix.

### The fix, and why it cannot break normal playback

```c
-    if (gl->FenceSync) {
+    if (gl->FenceSync && p->params.swap_buffers) {
```

`params.swap_buffers` is set by every real platform backend — `context_glx.c`,
`context_wayland.c`, `context_x11egl.c`, `context_drm_egl.c`, `context_win.c`,
`context_angle.c`, `context_android.c`, `context_dxinterop.c`. It is **not** set
by `libmpv_gl.c`, which contains no reference to it at all. So the guard changes
behaviour for exactly one consumer, `vo_libmpv`, where the fences were created
and then never consumed. Every ordinary mpv playback path is unaffected.

### Building a patched libmpv without touching system packages

```
git clone https://github.com/mpv-player/mpv.git
cd mpv && git checkout -B fence-fix v0.41.0
# apply the one-line change above to video/out/opengl/context.c
meson setup build -Dlibmpv=true -Dcplayer=false -Dvulkan=disabled -Dlua=disabled
ninja -C build
```

Then run the engine with `LD_LIBRARY_PATH=<mpv>/build` so only it picks up the
patched library. Verify with `ldd` that `libmpv.so.2` resolves to the build
directory. Do not install over the system package without explicit approval.

**Verified locally 2026-07-29** with the standalone harness, 180 s runs, 60 s
warm-up, identical 29.2 renders/s in both arms:

| libmpv | slope | bytes per render |
| --- | --- | --- |
| system 0.41.0 | +8.789 MiB/min | 5260 |
| patched 0.41.0 | **+0.124 MiB/min** | **74** |

A 98.6% reduction. Caveat on rigour: the patched arm's `hwdec=auto` resolved to
`vulkan-copy` while the system arm resolved to `nvdec`, because the local build
was configured with `-Dvulkan=disabled` and picks differently. A decoder-matched
re-run with `hwdec=no` on both was attempted and produced no usable data — the
machine suspended mid-run, stalling `CLOCK_MONOTONIC` and wedging the harness.
The confound is nonetheless covered from two directions: decoder choice provably
does not affect the slope (see the ruled-out list above, nvdec +9.659 against
software +9.521 in-engine), and the fence is appended in `ra_gl_ctx_submit_frame`
per rendered frame, which is downstream of decoding entirely.

Note the PR's commit SHAs do not exist in the upstream clone — GitHub rebases on
merge — so cherry-picking by the PR SHA fails. Apply the one-line change
directly and confirm the resulting `git diff` blob hashes match the published
patch (`86e8d5daac` → `86e5c797d6`).

## Ruled out — do not re-test without new evidence

- **Hardware decoding is irrelevant.** `hwdec=auto` (nvdec interop) and
  `hwdec=no` (pure software) leak at the same rate. The upstream issue reports
  the same.
- **`profile=fast` and `vd-lavc-dr=no`** change nothing (5368 and 5343
  bytes/render against a 5260 baseline).
- **FFmpeg version mismatch is a non-issue.** mpv links `libavcodec.so.62` and
  `libavutil.so.60`; the installed FFmpeg provides exactly those sonames, so the
  8.0.1-vs-8.1.2 difference is a minor bump within one ABI.
- **`mpv_render_context_set_update_callback` gating does not help.** The engine
  already renders roughly 1:1 with the video's frame rate — heavy scenes run at
  ~15 engine fps, not the 145 fps target, so `mpv_render_context_update` never
  reports "no new frame" and there is nothing to skip.

## Residual growth: SOLVED, upstream NVIDIA (workaround rejected)

**Root cause: `wl_closure` objects for `wl_buffer` events accumulating in
per-EGL-surface Wayland event queues that NVIDIA's `libEGL_nvidia.so` creates
but never dispatches.** One queue per monitor, which is why dual-monitor doubles
the rate.

Method that found it, in order:

1. Built libwayland 1.25.0 (exact installed version) with debug symbols, since
   Arch's debuginfod has no debug info for it. heaptrack then resolved the stack
   to `wl_closure_init::zalloc` (`wayland-private.h:265`) <-
   `wl_connection_demarshal` (`connection.c:913`) <- `queue_event`
   (`wayland-client.c:1650`).
2. Read the routing at `wayland-client.c:1663-1672`: a closure is allocated when
   an event is queued and freed when dispatched. `wl_display_dispatch()` drains
   only the default queue; another library's queue is its own responsibility.
   `display_queue` (which carries `delete_id`) *is* drained at lines 1869 and
   1898, so that is not it.
3. Patched the local libwayland to count queued against dispatched per named
   queue (`WLQ_DEBUG=1`). Every queue balanced at backlog 0-1 except two named
   `EGLSurface(<id>/<ptr>)` carrying `wl_buffer`, at **2588 and 5331 backlog
   with 7 dispatched each** over 70 s.
4. `libEGL_nvidia.so.610.43.03` is the only mapped library containing the
   `EGLSurface` queue-name strings.

Arithmetic reconciles: 17,312 leaked allocations over the 120 s capture at
~170 bytes each = 2.95 MB / 120 s = **1.47 MiB/min**, matching the measured
residual.

Cause is explicit sync. The protocol trace shows NVIDIA using
`wp_linux_drm_syncobj_surface_v1.set_acquire_point`/`set_release_point`, so it no
longer needs `wl_buffer.release` events to reclaim buffers — but the compositor
still sends them and the per-surface queue that used to consume them is left
undrained.

### Workaround exists but is NOT worth taking

`__NV_DISABLE_EXPLICIT_SYNC=1` (a string in `libnvidia-egl-wayland.so.1.1.21`)
makes NVIDIA drain the queues again — verified, backlog drops from 2588/5331 to
1/1. Hyprland 0.56 has no compositor-side equivalent; `render:explicit_sync` was
removed.

The framerate cost makes it a bad trade:

| wallpaper | sync ON | sync OFF | fps change |
| --- | --- | --- | --- |
| light scene `3726503096` | 98.78 fps, +1.730 MiB/min heap | 85.28 fps, **+0.198 MiB/min** | **-14%** |
| Radiant, dual monitor | 32.27 fps | **17.37 fps** | **-46%** |

On a GPU-bound scene, dropping explicit sync forces the client to wait on buffer
release events and roughly halves the framerate. Trading 46% of the framerate to
recover ~1.5 MiB/min is not worth it, especially now the far larger mpv leak is
fixed separately. **Recommendation: leave explicit sync enabled and treat the
residual as an NVIDIA driver bug to be fixed upstream.**

## Residual growth (superseded — original notes)

With mpv rendering disabled, growth persists at **+1.68 MiB/min**, sustained
over a 5.75-minute window with the arena tracking in lockstep and no plateau.
The rate is roughly the same on a 3-texture scene and on a 63-texture one, so it
scales with frames or events rather than scene content.

A valid 120 s heaptrack of `3726503096` ("Beneath The Seventh") leaked 3.42 MB
total, of which **3.09 MB (90%) is libwayland-client** — 2.91 MB over 73,778
calls under `wl_display_read_events` ← `WaylandOpenGLDriver::dispatchEventQueue`,
plus 174 KB under the NVIDIA EGL Wayland platform in
`WaylandOutputViewport::swapOutput`. The remaining leaked bytes (libtasn1
`asn1_array2tree`, gnutls, both via `ld-linux`) are one-time library init.
`/proc/<pid>/smaps` attributes 86% of RSS growth to `[heap]`, so this is
malloc'd memory rather than driver mappings.

Checked and clean, so do not re-investigate:

- Wayland protocol object lifetimes balance — 13,229 created
  (`wl_display.sync`, `wp_presentation.feedback`, `wl_surface.frame`) against
  13,238 `wl_display.delete_id`.
- `wl_callback_destroy` is called correctly in `surfaceFrameCallback`.
- `VectorAdapter` already registers `vector_finalizer`.

**Blocked on:** libwayland-client has no debug symbols installed, so the top
frame resolves only to an address. Installing symbols would name the function.

**Latent bug found while chasing this, not the cause:**
`WaylandOutputViewport::swapOutput` overwrites `frameCallback` with a fresh
`wl_surface_frame` proxy without destroying a still-pending one. The steady-state
callback loop is 1:1 so it does not currently fire, but there is no guard.

## Engine-side fixes committed during this investigation

These are real leaks, but all are small relative to the mpv one.

- `fix(script): release native layer adapter wrappers` — `ScriptableObjectAdapter`
  handed QuickJS a heap wrapper per object with no finalizer.
- `fix(media): release DBus error payloads` — `dbus_error_init` was called twice
  and `dbus_error_free` never.
- `fix(cli): report unrecognized command line arguments` — `parse_known_args`
  returned unmatched options and the result was discarded.

## House rule: the fullscreen-pause flag

The real option is **`--no-fullscreen-pause`**. `--no-full-screen-pause` is not a
flag and was silently ignored before the CLI fix above, leaving
`pauseOnFullscreen` enabled. That keeps the Wayland fullscreen detector
installed, and `anythingFullscreen()` performs a blocking `wl_display_roundtrip`
on **every main loop iteration** — measured at roughly 214 per second. Every
reproduction command used during this investigation carried the typo, so those
runs all had fullscreen detection unintentionally active. Check launcher configs
for the same mistake.

## Parked: dual-monitor framerate is a raw compute limit

Measured on Radiant Emerald with `--fps 145`, using `WPE_HEALTH_REPORT`:

| configuration | avg_fps | per-frame cost | limited by |
| --- | --- | --- | --- |
| HDMI-A-1 alone (1080p @ 60 Hz) | 60.34 | — | vsync |
| DP-1 alone (1440p @ 165 Hz) | 63.50 | 15.7 ms | GPU/CPU |
| both monitors | 32.44 | 30.8 ms | both, serialized |

`wl_display_dispatch` blocks, and both outputs render inside that one call on a
single thread sharing one EGL context, so cost per iteration is simply output A
plus output B. Predicting the dual figure from the two solo numbers gives
**30.94 fps against 32.44 measured** — a near-exact fit, which is what confirms
the serialization model.

DP-1 alone at 63.5 fps against a 165 Hz panel is the key number: that is not
vsync, it is the wallpaper genuinely costing ~15.7 ms per frame on an RTX 3080
with 22 FBOs and 189 MiB of render targets per output.

**No zero-cost framerate win exists here.** Tested and measured at zero effect:

- Disabling the Wayland fullscreen detector with the correct
  `--no-fullscreen-pause` flag: 32.44 with it on against 32.27 with it off. The
  blocking per-frame `wl_display_roundtrip` goes to an idle compositor on a
  separate connection and overlaps GPU work, so it never stalls anything.
- The mpv fence patch is framerate-neutral: 32.27 (system) against 32.45
  (patched).

Real options all trade quality or scope: one monitor only (60–63 fps), lower
scaling, `--disable-particles`/`--disable-parallax`, or a lighter wallpaper. The
only remaining avenue for more framerate at unchanged quality is GPU-profiling
the 22-pass effect chain (RenderDoc) to look for redundant or needlessly
full-resolution passes. Not attempted.

## Verification notes

- Do not use grim/PNG diffs as rendering proof. Video animation was confirmed
  without screenshots by logging mpv's `time-pos` and counting loop wraps
  (`range=[0, 3.96667]`, `loops=60`).
- When capturing with heaptrack, `LD_PRELOAD` must be applied to the engine and
  **not** to a wrapper such as `timeout`. A prior capture in this investigation
  was invalid because `timeout` itself was instrumented: 12 modules, one
  allocation, RSS flat at 7292 KiB. Always validate a capture by confirming it
  contains `linux-wallpaperengine`, `libmpv`, and `libnvidia-eglcore` modules
  before trusting it.
