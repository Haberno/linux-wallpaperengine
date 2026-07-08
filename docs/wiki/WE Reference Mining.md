---
type: Reference
title: WE Reference Mining
description: What the Windows Wallpaper Engine install contains and how to mine binaries/assets for 1:1 rendering and scripting parity.
resource: file:///home/admin/.steam/steam/steamapps/common/wallpaper_engine
tags: [linux-wallpaperengine, wallpaper-engine, reverse-engineering, shaders, scenescript]
timestamp: 2026-07-08T05:30:00-04:00
---

# WE Reference Mining

The Windows install at
`~/.steam/steam/steamapps/common/wallpaper_engine` is the parity reference.
Findings below were extracted 2026-07-08 (engine build of 2025/2026).

## Where things live

| What | Where |
|---|---|
| Scene renderer + generated shader templates + scene.json key tables | `wallpaper64.exe` (plain `strings -n 6` works; only ~13k strings) |
| SceneScript VM | `bin/scenescript64.dll` — it is **plain V8**, no API strings; the embedder API is in the exe |
| **SceneScript typed API (authoritative)** | `ui/dist/monaco/autocomplete/lib.sceneScript.d.ts` (2570 lines, every interface + doc links) |
| SceneScript stdlib injected into contexts | `assets/scripts/jsclasses/baseclasses.js` (Vec2/Vec3/Vec4/Mat3/Mat4 as plain JS classes; file ends exporting their prototypes), `assets/scripts/jsmodules/{wemath,wevector,wecolor}.js` |
| Stock shaders incl. PBR/fog/blur helpers | `assets/shaders/*.h`, `*.frag/vert` (`common_pbr_2.h` has all light helpers) |
| Post-processing material chain | `assets/materials/util/*.json` |
| Editor snippets showing API usage | `ui/dist/monaco/snippets/*.js` |

Model loading uses `bin/assimp-vc143-mt64.dll` (assimp!) for **editor
imports**; runtime MDL is custom.

## Generated PerformLighting_V1 (from wallpaper64.exe)

Signature:
`vec3 PerformLighting_V1(vec3 worldPos, vec3 color, vec3 normal, vec3 viewVector, vec3 specularTint, vec3 ambient, float roughness, float metallic)`
(`ambient` is what generic4.frag passes `f0`/baseReflectance into.)

Per-light templates (unrolled per index `i`, `const uint i = ...`):

```glsl
// point (shadowless / shadowed)
vec3 lightDelta = g_LPoint_Origin[i].xyz - worldPos;
light += ComputePBRLightShadow(normal, lightDelta, viewVector, color,
    g_LPoint_Color[i].rgb, g_LPoint_Color[i].w, g_LPoint_Origin[i].w,
    specularTint, ambient, roughness, metallic, 1.0 /* or shadowFactor */);

// directional
light += ComputePBRLightShadowInfinite(normal, g_LDirectional_Direction[i].xyz,
    viewVector, color, g_LDirectional_Color[i].rgb, specularTint, ambient,
    roughness, metallic, 1.0 /* or shadowFactor */);

// spot: cookie = -dot(normalize(lightDelta), g_LSpot_Direction[i].xyz)
//   spotCookie = smoothstep(g_LSpot_Direction[i].w, g_LSpot_Origin[i].w, spotCookie);
//   optional texture cookie: texSample2D(COOKIE_SAMPLER, projectedCoords.xy).rgb
//   then ComputePBRLightShadow(..., g_LSpot_Color[i].rgb * cookie,
//       g_LSpot_Color[i].w, g_LSpot_Exponent[i].x, ...)

// tube: lightDelta = PointSegmentDelta(worldPos, g_LTube_OriginA[i].xyz, g_LTube_OriginB[i].xyz)
//   color/radius from g_LTube_Color, exponent from g_LTube_OriginA[i].w
```

Shadow path: `CalculateProjectedCoords(worldPos, g_LFeature_ShadowProjection[i])`
→ `PerformShadowMapping(projectedCoords, g_LFeature_ShadowProjectionTransform[i])`;
directional uses 3 cascades (`p1/p2/p3` indices, `CalculateProjectedCoordsCascades`,
mix by `projectedCoords.w`); point lights use
`CalculateProjectedCoordsPoint(worldPos, g_LPoint_Origin[i].xyz,
g_LFeature_ShadowPointProjection[i], g_LFeature_ShadowPointProjectionTransform[i])`
+ `PerformPointShadowMapping`. Material names for the casters exist as
strings: `pointshadow`, `directionalshadow`, `spotshadow`, `spotcookie`,
`spotshadowcookie`.

