---
type: Playbook
title: Debugging Workflow
description: Steps for isolating linux-wallpaperengine rendering bugs and validating object-level output.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, debugging, rendering, workflow]
timestamp: 2026-08-19T00:00:00-04:00
---

# Debugging Workflow

## Isolate and screenshot a single object (engine's own tooling)
```
./linux-wallpaperengine --window 100x100x960x540 --fps 30 \
  --screenshot /tmp/obj.png --screenshot-delay 30 \
  --render-debug object=<id> <workshop-dir>
```
Other `--render-debug` modes: `base-only`, `no-solid-final`, `pass-log`,
`skip-object=<id>`, `skip-effect=<id>`. `pass-log` prints every pass with
shader, target and FBO — first stop for compositing questions.

For a fast live triage frame, use `grim -o DP-1 /tmp/lwe-live.png` (replace
`DP-1` with a name from `hyprctl monitors -j`). This is useful for spotting an
obvious missing object or texture, but it is not final rendering proof because
animation time, windows, compositor state, and stale processes can differ. Ask
the user to confirm live appearance. The engine's isolated `--screenshot` path
is deterministic enough for object-level debugging.

## Permanent debug hooks

Env-gated debug helpers live in `src/WallpaperEngine/Debug/`; the ledger in
`src/WallpaperEngine/Debug/README.md` lists every hook. All hooks are strict
no-ops unless their environment variable is set, so they ship in normal
builds. Currently:

- `WPE_DUMP_SHADERS=<dir>` dumps every fully composed shader unit (including
  compat-cache hits) for offline glslang triage.
- `WPE_JSON_TELEMETRY=1` logs every authored JSON key no parser consumed —
  the fastest way to find fields we don't implement yet without reading the
  format spec cold. Run it across a corpus and rank by frequency.
- `WPE_HEALTH_REPORT=<path>` (`-` = stdout) writes a JSON summary of error/
  exception counts, frame timing, and load-phase timings on every exit path.
  This is what `tools/validate-corpus.py` (below) reads back per item. Load
  metrics: `startup.project_load`, `startup.texture_prefetch`,
  `startup.scene_build`, `startup.first_frame` (process start → first frame,
  i.e. the whole load cost), `switch.prepare` (loader thread: project parse,
  read+decode, shared-context upload), `switch.apply` (render-thread stall, of
  which scene build, plus request-to-visible), and `texture.sync_load` (one per
  texture the cache had to decode on the calling thread — during a scene build
  that is the render thread, so a high count means the prefetch missed or
  `trim ()` evicted it). Interpretation and measured baselines:
  [[Load Performance]].

### Every environment variable the engine reads

The `Debug/README.md` ledger covers only the four hooks above. These also
exist and are not in it:

| Variable | Added | Effect |
|---|---|---|
| `WPE_LOG_FILE=<path>` | 2026-07-28 `965fadcf` | Persists structured engine diagnostics to a file. The desktop launcher detaches the engine with stdout/stderr on `/dev/null`, so **this is the only way to get logs out of a live desktop wallpaper** — a windowed repro is otherwise the only option. |
| `WPE_SCRIPT_TRACE` | 2026-08-18 `878a4ca4` | Traces SceneScript execution. See [[SceneScript Runtime]]. |
| `WPE_CAMERA_TRACE=1` | — | Logs active shot name, playhead, flags, eye, FOV per frame. See [[Camera Path Playback]]. |
| `WPE_CAMERA_QUEUE=sequence\|random` | — | Forces camera path queue order so shot sequence is reproducible. |
| `WPE_AUDIO_DEBUG=1` | — | Logs RMS and raw bands per capture block, including silent ones, to `/tmp/we-audio-debug.log`. |
| `WPE_AUDIO_SCALE` | — | Scales captured audio magnitude (`PulseAudioPlaybackRecorder.cpp`). Replaced the old `WPE_AUDIO_GATE` RMS noise gate, which no longer exists. |
| `WPE_CONTROL_SOCKET` | — | Control socket path (`switch`, `prop`, `memstats`, `fbostats`). |
| `WPE_CEF_OZONE`, `WPE_CEF_ANGLE`, `WPE_CEF_EXTRA`, `WPE_CEF_NO_IPG` | 2026-07-09 `b404e48f` | CEF backend knobs for web wallpapers — Ozone platform, ANGLE backend, extra command-line switches, and disabling in-process GPU. Reach for these first when a web wallpaper is black or fails to boot. |
| `WPE_NO_STACK_PROTECTOR` | 2026-07-28 `1d193ef1` | Exits CEF helper processes without unwinding main. |

## Live stack monitor — `tools/lwe-monitor`

The desktop launcher detaches both Waypaper and the engine with output pointed
at `/dev/null`, so nothing useful reaches a terminal. `tools/lwe-monitor` is a
live terminal monitor that watches observable runtime state instead of stdout:

- Waypaper's persisted monitor → wallpaper assignments and engine settings
- the live engine process, cgroup, resource use, and **stale/deleted mappings**
  (the `(deleted)` binary trap under *Instance hygiene* below)
