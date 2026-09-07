---
type: Rendering Concept
title: Puppet Warp Pipeline
description: How Wallpaper Engine puppet meshes are parsed, animated, skinned, and rendered in the fork.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine/src/WallpaperEngine/Render/Objects/CImage.cpp
tags: [linux-wallpaperengine, puppet-warp, mdl, rendering]
timestamp: 2026-08-19T00:00:00-04:00
---

# Puppet Warp Pipeline

Puppet warp is Wallpaper Engine's 2D skeletal animation. A puppet image's
texture may be a **disassembled parts atlas** (sword here, head there), while
MDLV vertices already describe the assembled model-space rest pose. MDLS and
MDLA deform that mesh; MDAT attaches separate child layers to its bones.

Implementation lives in `src/WallpaperEngine/Render/Objects/CImage.cpp`.

## Flow

1. `loadPuppetMesh` — validates the supported [[MDL File Format]] vertex
   masks/strides, then loads positions, blend indices/weights, UVs, and
   16-bit indices. MDLS/MDAT/MDLA are parsed by the shared
   `MdlAnimationParser` used by both CImage and CModel.
2. `selectPuppetAnimations` — matches the scene object's `animationlayers[]`
   entries by animation id and continuously evaluates live visibility, rate,
   blend, additive, animation selection, and blend-in/out state.
3. `updatePuppetPositionBuffer` — CPU-skins every frame:
   - per bone: interpolate T3/R3/S3 keyframes using quaternion rotation across
     all three Euler components, compose weighted/additive layers, walk the parent hierarchy,
     `skin = animWorld * inverseBindWorld`
   - per vertex: blend up to 4 bone influences, then map the posed
     model-space position onto the object's scene quad (`m_pos`).
4. `setupPuppetGeometryCallback` — binds the puppet VBOs and draws indexed
   triangles in place of the standard quad.
5. `resolveTransform` — for a child with `scene.json.attachment`, composes
   its parent's live `boneWorld * attachmentLocal` transform before the
   child's own origin/scale/angles. Attachment chains may be nested.

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
- **Additive clips resolve against one shared model reference pose.**
  Source checked 2026-09-06: `referenceBone` in `MdlAnimation.cpp` takes the
  first available embedded sample for each bone, falling back to `bindLocal`.
  This preserves the constraint-resolved pose of older MDLS0002 puppets.
  Every layer uses that same reference; rebasing each clip to its own first
  frame would cancel later one-shot entrance motion. The bind-only rule from
  `ba29823e` was superseded by `1e23f676`. Regression cases in
  `Testing/Cases/MdlAnimation.cpp` cover entrance clips, constrained puppets,
  and component-wise additive composition.
- **One reference pose is shared across layers** (`1e23f676`, 2026-08-01) —
  each layer rebasing its own copy desynchronizes stacked clips.
- **Additive bones compose per component** (`6040e79e`, 2026-08-01) —
  translation, rotation, and scale accumulate separately; composing whole
  matrices couples them and skews the result.
- **Frame rate:** the header float is **fps**, not duration
  (`frame = time * fps * rate`).
- **Attachments are live bone transforms.** Applying only ordinary scene
  parenting leaves attached child layers at their unbound offsets; adding a
  static per-wallpaper correction also fails as soon as the bone animates.
- **Blend is live.** Each layer's authored blend multiplies its visibility
  transition weight; later additive layers blend from identity and later
  non-additive layers blend toward their sampled absolute pose.

## Remaining parity gaps
- Bone constraint JSON in MDLS names (`"tp"`/`"tm"` translation limits) —
  likely powers mouse-interactive puppets.
- Nested/multi-mask clipping composition and nonzero descriptor flags. The
  auxiliary position array, range table, descriptors, and ordinary single-mask
  path are implemented and user-verified on MyGO — see [[Known Issues]].