## Engine-level shader combos (exe string table)

`SCENE_ORTHO`, `REVERSEDEPTH`, `FOG_DIST`, `FOG_HEIGHT`, `LIGHTS_COOKIE`,
`LIGHTS_SHADOW_MAPPING`, `LIGHTS_SHADOW_MAPPING_QUALITY`, `FORMAT`.
`common_pbr_2.h` branches on `REVERSEDEPTH` (shadow bounds test);
`genericimage3/4.frag` branch on `SCENE_ORTHO` (view vector).

## Uniform inventory beyond what we set

Fog: `g_FogDistanceColor/Params`, `g_FogHeightColor/Params` (scene keys
`fogdistance*`/`fogheight*`: start/end + startdensity/enddensity). HDR:
`g_HDRParams`. View basis: `g_ViewForward/Right/Up`,
`g_OrientationForward/Right/Up`. Skinning/morphs: `g_Bones`, `g_BonesAlpha`,
`g_MorphOffsets/Weights/BoneRules/BoneTransform` (morph data via
`g_Texture5` "morph" sampler in generic4.vert). Reflection pass:
`g_AltModelMatrix`, `g_AltViewProjectionMatrix`, `g_AltNormalModelMatrix`.
Legacy per-object light arrays: `g_LightsColorPremultiplied`,
`g_LightsColorRadius`, `g_LightsPosition`. Misc: `g_Daytime`,
`g_PointerState`, `g_RenderVar0..4` (per-pass params of the post chain),
`g_ViewportViewProjectionMatrices`, `g_TextureReductionScale`.

## HDR bloom + post chain (materials/util)

3D scenes use the HDR pipeline (`bloomhdrfeather/iterations/scatter/strength/threshold`
scene keys), not the 2D bloom object:

1. `hdr_downsample_bloom.json` → shader `hdr_downsample` with `BLOOM=1`
   (threshold via `g_RenderVar0`, params `g_BloomStrength`,
   `g_BloomBlendParams`, `g_BloomTint`, `g_BloomScatter`)
2. blur pyramid: `blur_h_bloom.json`, `downsample_quarter_bloom`,
   `downsample_eighth_blur_v` (+ `blur_k3`)
3. upsample: `combine_hdr_upsample.json` (variants `_linear`, `_dbg`,
   `combine_dhdr_upsample`)
4. final: `combine_hdr.json` reading `_rt_FullFrameBuffer` (also
   `combine_ldr`, `combine_srgb`, `combine_video_hdr` variants)

Other util materials: `fade.json` (camerafade: fullscreen translucent quad,
shader `fade`, `color * 0.7` with `g_Alpha`), `ccsimple.json` (LUT color
correction, `lutparams`), `composelayer_depthtest.json`,
`backbufferpassthrough.json`, volumetrics chain
(`volumetrics_{front,back,blur_h,blur_v,combine,fullscreen}.json` with
`_rt_volumetrics*` targets). MSAA target name:
`_rt_FullFrameBufferMultiSampled`.

## SceneScript facts that matter for our ScriptEngine

- Vectors/matrices are **plain JS classes** (baseclasses.js), not native
  handles: `update(value)` receives a real `Vec3` instance; the engine reads
  the **returned** value. Vec3 constructor accepts strings `"x y z"`, other
  vecs, or numbers. A future refactor could inject WE's own baseclasses.js
  into QuickJS instead of maintaining C++ vector adapters.
- `AudioBuffers { left, right, average: Float32Array }` from
  `engine.registerAudioBuffers(16|32|64)`; must be called at global scope.
- `input.cursorScreenPosition` is in **pixels**; `cursorWorldPosition` is
  world-space with only x/y meaningful.
- `engine.registerAsset(file, precache)` exists for script-created layers
  (`IAssetHandle`), also global-scope-only.
- Full interface list: ILayer = IImageLayer + ISoundLayer + IEffectLayer +
  ITextLayer + IParticleSystem + IModel + ICamera; globals: `thisLayer`,
  `thisScene` (IScene: `getLayer(name)`, ...), `engine`, `input`,
  `renderContext`, `localStorage`, `console`, `shared`.

## Mining tips

- `strings -n 6 wallpaper64.exe > dump.txt` then `rg` the dump; the scene
  key table sits in one contiguous region (search `camerafade`), the
  generated-shader templates in another (search `PerformLighting_V1`).
- `strings -el` (UTF-16) adds almost nothing (~500 strings).
- Don't bother disassembling `scenescript64.dll` — it's stock V8; all WE
  semantics are in the exe + the d.ts + baseclasses.js.
