# Wallpaper corpus findings

## 2026-07-20 reported set

These results came from the real two-monitor Wayland layer-shell session, with
DP-1 switched over the control socket and restored to `3420988161`. They are not
small-window validator results.

| Wallpaper(s) | Original symptom | Finding / fix | Live result |
|---|---|---|---|
| `3318541129` | Crash when selected / cold-started | Its packaged `effects/fluidsimulation/effect.json` has a trailing comma before the closing `dependencies` bracket. Wallpaper Engine's JsonCpp reader accepts this, while the strict nlohmann parser threw at line 413 and terminated startup. Packaged asset JSON now retries through a string-aware compatibility pass that removes only commas immediately before `]` or `}`; unrelated malformed JSON still fails. | The exact two-monitor cold-start failed before the fix with `json.exception.parse_error.101`. After the fix, PID `1857533` rendered it on DP-1 for over six minutes, then survived a real switch away and back. The control socket remained responsive with 92 live FBOs / 386,938,296 bytes across both monitors. |
| `3061226599` | Crash while loading / shortly after rendering | The scene binds a legacy 2D pivot script to the `Stone 2`/`Stone 3` 3D models and reads `thisLayer.size`. MDLV submesh bounds were skipped, so the model layer exposed no size and native `VectorAdapter` conversions could throw C++ exceptions through QuickJS. The parser now aggregates MDLV bounds, `CModel` exposes their size to SceneScript, vector operands become JavaScript `TypeError`s inside the native callback, and a failing authored update hook is disabled after its first error instead of retrying every frame. | Before the fix, the exact two-monitor cold start exited during scene construction with `Unsupported type conversion for VectorAdapter`; an intermediate build rendered for 11.020 seconds before SIGSEGV in the unsafe QuickJS error path. The final build remained active as PID `1890824`, survived a live selector switch away and back, served both debug socket commands, and reported 100 live FBOs / 776,627,960 bytes with the 5120x2160 scene targets present. |
| `3562141459` | Crash after rendering; large memory growth while cycling | A complex 3D scene creates about 613 render passes. `CPass` leaked the parsed/preprocessed `Shader` for every pass. The pass now owns it with `unique_ptr`; shader parameters, uniforms and attributes are owned too. | Three switch/restore rounds survived. Active RSS was 1.11-1.16 GB; restored RSS stayed at 754-758 MB. |
| `2444472355` -> `1545949115` | Freeze during the handoff | Wallpaper replacement is transactional: the old Project is not moved/destroyed until the replacement `CWallpaper` has constructed successfully. Partially built `CScene` objects unwind safely. | The exact handoff survived once, but the 50-unique memory run later stalled while `2444472355` was selected and stopped servicing several control requests. Treat this wallpaper as still unresolved and exclude it from cache stress runs. |
| `3759799379`, `3509578940`, `3696819731` | Cold-start stop/freeze/crash or failure to load | Cores converged on `AudioStream::resampleAudio`. Stereo resampler output was being written into storage sized using the mono input channel count, corrupting the heap. Resample sizing now uses the output layout, packet state is per stream, and shutdown drains queued packet references. | All three survived with audio enabled. |
| `3737268876` | Crash | This scene legitimately starts many sound streams: the live run reached 117 threads and 1.66 GB RSS while active. Thread count returned to 46 after switching away. Audio teardown now synchronizes and frees the streams/packets. | Survived and switched away cleanly. |
| `3465215190`, `3462491575`, `3417957645` | Freeze only after switching from another wallpaper | Switch failure/unwind safety and deterministic default pass matrices remove the cold-start-versus-rebuild lifetime/state difference. | Each survived a preceding wallpaper and switched away cleanly. |
| `3708206626` | Sonic and parts of the 3D track appeared transparent, black, or missing | Two 3D defaults interacted: the chase camera is a child of the script-driven `CameraBoneMoveMesh`, but the renderer used only a parse-time local camera pose; and 35 opaque `StageAuto` material passes omit `depthtest`/`depthwrite`, which were incorrectly defaulted off. Perspective cameras now follow the selected camera layer's live parent-composed transform after SceneScript, while omitted depth state defaults on only for 3D model materials (explicit material state still wins). | On HDMI-A-1 the original frame showed the track painting through Sonic. The rebuilt full scene rendered Sonic opaque with the lit track correctly occluded, remained active for over four minutes, and the test build passed 345 assertions in 49 cases. |

