---
type: Rendering Concept
title: 3D Scene Support
description: How perspective (3D) scene wallpapers are detected, parsed, and rendered, plus the SceneScript property pipeline that drives them.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine/src/WallpaperEngine/Render/Objects/CModel.cpp
tags: [linux-wallpaperengine, 3d, perspective, mdl, lights, scenescript]
timestamp: 2026-07-17T00:00:00-04:00
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
  3589454154) plus `queuemode` for camera path animation. Path playback supports
  the current curve-channel format and legacy timestamped transforms; visible
  camera objects select their own queue at runtime. Only when no camera object
  exists does WE fall back
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
- Camera-path `fov` and `zoom` channels rebuild the projection as the path
  advances; resting `zoom` is honored too. The exe also knows a
  `perspectiveoverridefov` property, which remains unsupported.
- Real WE exposes a **reversed depth** branch (stock `common_pbr_2.h` uses
  `REVERSEDEPTH` in shadow bounds checks); we render and build the shadow
  atlas with conventional GL depth and never define `REVERSEDEPTH`, so the
  stock shaders take the =0 branch. This is now an exact-parity/bias concern
  rather than a missing-shadow blocker.

## Object graph

- `model` objects → [[MDL File Format]] MDLV meshes via `MdlParser`: static
  tag-15 (pos/normal/tangent4/uv, stride 48) and skinned `0x0180000f`
  (adds blend indices/weights, stride 80). Bit 0 of the submesh flags selects
  32-bit indices; the independent BoostModel `0x400` flag is accepted without
  changing index width. Unknown layouts/flags remain hard failures.
- `CModel` renders one VBO/IBO per submesh through the normal material/`CPass`
  pipeline. `MdlAnimationParser` and `MdlAnimationEvaluator` are shared with
  CImage; MDLS/MDLA clips are evaluated every frame and packed into the
  shader's `g_Bones` mat4x3 array. Skinned geometry also participates in the
  shadow caster pass.
- MDAT named attachments resolve against a parent CImage or CModel's current
  animated bone pose inside `CObject::resolveWorldMatrix`, so nested child
  layers and lights follow animated 3D models (including Sonic Rooftop's
  `Stage`).
- `light` objects → `CLight` (`lpoint`, `lspot`, `ltube`, and
  `ldirectional`). Directional and spot base forward is **+X** (verified
  against the sun-tracking script in 3589454154 and native spot packing).
- Containers/groups have no renderer but register as `ScriptableObject` so
  origin/scale/angles scripts still run (orbital-mechanics groups). Camera
  objects also use this inert-container path. `CScene` selects the last visible
  authored camera layer and resolves its live world matrix after SceneScript,
  so parented chase-camera rigs move and rotate the rendered view.
- **Mouse-drag orbiting is a script feature, not an engine feature.** Stock 3D
  scenes ship a controller layer (a `models/util/fullscreenlayer.json` quad whose
  `visible` property carries the script) that reads `input.cursorLeftDown` +
  `input.cursorScreenPosition`, integrates them into orbit angles/distance, and
  pushes the result through `thisScene.setCameraTransforms({eye, center, up, fov})`.
  `shared.camera.*` is the script's own bookkeeping — the engine never reads it.
  `setCameraTransforms` was a no-op stub until 2026-07-26, which is why dragging
  did nothing in this fork while the real Wallpaper Engine orbited fine. It now
  applies *after* `updateCameraObject()` and camera-path evaluation in
  `CScene::renderFrame`, because a script that takes the camera owns it outright.
  3737268876 gates its controller on the `cameratype` user property
  (`0` = Manual, the default).
- **The script camera pose is sticky, not per-tick.** Once a controller calls
  `setCameraTransforms`, `CScene::m_scriptCameraTransform` holds that pose until
  the script pushes a new one, and `updateCameraObject()` stops re-applying the
  layer rig to the live camera (it still tracks it as the default). Both halves
  are needed. Controllers re-read `thisScene.getCameraTransforms()` at the top of
  every `update()` and integrate from it, so re-applying the rig mid-frame hands
  them a camera that resets between frames and they oscillate against their own
  smoothing. And the rig usually sits somewhere completely different from where a
  controller puts the camera — 3737268876's rig is at `eye 9.98 1.44 -0.05` while
  its controller orbits the origin — so falling back to it on any frame the script
  does not produce a usable pose snaps the view across the scene and back.
  `setCameraTransforms` drops non-finite poses for the same reason.