- the engine's control socket, including `memstats` and `fbostats`
- the CEF child process tree, coredumps, OOM kills, and kernel GPU faults
- timestamped changes correlated with wallpaper selections

Use it for anything that only reproduces on the real desktop path: memory
growth over hours ([[Video Texture Memory Growth]]), a wallpaper that dies only
when detached, CEF child explosions on web wallpapers, or confirming which
build the running process actually came from.

## Corpus-wide validation

`tools/validate-corpus.py` runs every workshop item in a Steam corpus through
an isolated, muted, windowed engine process, feeding it `WPE_HEALTH_REPORT`
and `WPE_DUMP_SHADERS`, then batch-compiles the dumped shaders with glslang
and classifies each item PASS/WARN/FAIL/SKIP:

```bash
tools/validate-corpus.py --jobs 2 --shader-jobs 6 --duration 2 --out validation-output
tools/validate-corpus.py --ids 2719499501 3558034522    # just a few items
tools/validate-corpus.py --include-assets              # also run non-wallpaper workshop items
```

The short duration starts after scene loading; `--startup-timeout` separately
bounds loading (90 seconds by default). `--jobs` limits concurrent renderer
processes and `--shader-jobs` limits a shared pool. Identical stage/source pairs
compile once across the run, with every item's failures retained. Reports are
updated after each item and expose `complete`, timing and cache statistics.
Keep concurrency within available RAM/VRAM; this is a smoke test, not a soak.

Items without a `scene`/`video`/`web` `type` in `project.json` are workshop
assets (effects, models, audio visualizers importable into other wallpapers,
not standalone) and are SKIPped unless `--include-assets` is given. FAIL
covers hangs, unclean shutdowns, missing health reports, `fatal.exception`,
zero rendered frames, and any glslang shader-compile failure; WARN is any
`log.error`. Shader dumps for PASSing items are deleted unless `--keep-all`;
`report.json` under `--out` keeps full per-item detail for every run.

**Baseline noise (handled)**: this Wayland/GLFW/PipeWire desktop logs `Failed
to initialize GLEW: No GLX display` and a `GLFW error 65548` line on every run,
both counted as `log.error`. They are listed in `BENIGN_ERRORS`, counted per
item from `log.txt`, and subtracted from the `log.error` counter before
classification, so a clean item PASSes again. The subtraction has to come from
the log because `health.json` caps its `details` samples — the counter alone
cannot say which errors were baseline. Add a pattern there if a new
machine-level line starts appearing on every run.

**Blind spot — the validator runs `--silent`**, so sounds are never loaded and
no audio-path failure can show up in a corpus run. This hid the case-sensitivity
bug in `PackageAdapter` for a full sweep: Rayman (2719499501) scored WARN in the
validator while dying ~2 s in on the real desktop, on the first of 69 sound
references it could not resolve. When an item passes the validator but fails
live, re-run it windowed with `--volume 0` instead of `--silent` before assuming
the desktop path is at fault.

**Known false-positive**: shader-only FAILs are not trustworthy. glslang is run
on each dumped unit without the preamble the engine injects during translation,
so `M_PI`, `TEX8FORMAT` and `input` come back undeclared/reserved on wallpapers
that render perfectly — 61 of the 98 FAILs in the 2026-07-26 full run. Check
`frames` in `report.json` before believing a shader FAIL.

## Build and test

```bash
cmake --build build-new -j"$(nproc)"
./build-new/output/tests
```

The test executable is the authoritative local suite (Catch2, 19 case files in
`src/WallpaperEngine/Testing/Cases/`). This build currently does not register
those cases with CTest, so a successful `ctest` invocation that reports no
tests is not useful verification.

