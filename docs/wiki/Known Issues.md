---
type: Issue Register
title: Known Issues
description: Open bugs, deferred rendering features, and house rules for the linux-wallpaperengine fork.
resource: file:///home/admin/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, bugs, deferred-work, rendering]
timestamp: 2026-07-08T13:30:00-04:00
---

# Known Issues

For the live consolidated dashboard (open issues + parity gaps + verification
queue), see [[Current Status]]. This page keeps the durable issue records.

## Open bugs
- **Media-update segfault** (pre-existing): a D-Bus album-art/track change
  fires `ScriptEngine::notifyMediaUpdate` → crash in the QuickJS
  `ObjectAdapter::instantiate` / `VectorAdapter<3>::instantiate` path.
  Crashes any wallpaper with media widgets (Gojo 3100265648, coredump
  captured 2026-07-06). Rendering-independent. *Possibly fixed 2026-07-08*:
  ported beingsuz's `af82084` — `~ScriptEngine` leaked the album-art listener
  (captures `this`), so any track change after a wallpaper swap called through
  a dangling pointer. Needs days of real track-change use to confirm.
- **MyGO eyelid skin missing on blink** (3558034522): the eyelid "skin" is
  driven by the clipping-mask system (second vertex array + per-bone index
  ranges + `masks/clipping_mask_*` refs between MDLV and MDLS) that we don't
  consume yet. Eyelashes close but the skin behind them doesn't appear.
  Data is decoded and understood — see [[MDL File Format]] — implementation
  deferred.

## Fixed 2026-07-08 (verified unless noted)
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
- **Visualizer bars saturated** (flicker-only response): band reduction was
  `0.35·log10(mag²)` clamped to 1 — real music pegged nearly every bar.
  Replaced with linear magnitudes + slow-decay auto-gain + sqrt shaping +
  noise gate (5b548f4). `WPE_AUDIO_GATE` / `WPE_AUDIO_DEBUG=1`
  (→ `/tmp/we-audio-debug.log`). *Pending final user confirmation of
  bar dynamics.*

## Deferred / not implemented
- Puppet bone constraint JSON (`"tp"`/`"tm"`) — mouse-interactive puppets.
- Animation layer `blend` weights (layers currently apply at full strength).
- Puppet effect chain: `clampuvs`, `copybackground`, and the `solid` flag are
  unhandled on puppet objects.

## House rules (from memory)
- No grim/PNG-diff verification of live output — ask the user; engine
  `--screenshot` on isolated windows is OK for debugging.
- Claude never launches the engine (or any window-creating process) itself —
  give the user the command instead.
- Data-driven rendering: no magic numbers; derive from scene.json/assets or
  the mined WE reference ([[WE Reference Mining]]); DSP constants get a
  `ponytail:` tuning note.
- Watch for stale engine instances after rebuilds → [[Debugging Workflow]].