- **Cross-layer `shared` state requires all `init()`s before any `update()`.**
  `shared` is one object on `globalThis` per scene, and scripts publish to it from
  `init()` and consume it from `update()`. `ScriptEngine::initializeQueuedScripts`
  runs the two hooks in separate passes; interleaving them (the original
  behaviour) made an early layer's first `update()` throw on state a later layer
  had not created yet, and a throw there sets `updateEnabled = false`
  **permanently**, silently killing that script for the life of the scene.
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
- Material passes loaded for a 3D model default omitted `depthtest` and
  `depthwrite` fields to enabled. Image/effect/particle materials retain the
  overlay-style disabled defaults, and explicitly authored depth state always
  wins. Imported stage meshes frequently omit both keys while relying on model
  depth defaults; treating them as overlays lets later meshes paint through
  foreground characters (workshop `3708206626`).
- Perspective scenes with `general.transparentsorting: true` reorder 3D model
  draw slots every frame. Opaque models remain stable first, translucent models
  are drawn back-to-front by their transformed origin in camera space, and
  additive models are drawn last. Non-model slots remain fixed so image/effect,
  particle, light, camera, and dependency ordering is not disturbed. The sort is
  render-only, so SceneScript layer indices still use the authored order.
  `general.customsortorder: true` suppresses the automatic sort and preserves
  explicit authored order. Classification is currently at whole-model
  granularity; a model with mixed passes uses its strongest blended class.

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
- spot: the authored degree angles are packed as `cos(innercone)` in
  `g_LSpot_Origin.w` and `cos(outercone)` in `g_LSpot_Direction.w`; the stock
  `smoothstep` cookie multiplies the color before `ComputePBRLightShadow`.
- tube: `PointSegmentDelta` lights from the closest point on the segment;
  endpoint A is the light origin and endpoint B is the full world transform
  of the authored local `controlpoint`.

Light counts are fixed at scene load (`CScene` counts light objects before
object creation) so `CPass` can pass `LIGHTS_DIRECTIONAL`/`LIGHTS_POINT`/
`LIGHTS_SPOT`/`LIGHTS_TUBE` combos and bind uniform arrays whose storage never
reallocates. Color, intensity, radius, exponent, cone, and control-point
properties remain script-driven.
`CScene::updateLightState` refreshes directions/colors and light-space
projections every frame after the script tick; invisible lights contribute
black and disable their shadow feature.

`SCENE_ORTHO` is defined on every scene pass (1 ortho / 0 perspective):
`genericimage3/4.frag` use it to pick the fixed `(0,0,1)` view vector on 2D
scenes vs. `normalize(v_ViewDir)` in 3D — same combo real WE sets globally.

## Shadow mapping

Models with opaque material passes cast into one 2048×2048 comparison-depth
atlas (`CScene.h` `SHADOW_ATLAS_SIZE`) before the scene's ordinary draw order. Spotlight and directional
features each consume atlas cells; point lights reserve a native-style 2×3
block for +X/−X/+Y/−Y/+Z/−Z faces. The atlas uses hardware depth comparison
and the stock nine-tap PCF shader path.

- Spotlights use a perspective projection spanning twice the authored outer
  cone and their radius, with a stable alternate up axis for vertical lights.
- Directional lights use the authored three cascade distances. Each
  orthographic projection encloses the camera-frustum slice and snaps to the
  shadow texel grid to suppress shimmer.
- Point lights render six 90° projections and supply the compact projection
  coefficients expected by `CalculateProjectedCoordsPoint`.
- Shadow casters currently include opaque CModel submeshes (static or
  skinned). Alpha-cutout/translucent model casters and non-model objects are
  not part of the shadow pass. Exact bias/edge parity remains a live visual
  verification item.

## Material and texture compatibility

Recent Sonic-focused compatibility work preserves authored
`alphatocoverage`, registers `TEX<n>FORMAT` for every sampler, derives packed
component feature combos from TEX flag bits 20–23, merges authored/user
texture overrides, and accepts WE shader constructs involving uniform-backed
initializers and duplicate macros. Texture-cache identity includes the
project's `AssetLocator`, preventing equal relative names from resolving to a
previous wallpaper; uploads validate the actual decoded payload layout.

For tangent-space normal mapping, `CModel` keeps the inverse-transpose
orientation but normalizes its basis columns. This removes object scale (often
`0.01`) from the shader's unnormalized tangent basis and prevents normal maps
from being amplified by 100×. These code fixes improve the Sonic scenes but do
not yet close the reported floor-texture/translucent-mask mismatch.

Translucent `generic4` model passes also select the stock
`DOUBLESIDEDLIGHTING` branch unless the material explicitly authors that combo.
This keeps thin back-lit meshes from retaining alpha while their one-sided PBR
result falls to black; Saturn's ring is the reference case. The rule is based
on pass state rather than a wallpaper or material-name exception.

## Fog