**Why `build-new` and not `build`.** A CMake build directory is not
relocatable — `CMakeCache.txt` stores absolute source and binary paths. The
original `build/` was configured while the repo still lived at
`~/repos/linux-wallpaperengine`, so after the move to `~/Projects/repos/` it
refused to build at all (*"The source directory … does not appear to contain
CMakeLists.txt"*). `build-new/` was configured fresh at the fork path on
2026-08-09 and `build/` was deleted 2026-08-19.

`build/` is still the **project's** convention: `README.md`,
`.github/workflows/cmake.yml`, and `validate-corpus.py --exe` all assume it.
Only this machine is on `build-new`, so pass `--exe build-new/output/…`
explicitly to the validator. Renaming `build-new` → `build` would realign with
the project and remove that friction, at the cost of one more re-configure.

Two settings the old `build/` had that `build-new/` lost, worth restoring if
full rebuilds feel slow — it currently has neither ccache nor lld:

```bash
cmake -B build-new -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
  -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld \
  -DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld .
```

## Runtime audio regression checks

These are manual live checks. Record the parent engine before and after each
test; helper/zygote processes contain `--type=` and are not the parent:

```bash
pgrep -af '^/home/admin/Projects/repos/linux-wallpaperengine/build[^/]*/output/linux-wallpaperengine --layer '
coredumpctl list linux-wallpaperengine
```

### Media metadata / track-change crash — TO BE TESTED

1. Select the **Gojo (Jujutsu Kaisen)** media-widget wallpaper and play music
   from a player that publishes title, album art, and playback state.
2. Confirm its widget shows the current track, then change next/previous track
   repeatedly, pause/resume, and allow album art to change.
3. Switch to a non-media wallpaper, change the track again, then switch back.
   This specifically exercises the formerly leaked listener after its owning
   ScriptEngine has been destroyed.
4. Pass criteria: the wallpaper engine parent remains alive, no new coredump
   appears, and the widget resumes updating after returning. A silent automatic
   process restart is a failure even if a wallpaper reappears.

If it fails, preserve the newest entry from:

```bash
coredumpctl list linux-wallpaperengine
coredumpctl gdb linux-wallpaperengine
```

Start with `thread apply all bt full` in GDB and note which track-change step
triggered the crash.

### AirPods/default-sink hot-swap — TO BE TESTED

1. Select a familiar audio-reactive wallpaper, start continuous music, and
   confirm the visualizer is moving before changing outputs.
2. Record the engine parent and current sink:

   ```bash
   pgrep -af '^/home/admin/Projects/repos/linux-wallpaperengine/build[^/]*/output/linux-wallpaperengine --layer '
   pactl get-default-sink
   pactl list source-outputs
   ```

3. Using the normal desktop audio selector, switch from AirPods to a local
   speaker/HDMI output, wait for playback and the visualizer to resume, then
   switch back to AirPods. Repeat three times while music continues.
4. Pass criteria: the same engine parent survives, playback remains audible,
   the capture source follows the active sink monitor, and visualizer motion
   returns within a few seconds after every switch. A crash, automatic parent
   replacement, capture left on the old monitor, or permanently flat response
   is a failure.

On failure, collect this immediately before restarting anything:

```bash
pactl info
pactl list sources
pactl list source-outputs
coredumpctl list linux-wallpaperengine
```

These checks validate runtime stability and capture routing. They do not prove
exact Wallpaper Engine 16/32/64-band magnitude or peak-placement parity.

## Extract wallpaper assets
Use the `repkg` CLI (`~/.local/bin/repkg`) rather than hand-rolling a parser:
`repkg extract <workshop-dir>/scene.pkg -o <output-dir>` unpacks textures,
models, materials, and shaders into a browsable tree (e.g.
`shaders/effects/depthparallax.frag`, `materials/effects/*.json`). It also
decodes `.tex` payloads to readable images during extraction.

Format notes if `repkg` is unavailable: `scene.pkg` is a simple offset table
— version string, count, then `{name, offset, len}` entries (strings are
u32-length-prefixed), payload after the header. TEX files: `TEXV0005`/
`TEXI0001` header (format, flags, texw/h, realw/h), `TEXB0003/4` container;
payload is raw BGRA either LZ4-block compressed (`cflag==1`, per-mip
`w,h,cflag,uncomp,comp`) or an embedded PNG. Fallback extraction script from
the July 2026 session: scratchpad `unpkg.py`.

## Verify puppet data independently
Parse the [[MDL File Format]] in Python/numpy, CPU-skin, rasterize with the
atlas texture — comparing that reference against the engine's output (via the
debug screenshot) localizes bugs to either the data model or the GL path.
The engine's skin matrices can be dumped with a temporary `sLog.out` in
`updatePuppetPositionBuffer`.

## Instance hygiene
Rebuilt binaries leave stale "(deleted)" engine instances stacking. Before
trusting visuals: `pgrep -af linux-wallpaperengine` + `ls -l /proc/<pid>/exe`
— exactly one instance, not "(deleted)". The waypaper wrapper SIGTERMs and
relaunches the engine on its own schedule; an unexpected exit-144/signal-15 in
a test run usually means the wrapper took over (and is then running the same
freshly built binary).

After every implementation requested by the user, terminate the current parent
engine and restore the saved Waypaper selection with the newest binary:

```bash
systemctl --user stop linux-wallpaperengine-latest.service
pkill -TERM -f '^/home/admin/Projects/repos/linux-wallpaperengine/build[^/]*/output/linux-wallpaperengine '
/home/admin/.local/bin/waypaper --restore
```

The service may not exist; that is harmless. Do not create a transient unit by
default, and do not reuse an old launch command containing stale wallpaper
paths. Confirm exactly one parent engine remains and that `/proc/<pid>/exe`
does not end in `(deleted)`.

## Setup failures = invisible layers
`Failed to setup object <id>` in the log means the object doesn't render at
all. Usually a shader translation failure → [[Shader Translation]].

## Reference-binary and Ghidra workflow

Use only the locally installed Wallpaper Engine binary/assets as a parity
reference. Start with asset extraction and `pass-log`, then strings and focused
xrefs/decompilation. Reusable headless Ghidra scripts are in
`.claude/tools/ghidra/`; exact commands, local paths, RenderDoc notes, and the
binary-hash workflow are kept in `.claude/lwe-terminal-runbook.md`.
