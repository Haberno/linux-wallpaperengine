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
When individual camera fields are omitted, Wallpaper Engine defaults amount to
`0.5`, delay to `0.1`, and mouse influence to `0.5`.

## Depth resolution (hierarchical)

`CObject::resolveParallaxDepth()` — shared by CImage, CParticle, CText —
walks to the **root-most layer** and uses that layer's depth for the complete
subtree. Child-authored depths do not override it. The official Layer
constructor defaults the depth to **1.0**; authors pin a subtree with an
explicit root depth of `0` (MyGO character, 3367988661 clock/date container).
Unauthored root layers therefore drift at depth 1 (Gojo backgrounds and
3487328036's puppet/window layer).

**`locktransforms` is only an editor-UI lock** — it never affects parallax or
rendering. An earlier exclusion based on it broke wallpapers that author every
layer locked (One Piece 3135984503 has locked layers with depths 1.1–1.35).

## Displacement convention

`CScene` smooths mouse displacement using Wallpaper Engine's frame update:

```
alpha = delay <= 0 ? 1 : clamp((1 - delay / 3) * 10 * dt, 0, 1)
smoothed = mix(smoothed, target, alpha)
```

Each renderer then applies:

```
offset.x = -depth.x * amount * displacement.x * (sceneWidth * 0.5)
offset.y =  depth.y * amount * displacement.y * (sceneHeight * 0.5)
```

`PARALLAX_TRANSLATION_SPAN = 0.5` converts this fork's centered `[-1,1]`
displacement into Wallpaper Engine's half-canvas cursor delta. It is therefore
part of the coordinate conversion, not a tuning constant. Fullscreen layers
are excluded (they always cover the projection).

Shader-driven parallax (`depthparallax` etc.) instead reads the influenced,
smoothed mouse position remapped to `[0,1]`:
`g_ParallaxPosition = 0.5 + displacement * 0.5`. Camera `amount` does not
scale this shader input.

See [[Wallpaper Case Studies]] for the evidence trail.

## Divergence from upstream (Almamu/linux-wallpaperengine)

Compared against `upstream/main` (2026-07-07). The camera formula and response
were subsequently recovered from `wallpaper64.exe` (2026-06-29 build).

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
3. **Response curve — binary-confirmed.** Wallpaper Engine computes
   `alpha = clamp((1 - delay / 3) * 10 * dt, 0, 1)` and linearly mixes the
   influenced cursor toward its target. This replaces the fork's earlier
   exponential approximation. Its whole-layer transform uses a half-canvas
   cursor delta, exactly equivalent to this fork's `[-1,1]` displacement
   multiplied by `PARALLAX_TRANSLATION_SPAN = 0.5`.

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

1. **The shader shares the smoothed cursor, but not camera `amount`.**
   `depthparallax.vert` computes `prlxInput = g_ParallaxPosition * 2 - 1`,
   and `g_ParallaxPosition` is `0.5 + m_parallaxDisplacement * 0.5`.
   Wallpaper Engine applies camera `amount` later, only in whole-layer
   translation. Keeping it out of this uniform is essential for depth-map-only
   scenes such as 3751020128, which intentionally authors `amount = 0`.
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
stock WE GLSL. The input semantics feeding that shader are now
binary-confirmed.