Distance and height fog use the stock `common_fog.h` path. Scene-level
`fogdistance*`/`fogheight*` settings drive `FOG_DIST`/`FOG_HEIGHT` and the
`g_FogDistance*`/`g_FogHeight*` uniforms; a material only participates when
its authored `FOG` combo is non-zero, matching the native `FOG_COMPUTED`
gate. The uniform parameters are packed as `{start, end-start, startDensity,
endDensity-startDensity}`, and the stock shader applies the authored squared
density ramp. Scene property scripts are ticked without a `thisLayer` object,
so scripted colors such as the day/night fog in 3708206626 update live.

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
- Layer-dependent initializers are queued until the complete object graph has
  registered its scriptable layers. Scripts may therefore resolve
  `thisScene.getLayer(...)` during initialization without depending on authored
  construction order.

## Known gaps (priority order for the test wallpapers)

1. **HDR post pipeline** (`hdr`, `bloomhdr*`) — **backlogged 2026-07-17** until
   the Sonic ordinary-material sweep is complete. The full
   downsample/blur/upsample/final-combine chain is documented in
   [[WE Reference Mining]] and currently skipped. Global RGBA16F FBOs were an
   **outdated experiment and were reverted**; ordinary targets are RGBA8.
   The **LDR** bloom chain (the same one WE uses for 2D scenes) now runs on 3D
   scenes as of 2026-07-26 — see "Closed since the previous snapshot". Because
   it thresholds an RGBA8 target, highlights that would exceed 1.0 in HDR clamp
   before `g_BloomThreshold` is applied, so bloom is weaker than reference WE on
   very bright sources. The separate `hdr_downsample` + `BLOOM` combo chain
   (`assets/materials/util/hdr_downsample_bloom.json`) is the real fix.
2. **Sonic material/transparency parity**: ordinary format/component metadata,
   texture scoping, alpha-to-coverage, and tangent-space scaling are fixed, but
   floor materials and the character's translucent mask still need a precise
   extracted-pass comparison.
3. **Morph targets/model extensions** (`g_Morph*`, MDMP/MDLE): skeletal
   animation is supported; morph payloads and texture-backed morph evaluation
   are not.
4. **Reflection and volumetric post paths**: alternate model/view matrices,
   mipmapped-framebuffer reflection inputs, and the volumetrics material chain
   are not orchestrated yet.
5. **ILayer interaction verification**: `getParent`/`getChildren`, object-space
   transforms, `lookAt`, reparenting, attachment access, and texture-animation
   controls are implemented (`a1e1a186`, source checked 2026-09-06).
   `thisScene.getLayer`, `cursorWorldPosition`, and `cursorLeftDown` also
   work at the API level; the Sykm dock and live interaction still need
   verification.
6. **Perspective override FOV** (`perspectiveoverridefov`) remains unsupported.

## Closed since the previous snapshot

- **Implemented 2026-07-17:** shared 3D skeletal animation, GPU skinning,
  animated MDAT attachment resolution, auxiliary submesh flags, spotlight
  shadows, three-cascade directional shadows, and six-face point shadows.
- **Outdated/reverted:** global RGBA16F FBOs. Ordinary targets are RGBA8; the
  correct HDR/bloom pipeline remains gap 1 rather than a format toggle.
- **Implemented 2026-07-26:** LDR bloom on 3D scenes. `CScene` used to refuse it
  outright (`"Bloom is not supported on 3D scenes yet, ignoring"`), which
  affected **97 of the 110 3D scenes in the local corpus** — nearly all of them
  enable `general.bloom`. The refusal turned out to be unnecessary rather than a
  crash workaround: `git log -S` shows it was introduced by the 3D-support
  commit `1ec8b71` itself, i.e. never attempted. The bloom helper is a synthetic
  object (id `-1`, `bloomimagewpenginelinux`) whose four passes all write to
  explicit targets, so pass 1 uses `glm::ortho` over the scene-sized quad and
  passes 2-4 use identity × a full NDC quad — the camera never enters, and
  `buildFrameRenderOrder` only permutes `Model3D` slots so the helper keeps its
  index. `_rt_4FrameBuffer`/`_rt_8FrameBuffer`/`_rt_Bloom` were already created
  unconditionally in `setupFramebuffers`, so this adds **no** FBO memory, only
  four draw passes.
- **Implemented 2026-07-26:** `general.bloomtint` (`g_BloomTint`). The engine
  never parsed it, for 2D either, so every bloom rendered with the shader's
  white default. Only **5 corpus scenes** author a genuinely non-white tint
  (3337088805, 3371684680, 3455121165, 3523698439, 3557068717) — an earlier
  count of "77" was wrong, it mistook user-property-bound `{user,value}` dicts
  for tints. One of the five (`3455121165`) authors `0 0 0`, i.e. bloom off,
  which today renders as full-strength white. Fixed in `WallpaperParser` so 2D
  benefits too.
