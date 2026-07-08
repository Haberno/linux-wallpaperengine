---
type: Rendering Concept
title: Puppet Warp Pipeline
description: How Wallpaper Engine puppet meshes are parsed, animated, skinned, and rendered in the fork.
resource: file:///home/admin/repos/linux-wallpaperengine/src/WallpaperEngine/Render/Objects/CImage.cpp
tags: [linux-wallpaperengine, puppet-warp, mdl, rendering]
timestamp: 2026-07-06T20:00:00-04:00
---

# Puppet Warp Pipeline

Puppet warp is Wallpaper Engine's 2D skeletal animation. A puppet image's
texture is a **disassembled parts atlas** (sword here, head there); the mesh's
rest pose mirrors that atlas exactly (`u = 0.5 + x/size`, verified residual
~1e-8). The character only exists once the skeleton poses it.

Implementation lives in `src/WallpaperEngine/Render/Objects/CImage.cpp`.

## Flow

1. `loadPuppetMesh` — parses the [[MDL File Format]]: vertices (positions,
   blend indices/weights at offsets 40/56, UVs at 72), indices, then
   `loadPuppetBones` (MDLS) and `loadPuppetAnimations` (MDLA).
2. `selectPuppetAnimation` — matches the scene object's `animationlayers[]`
   entries (by `animation` id) against the file's animations; all visible
   layers become active, each with its own `rate`.
3. `updatePuppetPositionBuffer` — CPU-skins every frame:
   - per bone: interpolate keyframes (T3/R3/S3, z-rotation only),
     compose layers, walk the parent hierarchy,
     `skin = animWorld * inverseBindWorld`
   - per vertex: blend up to 4 bone influences, then map the posed
     model-space position onto the object's scene quad (`m_pos`).
4. `setupPuppetGeometryCallback` — binds the puppet VBOs and draws indexed
   triangles in place of the standard quad.

## Invariants (each fixed a real bug — see [[Wallpaper Case Studies]])

- **Warp only at the final on-screen pass.** All effect passes (waterwaves,
  shine, pulse, foliagesway…) run on the plain quad in image space so their
  masks stay aligned with the texture.
- **Dedicated composite pass when effects exist.** A passthrough pass
  (`materials/util/effectpassthrough.json`) is appended and warped instead of
  hijacking the last effect pass — effect shaders manipulate `a_Position` in
  image space and would mangle scene-space puppet geometry.
- **Flatten vertex z to 0.** Puppet z encodes part layering (±700 in Gojo),
  not scene depth; the ortho near/far planes would clip it. Draw order comes
  from the index buffer; depth testing is off.
- **Disable `GL_CULL_FACE` for the puppet draw.** The Y-flip into scene space
  inverts triangle winding; materials with `cullmode: normal` would cull the
  whole mesh.
- **Additive layers are deltas from their own frame 0**, not from the bind
  pose. Every animation stores absolute poses including the atlas→body
  assembly; composing against bind applies the assembly once per layer.
- **Frame rate:** the header float is **fps**, not duration
  (`frame = time * fps * rate`).

## Not yet implemented
- Bone constraint JSON in MDLS names (`"tp"`/`"tm"` translation limits) —
  likely powers mouse-interactive puppets.
- Per-layer `blend` weight (currently layers apply fully).
- The second per-vertex position array + per-bone index ranges + clipping-mask
  references found between the mesh and MDLS in some files (eye/eyelid
  clipping in MyGO) — see [[Known Issues]].
