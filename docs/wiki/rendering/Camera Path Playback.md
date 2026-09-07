---
type: Rendering Concept
title: Camera Path Playback
description: Curve and legacy camera path parsing, per-frame Bezier evaluation, and playback modes recovered from wallpaper64.exe.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine/src/WallpaperEngine/Data/Model/CameraPath.cpp
tags: [linux-wallpaperengine, camera, animation, reverse-engineering]
timestamp: 2026-08-09T00:00:00-04:00
---

# Camera Path Playback

Scenes drive the 3D camera with authored shots. Two formats exist and both are
supported:

- **Legacy** — a `transforms` array of timestamped center/eye/up/zoom entries,
  interpolated with the Hermite variant that gives both ends of a segment the
  same tangent (half the segment's value delta).
- **Curve** — the modern editor output: `options` plus `center`/`eye`/`up`
  channel groups (`c0`/`c1`/`c2`), `fov`, and `zoom`. Camera objects reference
  a separate `scripts/camera_paths_<id>.json`; older scenes list files in
  `camera.paths`.

Both feed `CameraPathSource`, and both enable the camera fade.

## Reference binary evidence

`wallpaper64.exe`, sha256
`40e2ce021e9352324fadb3b8f72b8ba2a7ee95b71cc571d5b9f84be75cd993b0` (build of
2026-07-08). The parse/evaluate cluster sits at `0x1401a8000`–`0x1401ab000`:

| Address | Role |
|---|---|
| `1401a8c10` | folds `mode`/`random`/`startpaused` into the options flag word |
| `1401a8ce0` | keyframe array parse (`frame`, `value`, `step`, `back`, `front`) |
| `1401a96b0` | options parse (`length`, `fps`, `mode`, `random`, `startpaused`, `wraploop`) |
| `1401a98b0` | wraploop closer: trims past `length`, appends the closing keyframe |
| `1401a9410` | `events` array parse (`frame` scaled by seconds-per-frame) |
| `1401a9bc0` | per-frame Bezier bake — the evaluator |
| `1401a9f60` | playback advance: loop / mirror / single, paused and finished bits |
| `1401f1bc0` | `c0`/`c1`/`c2` vector channel group |
| `1401f2030` | whole-path parse |
| `1401f2ad0` | camera sampler: queue selection plus the two-frame blend |

The option-name strings have no direct Ghidra xrefs; sweep the region with
`XrefsInRange.java 14048ee80 14048ef00` instead of `FindStringXrefs.java`.

## Keyframe layout

Each keyframe is 28 bytes of 7 32-bit fields: `frame`, `value`, `flags`,
`back.x`, `back.y`, `front.x`, `front.y`. Flags are `1` = back handle enabled,
`2` = front handle enabled, `4` = `step`.

Handle offsets are only read when that handle is `enabled`, so a disabled
handle keeps a zero offset. The evaluator never tests the enable bits — it
reads the offsets — which is why zeroing them matters.

Keyframes are **not sorted**: the loader keeps an entry only when its `frame`
is strictly greater than the last kept one, so out-of-order or duplicated
frames are discarded.

Editor-only keys — `magic`, `lockangle`, `locklength`, and `options.cameramode`
— appear in scene data but have no string in the runtime at all.

## Evaluation

Values are baked **per whole frame**, then the two frames around the playhead
are blended:

```
lower = clamp(trunc(time / secondsPerFrame), 0, length - 1)
upper = min(lower + 1, length)
blend = fmod(time, secondsPerFrame) / secondsPerFrame
value = channel(lower) * (1 - blend) + channel(upper) * blend
```

Within a segment, `step` on the *later* keyframe holds the earlier value for
the whole span. Otherwise a cubic Bezier is solved in the frame/value plane:

- Time control points are `prev.frame + prev.front.x * span * 0.5` and
  `next.frame + next.back.x * span * 0.5`. **The x offsets are normalized
  against the segment**, so `1.0` reaches half of it — they are not seconds and
  not frames.
- Value control points are `prev.value + prev.front.y` and
  `next.value + next.back.y`, absolute offsets with no scaling.
- The curve parameter is found by bisection: seed it with the segment-relative
  position, start the step at `0x1.ff7ceep-1` (just under 1.0), halve it every
  iteration, and stop once the solved frame is within `0.01` frames or after
  1000 iterations. That epsilon is why off-midpoint samples carry a little
  slack.

With both handles disabled the time and value curves share their weights, so
the segment is exactly linear.

## Playback modes

`options.mode` and the booleans become a flag word whose bits the runtime tests
directly: `0x1` mirror, `0x2` single, `0x4` random, `0x10` wraploop,
`0x20000000` startpaused, plus two runtime-only bits, `0x40000000` finished and
`0x80000000` reversed.

- **loop** (no mode, the default) — wraps with `fmod` and never ends. A looping
  shot therefore never advances the queue.
- **mirror** — plays to the end, sets the reversed bit, runs backwards to zero,
  clears it, and repeats. Also never advances the queue.
- **single** — stops on the last frame and sets the finished bit. The sampler
  sees that bit, clears it, resets the playhead, and moves to the next path.
- **startpaused** — parks the shot on its first frame. Nothing in this port
  resumes it, which matches the reference absent an external resume.

`wraploop` is applied at parse time, not playback: every channel is trimmed to
`length` and gains a closing keyframe carrying the first keyframe's value and
the negated first `front` handle, so the seam is continuous.

## Debug flags

- `WPE_CAMERA_TRACE=1` — logs the active shot name, playhead, flags, eye, and
  FOV every frame.
- `WPE_CAMERA_QUEUE=sequence|random` — forces every camera path queue, so a
  shot order is reproducible instead of reshuffled per run.

Klonoa and Huepow 2885298446 forced to `sequence` plays all 22 shots in the
authoring order of `scripts/camera_paths_138.json`, 10.0 s each, then wraps:
Moon Zoom Out, Angled 1, Angled 2, Moon Angled, Klonoa, Huepow, Phantomile,
Moon Sweep, Heroes to Moon, Moon Down, Klonoa Zoom, Full Scene Reveal, Moon
Focus, Behind Angled, Huge Moon, Distant 1, Above, Across Front, Whole
Platform, Bosses A, Bosses B, Full Scene.

Its other five camera objects hold zero paths and are gated behind the
`cameratype` user property, so only Dynamic Cam ever plays.

## Checking pacing against the reference

Bucketing the traced eye displacement per second shows whether the authored
handles are doing anything. Klonoa and Huepow 2885298446, shot "Huepow":

| t (s) | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| speed | 0.23 | 0.29 | 0.31 | 0.33 | 0.33 | 0.31 | 0.28 | 0.22 | 0.10 |

That is the ease the handles ask for. A flat row would mean the handles are
being ignored.

Do not read a straight-line *trajectory* as a bug. Both 2885298446 and
3562141459 author every channel with exactly two keyframes and zero handle `y`
offsets, so the camera travels a straight line between two poses at a varying
rate — a curved trajectory needs intermediate keyframes the scenes do not have.

## Not implemented

`options.events` (named callbacks fired as the playhead crosses a frame) is
parsed by the reference at `1401a9410` but ignored here — no installed
wallpaper authors a non-null `events`.

## Related Concepts

- [3D Scene Support](3D%20Scene%20Support.md)
- [WE Reference Mining](../reference/WE%20Reference%20Mining.md)
- [Debugging Workflow](../workflow/Debugging%20Workflow.md)