`3562141459`, `2444472355`, `1545949115`, `3737268876`,
`3465215190`, `3462491575`, `3759799379`, `3417957645`,
`3509578940`, and `3696819731` were each left active for 15 seconds in one
continuous live-session pass. The engine remained alive after every item and
reported no fatal switch/load or allocator errors.

## Memory interpretation

Measure the restored baseline, not only the active peak. 3D models, decoded
textures and dozens of audio streams can make an active scene substantially
larger without being a leak.

Before the ownership fix, repeated `3562141459` rounds left the restored main
process RSS climbing from roughly 1.32 GB to 1.71 GB to 2.13 GB. With the fix,
the clean baseline was 821 MB, active 3D peaks were 1.11-1.16 GB, and restored
RSS was 754 MB, 758 MB and 755 MB.

The include-preprocess, GLSL-compatibility and GLSL-translation caches also used
entry-count limits even though shader entries vary enormously in size. They are
now each limited to 64 MiB of retained shader text, preventing a large mixed
library from filling a count-based cache with hundreds of unusually large
sources. The texture cache also re-checks its 512 MiB budget after transition
teardown; previously it could remain over budget when the outgoing wallpaper
was still referenced during the last insertion. A one-time rise as these
bounded caches warm is expected; linear post-restore growth is not.

A separate cache stress run loaded 24 different installed wallpaper IDs (not
the reported set) in one real session. Whole-cgroup memory started at 3.798 GB,
briefly peaked at 3.856 GB on a heavier active wallpaper, and finished at
3.561 GB after restoring `3420988161`. Main-process RSS finished at 1.010 GB
versus 838 MB at startup.

### Unique-wallpaper cache leak

The later full-library run disproved the initial bounded-cache interpretation.
Across 165 unique wallpapers, the main PID rose from 577,372 kB to a
12,116,736 kB peak and still used 10,056,376 kB after restoring `2297432332`.
It later crashed in CEF's `MallocDumpProvider::OnMemoryDump`. This was not an
OOM kill and the retained RSS was predominantly anonymous memory.

The texture budget existed but eviction accidentally excluded every authored
texture. Scoped keys have this form:

```
<asset-locator fingerprint>\x1f<texture filename>
```

The fingerprint begins with the virtual `$mediaThumbnail` mount, so all scoped
keys begin with `$`. The eviction predicate used `key.starts_with('$')` to pin
the two runtime album-art aliases and therefore treated every workshop texture
as permanent. A focused instrumented replay showed the cache growing from
40.5 MB / 7 entries to 704.7 MB / 154 entries in ten switches even though only
69.2 MB / 16 entries were still referenced by the active wallpaper.

The fix pins a `$...` key only when it is unscoped (contains no `\x1f`
separator). Cache accounting now asks each texture provider for its actual
approximate GPU footprint; `CTexture` includes every image and mip level rather
than estimating only image zero. `memstats` also reports texture-cache and live
FBO counts/bytes so future runs can distinguish cache retention from an active
high-cost scene.

Post-fix, 49 unique wallpapers (the first 50 with unresolved `2444472355`
excluded) kept `tex_bytes` at or below the 536,870,912-byte budget throughout.
After restoring `2297432332`, the main PID used 844,004 kB RSS and NVIDIA
reported 860 MiB, versus 2,590,452 kB and 4,136 MiB in the pre-fix 50-item run.
Live FBO state returned to the exact baseline: 8 objects / 104,093,796 bytes.
`2955378002` legitimately reached 4,627,824,668 live FBO bytes while active,
then dropped to 507,072,988 bytes on the next wallpaper; do not classify such
an active peak as retained memory.

Raw measurements are in:

- `validation-output/unique-memory-pid-1606704-combined.csv` (pre-fix full library)
- `validation-output/fixed-build-unique-memory-pid-1657138-batch01-part2.csv` (pre-fix focused batch)
- `validation-output/postfix-cache-memory-pid-1739154-batch01.csv` (post-fix 49-item batch)

## Other fixes exposed by the corpus

- The validator uses `WPE_CONTROL_SOCKET` so it cannot steal the real desktop
  engine's Unix socket.
- Strict desktop-GLSL compatibility rewrites are centralized in `ShaderUnit`.
  The previous failing corpus set (139 emitted units across 23 wallpapers)
  batch-compiled after the compatibility fixes.
- `ScriptEngine` teardown no longer calls JavaScript setters/GC while the
  runtime graph is already unwinding; freeing the context/runtime owns that
  cleanup and avoids teardown exceptions and crashes.
