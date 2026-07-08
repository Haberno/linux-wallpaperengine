---
type: Rendering Concept
title: 3D Scene Support
description: How perspective (3D) scene wallpapers are detected, parsed, and rendered, plus the SceneScript property pipeline that drives them.
resource: file:///home/admin/repos/linux-wallpaperengine/src/WallpaperEngine/Render/Objects/CModel.cpp
tags: [linux-wallpaperengine, 3d, perspective, mdl, lights, scenescript]
timestamp: 2026-07-08T05:30:00-04:00
---

# 3D Scene Support

Test/reference wallpaper: workshop `3589454154` (Saturn space scene: 24 MDLV
models, 2 script-driven lights, orbit containers, HDR bloom). Extracted copy
lives wherever `repkg extract` was last run; scene facts below come from its
`scene.json`.

## Scene detection and camera

- A scene is 3D when `general.orthogonalprojection` is `null`/absent
  (`WallpaperParser::parseScene`). 3D scenes author `fov`, `nearz`, `farz`,
  `zoom` in `general` (not `camera`).
- **The top-level `camera.eye/center/up` block is only the editor's last
  viewport, NOT the runtime camera.** The runtime camera is a dedicated
  *camera object* in `objects[]` (key `"camera": "default"`): its `origin` is
  the eye, its `angles` (radians, Rz·Ry·Rx like other objects) orient a
  camera that looks down **−Z** with **+Y up** by default, and its own
  `fov`/`zoom` override the `general` values. It may carry
  `"path": "scripts/camera_paths_<id>.json"` (`{"paths": [...]}`; empty in
  3589454154) plus `queuemode` for camera path animation — unimplemented, the
  resting pose is used. Only when no camera object exists does WE fall back
  to the editor viewport ("last position used in the editor", per the
  official docs).
  - Verified three ways on 3589454154: the 4K UI plane (container 395, ±1.92
    × ±1.08 at z=0) projects to exactly NDC ±1 from the camera object's pose
    (0,0,2.3) and is 35° off-frustum from the editor eye; the workshop
    `preview.gif` shows the centered composition; the docs describe "Edit
    Camera POV" placing a Camera asset.
- `Camera::setPerspectiveProjection` uses `glm::perspective` with `fov` as the
  **vertical** field of view and the constructor's lookAt as the view matrix.
  Render size = output resolution (no authored projection size exists).
- **Present-flip compensation**: Wayland/GLFW outputs present the scene FBO
  with a vertical flip (`Output::renderVFlip`, X11 does not); the 2D pipeline
  bakes the compensation into object coordinates. 3D scenes bake it into the
  projection instead: `m_projection[1][1] *= -1` when the output flips
  (`Camera::isYFlipped`). Consequences: model front-face winding mirrors once
  more (CW → CCW when flipped, `CModel`), screenshots stay correct because
  `takeScreenshot` already orients rows by the same `renderVFlip` flag, and
  the world renders upright on every backend. Forgetting this made the whole
  3D scene (planet, icons, text) appear upside down on Hyprland.
- `zoom != 1` is not implemented (logged and ignored). The exe also knows a
  `perspectiveoverridefov` property.
- Real WE renders with **reversed depth** (`REVERSEDEPTH` combo exists and
  `common_pbr_2.h` branches on it); we use conventional depth. Only matters
  once shadow mapping is implemented.

## Object graph

- `model` objects → [[MDL File Format]] MDLV static meshes via `MdlParser`
  (tag-15 layout only: pos/normal/tangent4/uv, stride 48). Rendered by
  `CModel` through the normal material/`CPass` pipeline, one VBO/IBO per
  submesh, `glFrontFace(GL_CW)` because WE is a D3D engine.
- `light` objects → `CLight` (`lpoint`, `ldirectional`; spot/tube parse as
  point with an error). Directional base forward is **+X** (verified against
  the sun-tracking script in 3589454154).
- Containers/groups have no renderer but register as `ScriptableObject` so
  origin/scale/angles scripts still run (orbital-mechanics groups). The
  camera object also lands in this inert-container path; its pose is consumed
  at parse time by `WallpaperParser`.
- Screen-glued UI in 3D scenes (docks, clocks, text plates) is authored as a
  world-space canvas: a container scaled `0.001` holding children positioned
  in virtual-4K pixels, sized so it exactly fills the camera-object frustum
  at z=0. Nothing in the engine is special about it — if the camera is right,
  the UI lands right.
- `CText` in 3D mirrors its quad with `scale(1,−1,1)` so the glyph top sits at
  world +y (the shared VBO was authored for the 2D screen-space convention),
  and disables face culling for the flipped winding. With the projection-level
  flip above, this is correct on both flipped and unflipped backends. `CImage`
  3D quads (v=0 paired with the world-top edge) need no extra handling for the
  same reason.
- `CObject::resolveWorldMatrix` composes translate·Rz·Ry·Rx·scale through the
  parent chain (radians). CImage/CText in 3D scenes use it with the real
  perspective MVP instead of the 2D screen-space path.
