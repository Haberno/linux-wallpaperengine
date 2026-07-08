---
type: Playbook
title: Debugging Workflow
description: Steps for isolating linux-wallpaperengine rendering bugs and validating object-level output.
resource: file:///home/admin/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, debugging, rendering, workflow]
timestamp: 2026-07-06T20:00:00-04:00
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

Note: grim/PNG-diff of the live wallpaper is banned for verification (ask the
user to look instead); the engine's own `--screenshot` on an isolated window
is fine for *debugging*.

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

## Setup failures = invisible layers
`Failed to setup object <id>` in the log means the object doesn't render at
all. Usually a shader translation failure → [[Shader Translation]].
