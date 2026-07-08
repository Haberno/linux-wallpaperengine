---
type: Rendering Concept
title: Parallax System
description: Camera parallax depth resolution and displacement behavior for Wallpaper Engine scene objects.
resource: file:///home/admin/repos/linux-wallpaperengine/src/WallpaperEngine/Render
tags: [linux-wallpaperengine, parallax, rendering, scene]
timestamp: 2026-07-06T20:00:00-04:00
---

# Parallax System

Camera parallax shifts layers with the mouse. Config comes from scene.json
`general`: `cameraparallax`, `cameraparallaxamount`, `cameraparallaxdelay`,
`cameraparallaxmouseinfluence`; per-object `parallaxDepth`.

## Depth resolution (hierarchical)

`CObject::resolveParallaxDepth()` — shared by CImage, CParticle, CText:

1. If the object authored `parallaxDepth`, use it.
2. Else inherit from the **nearest ancestor** that authored one.
3. Else default **1.0** (Wallpaper Engine's root default).

Authors pin layers with an explicit `0`; children pinned via a parent's `0`
(MyGO character). Unauthored root layers drift at depth 1 (Gojo backgrounds).

**`locktransforms` is only an editor-UI lock** — it never affects parallax or
rendering. An earlier exclusion based on it broke wallpapers that author every
layer locked (One Piece 3135984503 has locked layers with depths 1.1–1.35).

## Displacement convention

`CScene` smooths mouse displacement (`delay * 0.1` s time constant, mix per
frame) and exposes it; each renderer applies:

```
offset.x = -depth.x * amount * displacement.x * (sceneWidth * 0.5)
offset.y =  depth.y * amount * displacement.y * (sceneWidth * 0.5)
```

`PARALLAX_TRANSLATION_SPAN = 0.5` (fraction of scene width a unit-depth layer
travels over a full mouse swing) is a calibrated guess — recalibrate against a
reference wallpaper if strength feels off. Fullscreen layers are excluded
(they always cover the projection).

Shader-driven parallax (`depthparallax` etc.) instead reads
`g_ParallaxPosition` = `0.5 + displacement * amount`.

See [[Wallpaper Case Studies]] for the evidence trail.

## Divergence from upstream (Almamu/linux-wallpaperengine)

Compared against `upstream/main` (2026-07-07). Two of the three differences
are deliberate, evidenced fixes; the third is where remaining "feel" drift
most likely lives.

1. **Formula shape — multiplicative, not additive.** Upstream:
   `x = (depth.x + amount) * displacement.x * sceneWidth` — every layer
   drifts by at least `amount` even at `depth=0`, so a layer can never be
   truly pinned unless `amount` is also 0. This fork:
   `x = -depth.x * amount * displacement.x * (sceneWidth * 0.5)` — `depth=0`
   zeroes motion regardless of `amount`. The case-study wallpapers (One Piece
   3135984503, 2665939987) author `depth=0` as a hard pin, so the
   multiplicative model matches observed WE behavior. **Correct, keep.**
2. **Rotation order (PR #479).** Upstream bakes `rotModel` into `mvp` before
   translating by the parallax offset, so the offset rotates with the
   object's own authored rotation. This fork translates first, then
   multiplies by `rotModel`, keeping the parallax drift in scene space.
   Fixes spinning/rotating layers dragging their parallax offset around with
   them. **Correct, keep.**
3. **Response curve — unverified on both sides, likely source of residual
   "feel" mismatch:**
   - *Magnitude:* upstream centers the mouse as `pos - 0.5` (range ±0.5);
     this fork uses `pos*2 - 1` (range ±1.0) and halves the reference size via
     `PARALLAX_TRANSLATION_SPAN` to compensate. Net scale roughly cancels,
     but it's two constants tuned together by eye, not one formula derived
     from a spec — see the existing `PARALLAX_TRANSLATION_SPAN` note above.
   - *Smoothing:* this fork's `alpha = 1 - exp(-dt / (delay * 0.1))` is a
     single-pole exponential low-pass — framerate-independent, but with no
     ease-in: a mouse direction change starts moving the layer at full filter
     speed immediately, then decays into the new target. Upstream instead
     uses a linear-clamped mix (`delay * dt`, clamped to 1), also invented.
     Neither has been checked against WE's actual camera-lag curve, which
     more plausibly behaves like a damped spring (continuous acceleration,
     no instantaneous velocity kick). This is the most likely remaining
     cause of subtle "feel" differences even when pinning/direction/magnitude
     are all correct.

## Depth-map parallax occlusion effect (`depthparallax`)

Separate feature from the camera-parallax translation above. `effects/
depthparallax` (`shaders/effects/depthparallax.{vert,frag}`) is a per-pixel
steep-parallax/relief-mapping shader: it ray-marches a depth-map texture
(`g_Texture1`, `mode: depth`, r8) and offsets the *texture coordinates* of
the layer's own image, producing a real perspective/tilt illusion rather
than a whole-layer translation. This is what gives WE backgrounds with a
depth map their "tilting plane" look, as opposed to flat panning.

Case study: **3005674933**. The `bg` object authors `parallaxDepth: 0 0`
(hard-pinned — CObject's camera-parallax translation contributes nothing to
it) and carries a `depthparallax` effect layer (`textures: [null, "bg_depth"]`,
`center 0.3`, `scale 0.7 0.7`, `sens 1.0`, default `QUALITY` combo = 1 →
24-layer occlusion mapping). **100% of this wallpaper's visible tilt comes
from this shader**, not from per-object translation — so if the fork's
version looks like plain panning instead of a tilt, the effect is either not
warping correctly or is being fed too weak an input, not "translating
wrong."

Two things confirmed while tracing this:

1. **The shader shares the same input as per-object parallax.**
   `depthparallax.vert` computes `prlxInput = g_ParallaxPosition * 2 - 1`,
   and `g_ParallaxPosition` (`CScene.cpp`: `0.5 + m_parallaxDisplacement *
   amount`) is the exact same per-frame value that feeds `CImage`/`CText`/
   `CParticle` translation. This means the response-curve calibration
   question above (mouse-displacement magnitude, exponential-decay
   smoothing) doesn't just affect layer panning — it directly scales the
   strength of every `depthparallax`-style effect in the engine. Broadens
   the earlier finding rather than being a separate bug.
2. **`g_EffectTextureProjectionMatrix`/`...Inverse` were hardcoded identity —
   fixed.** Was `CPass.cpp:882-883`, `addUniform (..., glm::mat4 (1.0))`
   (same in upstream, pre-existing, not fork-introduced).
   `depthparallax.vert` uses the inverse to build a local
   `projectedDirX`/`projectedDirY` basis so the tilt direction follows the
   layer's own rotation; identity forced that basis to always be
   screen-axis-aligned. Harmless for axis-aligned/locked layers like
   3005674933's `bg`, but a latent correctness gap for any rotated layer
   using `depthparallax` or similar effects.

   Fix: `CImage::updateScreenSpacePosition()` now builds a rotation-only
   local→world matrix from `transform.angles` (same rotation chain as
   `rotModel`, minus the scene-center translate — translation doesn't affect
   a direction matrix) into new members `m_effectTextureProjectionMatrix` /
   `...Inverse` (inverse via `glm::transpose`, exact for a pure rotation).
   Wired to passes via `CPass::setEffectTextureProjectionMatrix()`, called
   from `CImage::setupPasses()` alongside the existing `setModelMatrix()`
   call. Identity when `angles == 0`, so every currently-verified wallpaper
   (including 3005674933, whose `bg` is unrotated) is byte-identical to
   before — this only changes behavior for rotated layers, which we don't
   have a case-study wallpaper for yet. Verified with a standalone glm-based
   assert check (scratchpad, not committed): zero-angle identity, matches
   `rotModel`'s rotation submatrix, inverse round-trips a world direction
   through local space correctly.

Not yet checked: whether the ray-march math itself
(`ParallaxMapping`/`g_Scale`/`g_Center` handling) round-trips correctly
through the HLSL→GLSL translation — the shader source itself is unmodified
stock WE GLSL, so if there's a magnitude bug it's upstream of the shader, in
the `g_ParallaxPosition` value fed into it (point 1 above).