- The scene FBO gets a depth renderbuffer (`CFBO` `withDepthBuffer`), and
  `renderFrame` re-enables `glDepthMask` before the clear because passes with
  `depthwrite disabled` leave the mask off.

## Lighting

`ShaderUnit::generateLightingV1` emits `PerformLighting_V1` with one unrolled
call per light, matching **verbatim** the generated templates found inside
`wallpaper64.exe` (see [[WE Reference Mining]]):

- point: `ComputePBRLightShadow(normal, lightDelta, viewVector, color,
  g_LPoint_Color[i].rgb, g_LPoint_Color[i].w /*radius*/,
  g_LPoint_Origin[i].w /*exponent*/, specularTint, baseReflectance,
  roughness, metallic, shadowFactor)`
- directional: `ComputePBRLightShadowInfinite(normal,
  g_LDirectional_Direction[i].xyz /*towards light*/, ...)`

Light counts are fixed at scene load (`CScene` counts light objects before
object creation) so `CPass` can pass `LIGHTS_DIRECTIONAL`/`LIGHTS_POINT`
combos and bind uniform arrays whose storage never reallocates.
`CScene::updateLightState` refreshes directions/colors every frame after the
script tick; invisible lights contribute black. `shadowFactor` is pinned to
1.0 until shadow mapping exists.

`SCENE_ORTHO` is defined on every scene pass (1 ortho / 0 perspective):
`genericimage3/4.frag` use it to pick the fixed `(0,0,1)` view vector on 2D
scenes vs. `normalize(v_ViewDir)` in 3D — same combo real WE sets globally.

## SceneScript property pipeline

Property scripts (`origin`/`scale`/`angles`/`visible` with a `script` key)
are stripped of ES-module syntax and evaluated as an IIFE returning the
lifecycle hooks (`init`/`update`/...), each with its own captured `thisLayer`
and seeded `scriptProperties` (`ScriptEngine::queueScript`).

Semantics that MUST hold (bug fixed 2026-07-08, previously nulled values and
froze the sun/orbits in 3589454154):

- A hook returning `undefined`/`null` means **keep the current value**. WE
  passes plain JS `Vec3` instances (see baseclasses.js note in
  [[WE Reference Mining]]) and reads back the *return value*; scripts that
  only mutate the argument still work for us because our vector adapters
  write through live. `jsToDynamicValue` must never null a DynamicValue on an
  undefined result — the missing-`init` case returns undefined through
  `ScriptEngine::call`.
- `angles` cross the boundary in **degrees** (scene data is radians) as plain
  `{x,y,z}` objects (`anglesToJs`/`jsToAngles`).

Engine/script API state (`EngineObject`, `InputObject`):

- `engine.registerAudioBuffers(res)` returns `{left,right,average}`
  Float32Arrays that are zero-copy views over the recorder's live spectrum
  (mono: all three share storage). 16/32/64 bands.
- `engine.setTimeout/setInterval` work as of 2026-07-08 (previously: instance
  registry never populated + wrong `JS_CFUNC_generic` proto + missing
  callback dup → they always threw).
- `engine.screenResolution` (output pixels), `engine.canvasSize` (scene
  size). `input.cursorScreenPosition` is in **pixels** per the official
  d.ts — scripts divide by `screenResolution` themselves.
- `shared` global works (astronomy script populates ~323 keys in the test
  scene).

## Known gaps (priority order for the test wallpaper)

1. **HDR bloom** (`bloom: true` + `bloomhdr*` keys): sun glow missing. The
   full material chain is documented in [[WE Reference Mining]]; needs a
   downsample/blur/upsample FBO pyramid orchestrated in `CScene`. Currently
   logged and skipped.
2. **Shadow mapping** (`castshadow: true` on both lights): Saturn↔rings
   shadows missing. Uniforms/templates known (`g_LFeature_Shadow*`), needs a
   shadow atlas pass + `REVERSEDEPTH` decision.
3. **`thisScene.getLayer(name)`** and the wider ILayer surface: the Sykm dock
   overlay scripts want it (`visible_425`), plus `input.cursorWorldPosition`
   is a zero stub.
4. **Spot/tube lights**: parse as point; templates for both are in the exe
   (cookie sampling via `g_LSpot_*`, segment lights via `g_LTube_OriginA/B`).
5. **`transparentsorting`**: we render in authored order; WE sorts
   transparents (`customsortorder` exists too). Matters for orbiting
   translucent objects crossing each other.
6. **`camerafade`**: WE fades the scene in via `materials/util/fade.json`;
   we pop in.
7. **Skinned models in 3D scenes** (`g_Bones` path, MDLS/MDLA): only static
   tag-15 MDLV is parsed; puppet skinning lives in the 2D CImage path (see
   [[Puppet Warp Pipeline]]).
8. **Fog** (`FOG_DIST`/`FOG_HEIGHT`, `g_Fog*` uniforms): unused by the test
   scene, unimplemented.
