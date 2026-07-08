---
type: Issue Register
title: Known Issues
description: Open bugs, deferred rendering features, and house rules for the linux-wallpaperengine fork.
resource: file:///home/admin/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, bugs, deferred-work, rendering]
timestamp: 2026-07-06T20:00:00-04:00
---

# Known Issues

## Open bugs
- **Media-update segfault** (pre-existing): a D-Bus album-art/track change
  fires `ScriptEngine::notifyMediaUpdate` → crash in the QuickJS
  `ObjectAdapter::instantiate` / `VectorAdapter<3>::instantiate` path.
  Crashes any wallpaper with media widgets (Gojo 3100265648, coredump
  captured 2026-07-06). Rendering-independent. *Possibly fixed 2026-07-08*:
  ported beingsuz's `af82084` — `~ScriptEngine` leaked the album-art listener
  (captures `this`), so any track change after a wallpaper swap called through
  a dangling pointer. Pending verification.
- **MyGO clock text invisible** (3558034522): `CText`'s 2D path ignores the
  `parent` chain — the "Date" (352) and "Clock" (360) text objects are
  children of 341 with relative origins (0,-83)/(0,-128) and render
  off-screen bottom-left. Additionally CText only ticks the *text* script,
  never the *origin* script (341 positions itself via an
  `engine.canvasSize`-based script). Fix: route the 2D path through
  `CObject::resolveWorldMatrix` and register origin scripts. Parent
  visibility should cascade to children at the same time (children have no
  own `visible`; parent's is `{user: clock}`).
- **MyGO audio bars invisible** (3558034522): object 415 "Audio bar"
  (composelayer + `Simple_Audio_Bars` effect, combos ANTIALIAS/CLIP_HIGH/
  RESOLUTION=64, `g_AudioSpectrum64*` uniforms — CPass binds these). Root
  cause not yet confirmed; needs a log run to check for shader-translation
  or setup failure on the effect.
- **MyGO eyelid skin missing on blink** (3558034522): the eyelid "skin" is
  driven by the clipping-mask system (second vertex array + per-bone index
  ranges + `masks/clipping_mask_*` refs between MDLV and MDLS) that we don't
  consume yet. Eyelashes close but the skin behind them doesn't appear.
  Data is decoded and understood — see [[MDL File Format]] — implementation
  deferred.

## Deferred / not implemented
- Puppet bone constraint JSON (`"tp"`/`"tm"`) — mouse-interactive puppets.
- Animation layer `blend` weights (layers currently apply at full strength).
- Parallax response curve (magnitude constant `PARALLAX_TRANSLATION_SPAN =
  0.5` and the exponential-decay smoothing shape) is uncalibrated against
  real WE — both are tuned-by-eye heuristics, not derived from a spec. The
  smoothing has no ease-in (snaps to full filter speed on direction change),
  unlike WE's likely damped-spring feel. Needs a reference wallpaper
  recording to calibrate against → [[Parallax System]].
- `parseMdlvHeader` strict path never matches real files (docs' original
  layout was wrong); every load takes the scan fallback and logs an error —
  cosmetic, could drop the strict path or fix its offsets.
- Puppet effect chain: `clampuvs`, `copybackground`, and the `solid` flag are
  unhandled on puppet objects.

## House rules (from memory)
- No grim/PNG-diff verification of live output — ask the user; engine
  `--screenshot` on isolated windows is OK for debugging.
- Data-driven rendering: no magic numbers; derive from scene.json/assets.
- Watch for stale engine instances after rebuilds → [[Debugging Workflow]].
