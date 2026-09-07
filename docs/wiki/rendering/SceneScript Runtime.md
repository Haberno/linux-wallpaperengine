---
type: Concept
title: SceneScript Runtime
description: How Wallpaper Engine property scripts are lowered and executed on QuickJS, and the traps that make them fail silently.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine/src/WallpaperEngine/Scripting
tags: [linux-wallpaperengine, scripting, quickjs, scenescript]
timestamp: 2026-08-19T00:00:00-04:00
---

# SceneScript Runtime

Wallpaper Engine property scripts (`object.<property>.script` in `scene.json`)
run on QuickJS in `WallpaperEngine::Scripting`. They are authored as ES modules
but are **not** executed as modules here.

## Module lowering

`normalizeSceneScriptModuleSyntax` (`ScriptEngine.cpp`) rewrites each script
before evaluation: it strips `'use strict';`, strips the `export ` keyword,
**drops `import` lines entirely**, and prepends `const WEMath = globalThis.WEMath;`
/ `const WEVector = globalThis.WEVector;`. The result is wrapped in a classic
IIFE with an 11-line prelude.

Two consequences:

- **The C++ ES modules are never instantiated for property scripts.** Because
  imports are dropped, `MathModule` / `VectorModule` never load; scripts see the
  JS shims in `Scripting/resources/builtins.js` instead. **The shims and the C++
  modules must agree.** They did not for `WEMath.deg2rad`/`rad2deg`: the shim
  defined them as *functions* while `MathModule` defines them as *numbers*, so
  `vec.multiply(WEMath.deg2rad)` multiplied by a function object and threw
  `Unsupported type conversion for VectorAdapter`.
- **Stack-trace line numbers are offset.** To map a reported line back to the
  extracted source: `wrapped = 11 + (prefix lines) + (index into the
  import-stripped body)`. Reverse it before reading the script or you will
  diagnose the wrong statement.

## Vector method argument order

`VectorAdapter`'s binary operators take `this` from the JS receiver and the
operand from `argv[0]`. All of them were originally written as
`operand OP this`. `add`, `multiply`, `dot`, `min` and `max` are commutative so
nothing showed, but `subtract`, `divide`, `cross` and `mix` were inverted:
`a.subtract(b)` returned `b - a`, `a.divide(b)` returned `b / a`, `a.cross(b)`
returned the negated normal, and `a.mix(b, t)` interpolated at `1 - t`.

This is worth knowing because the failure is *silent and oscillatory*, not a
throw. Stock controllers round-trip state through `shared` with a symmetric
offset (`shared.x = v.add(k)` on one line, `v = shared.x.subtract(k)` on the
next). With `subtract` reversed that round-trip negates instead of restoring, so
the value flips sign every frame — a period-2 oscillation that reads as the
wallpaper violently shaking. It is also why normalization
(`v.divide(v.length())`) produced componentwise reciprocals and `inf` poses.

## Lifecycle traps

- **A throw in `update()` is permanent.** `ScriptEngine::tick` sets
  `updateEnabled = false` on the first exception and never retries, so one bad
  frame silently kills that property for the life of the scene. A script that
  logs a single error at startup and then goes quiet is *disabled*, not
  recovered.
- **All `init()`s run before any first `update()`.** `shared` is one object on
  `globalThis` per scene; scripts publish to it from `init()` and consume it from
  `update()`. `initializeQueuedScripts` therefore runs the hooks in two separate
  passes. Interleaving them made an early layer's first `update()` throw on state
  a later layer had not created yet, which (per the rule above) disabled it
  forever.
- **Every script needs the full user-property object at startup.** Wallpaper
  Engine calls `applyUserProperties` once with *all* project properties before
  the first frame, and scripts routinely gate their entire behavior on it.
  Dispatching only on later live changes leaves them running their inert
  default branch forever — which is how 2244339517 ended up with a camera that
  never rotated, with no error anywhere. `ScriptEngine::dispatchAllUserProperties`
  now does the startup pass (`f826719a`, 2026-08-18); the per-key
  `dispatchUserProperty` remains for live edits.

  **The missing `thisObject` binding is fixed (`181c752e`, 2026-09-07).**
  The original 2026-08-19 corpus recorded 11 affected items out of 363. The
  prelude now aliases `thisObject` to `thisLayer`; a live property-script
  fixture verified object identity and the owning layer's name during updates.
  Keep startup dispatch. Other property-hook/animation-API errors remain.
  → [[Known Issues]], [project checklist](../../Organized%20all.md).
- **`engine.frametime` must never be zero.** The stock idiom for framerate
  normalization is `engine.frametime * (1 / engine.frametime)`, which is
  `0 * Infinity = NaN` on the first tick, before any frame has elapsed. That NaN
  gets integrated into script state and never washes out. `engine_get_frametime`
  clamps to `0.0001f`.

## Debugging

`WPE_SCRIPT_TRACE` traces script execution at runtime (added 2026-08-18,
`878a4ca4`) — reach for it before hand-instrumenting a script, especially when
the symptom is "nothing happens" rather than a thrown error, since both of the
silent-failure modes above (a disabled `update()`, an ungated default branch)
look identical from the outside.

Extract the real script with `repkg extract <workshop-dir>/scene.pkg -o <dir>`
and read it out of `scene.json` — the property scripts are string fields on the
objects, so `python3 -c` over the JSON beats grepping the packed file. Map the
reported line number back through the lowering offset above before drawing
conclusions.

See [[3D Scene Support]] for the camera-ownership half of drag controllers, and
[[Debugging Workflow]] for extraction and fresh-process checks.
