# 3D Scene Support — Milestone 1 Design

- **Date:** 2026-07-08
- **Status:** Approved (design conversation), pending spec review
- **Target wallpaper:** 土星 | Saturn - Sykm (workshop 3589454154)
- **Milestone scope decision:** Core 3D without shadows/HDR ("Core 3D, no shadows")

## Goal

Load and render Wallpaper Engine 3D scenes (perspective camera, `model` objects,
`light` objects). Milestone 1 is complete when the Saturn wallpaper renders
correctly: planet, rings, moons, and skybox depth-correct under a perspective
camera, moons orbiting via their authored scripts, surfaces shaded by the
scene's directional/point lights. Shadows and HDR bloom are explicitly absent
and become follow-up milestones.

## Success criteria

1. Saturn scene loads with no parse exception (`orthogonalprojection: null`).
2. Isolated debug-window screenshots show depth-correct geometry (unlit stage,
   then lit stage).
3. Moon positions change over time (script-driven origins work in 3D chain).
4. 2D regression: Last Train (2488626583), Gojo (3100265648), MyGO
   (3558034522) screenshots unchanged before/after.
5. User signs off on live output (house rule: no automated final verification).

## Evidence gathered (2026-07-08)

### Saturn scene facts (extracted via repkg to scratchpad)

- `general.orthogonalprojection` is JSON `null`; `fov 50`, `nearz 0.01`,
  `farz 10000`, `zoom 1.0` live at the **general** level (2D scenes carry
  nearz/farz/fov in the `camera` section instead).
- Camera: real look-at — eye `(3.659, 1.387, 2.296)`, center
  `(3.303, 1.172, 1.387)`, up `+Y`.
- 130 objects: 24 `model`, 56 `image`, 17 `text`, 2 `light`, 1 particle,
  3 sound, 27 plain containers (transform/visibility groups, some scripted).
- Lights: 1 × `ldirectional` (active; scripted `angles` tracking the sun) and
  1 × `lpoint` (`visible: false`), both `castshadow: true`, parented to a
  scripted group (id 459). `general.lightconfig` declares
  `{directional:1, directionalshadow:1, point:1, pointshadow:1}`.
- All model materials use WE stock shader **`generic4`** with
  `depthtest/depthwrite: enabled`, `cullmode: normal`; combos seen:
  `LIGHTING` (0 on the skybox only), `REFLECTION 0`, `SHADINGGRADIENT 0`,
  `TINTMASKALPHA 0`. Ring material uses `blending: translucent`.
- `general.hdr: true` plus `bloomhdr*` keys (deferred);
  `transparentsorting: true` (deferred).
- Orbital mechanics run as object property scripts (`origin`, `angles`,
  `visible`) communicating through `shared.*` — the existing ScriptEngine /
  UserSetting machinery already executes these.

### MDL static-mesh format (decoded from `models/s1/s1.mdl`, a unit sphere)

Same MDLV container the puppet loader parses. Layout:

```
"MDLV0023\0"
3 DWORDs                      (15, 1, 1 in s1.mdl)
material json path, NUL-terminated
DWORD 0                       (unknown)
float[6] bounding box min/max
DWORD tag                     (15 — likely vertex attribute mask)
DWORD vertexByteLength
vertices, stride 48:
  offset  0  position  vec3
  offset 12  normal    vec3
  offset 24  tangent   vec4   (w = ±1 handedness)
  offset 40  uv        vec2
DWORD indexByteLength
uint16 triangle indices
zero footer
```

Verified statistically: positions unit-length (sphere), normals radial,
tangents unit and perpendicular to normals, w exactly ±1, UVs a clean grid.
Note: a pole vertex with `pos.x == 0.0` makes the first vertex look like
padding — do not trust eyeballed alignment; validate with the checks above.

This also decodes the puppet format's unknown bytes: puppet stride 80 =
pos3 + normal3 + tangent4 + blendindices4u + blendweights4f + uv2
(offsets 0/12/24/40/56/72 — matches the wiki's known 0/40/56/72).
Update `docs/wiki/MDL File Format.md` during implementation.

### Lighting contract (extracted from `wallpaper64.exe` strings — the
`LightingV1` chunk is embedded as plain shader source)

- Entry point: `vec3 PerformLighting_V1(vec3 worldPos, vec3 color,
  vec3 normal, vec3 viewVector, vec3 specularTint, vec3 ambient,
  float roughness, float metallic)`.
- WE generates the body per scene, unrolled per light
  (`const uint i = <n>;` blocks), calling `ComputePBRLightShadow` /
  `ComputePBRLightShadowInfinite` / `PointSegmentDelta` — all defined in the
  **shipped** `assets/shaders/common_pbr_2.h`. No BRDF code needs writing.
- Uniform arrays: `g_LPoint_Origin[i]` (xyz origin, w passed alongside
  intensity), `g_LPoint_Color[i]` (rgb, w), `g_LDirectional_Direction[i]`,
  `g_LDirectional_Color[i]`, `g_LSpot_Origin/Color/Direction/Exponent[i]`,
  `g_LTube_OriginA/OriginB/Color[i]`. Exact `Color.w` / `Origin.w` meaning
  (intensity vs radius) to be pinned against `ComputePBRLightShadow`'s
  parameter list in `common_pbr_2.h` during implementation.
- Shadow branches use `g_LFeature_Shadow*` uniforms — milestone 1 emits the
  no-shadow variants (`shadowFactor = 1.0`).

### Existing engine plumbing (already in place)

- `ShaderUnit::preprocessRequires` handles `#require`;
  `generateLightingV1()` currently returns a zero-light stub
  (`src/WallpaperEngine/Render/Shaders/ShaderUnit.cpp:363`).
- CPass already binds `g_ViewProjectionMatrix`, `g_ModelMatrix`,
  `g_NormalModelMatrix`, `g_LightAmbientColor`, `g_LightSkylightColor`
  (`CPass.cpp` ~862–883). Missing: `g_EyePosition`, all `g_L*_*` arrays.
- MDLV container parsing exists in the puppet path (`CImage.cpp`).
- Script properties support restored (commit f2c3d06).

### Known gaps (the work)

- Parser throws on `orthogonalprojection: null`
  (`WallpaperParser.cpp:29`); `model`/`light` objects unparsed
  (`ObjectParser.cpp` logs "not supported yet").
- `Render::Camera` is ortho-only; the computed lookAt is unused for ortho.
- Scene FBOs have no depth attachment; depth test globally disabled
  (`CWallpaper.cpp:412`).
- No 3D model render object; no light uniform plumbing.

## Design

### 1. Data model & parser

- `parseScene` accepts `orthogonalprojection` null/absent → perspective flag
  on projection data. Read `fov/nearz/farz/zoom` from `general` first,
  falling back to the `camera` section (preserves 2D behavior).
- New `Model` object type: mdl path, scale, angles, visible + base
  `ObjectData` (origin/angles scripts already flow through UserSettings).
- New `Light` object type: type string (`lpoint`, `ldirectional`; spot/tube
  parse-but-warn), color, intensity, radius, exponent, castshadow (stored,
  unused this milestone).
- Light counts derive from parsed light objects, not `lightconfig`.

### 2. MDL static meshes & CModel

- Shared MDLV static-mesh loader (tag-15 layout above) returning material
  path + vertex/index buffers. Puppet path in CImage untouched.
- New `Render::Objects::CModel`: VAO/VBO/EBO from the mesh, material loaded
  via the existing MaterialParser, rendered through CPass so `generic4`
  compiles through the existing shader translation with its combos.

### 3. Camera & transforms

- `Camera::setPerspectiveProjection` using `glm::perspective` and the
  already-computed lookAt view. Scene FBO sized to output resolution for 3D
  scenes. `zoom != 1` logged and ignored this milestone (Saturn is 1.0).
- World transforms compose origin/angles/scale up the parent chain
  (following CImage's iterative resolveTransform pattern), honoring
  `disablepropagation`.

### 4. Depth & passes

- Scene FBO gains a depth attachment **only for 3D scenes** (2D pipeline
  byte-identical).
- CPass honors material `depthtest`/`depthwrite`/`cullmode` for model draws;
  depth state restored around 2D-style draws.
- Authored render order kept; `transparentsorting` deferred (ring
  translucency relies on authored order + depth test this milestone).

### 5. Lighting

- `generateLightingV1()` emits the unrolled loop matching the extracted WE
  source for N directional + M point lights, `shadowFactor = 1.0`.
- CScene collects visible lights per frame (world-space position/direction
  via their scripted transform chains); CPass binds `g_EyePosition` and the
  `g_LDirectional_*` / `g_LPoint_*` arrays.
- Verify the values feeding `g_LightAmbientColor`/`g_LightSkylightColor`
  (`ambientcolor`/`skylightcolor` are parsed already).

### 6. Verification

Staged, per house rules (debug `--screenshot` on isolated windows OK; live
output judged by the user):

a. Scene loads; unlit geometry screenshot (LIGHTING forced 0).
b. Lit screenshot (stub replaced, lights bound).
c. 2D regression screenshots: Last Train, Gojo, MyGO unchanged.
d. User inspects live wallpaper for sign-off.

## Deferred (follow-up milestones, record in Known Issues)

- Shadow mapping (directional cascades + point shadows; scene declares both;
  ring/planet eclipse shadows are visibly part of this wallpaper's look).
- HDR pipeline + `bloomhdr*` path (`general.hdr: true`).
- `transparentsorting` (back-to-front translucent sorting).
- Spot/tube lights, 3D skeletal animation (MDLA quaternions), morphing
  (`MORPHING` combo), screen-space reflection (`_rt_MipMappedFrameBuffer`),
  fog.

## Risks / open questions (resolve data-driven during implementation)

- `fov` vertical vs horizontal semantics; WE Euler angle order in 3D;
  UV/winding handedness under the transform chain → settle against the
  Rosetta sphere and WE reference screenshots.
- `g_LPoint_Color[i].w` vs `g_LPoint_Origin[i].w` packing → read
  `common_pbr_2.h` signatures.
- Image/text objects (clock UI, backdrop quads parented to models) are
  world-space quads that must render under the perspective VP — in scope;
  contained risk since CImage already builds per-object model matrices.

## Documentation obligations

- Update `docs/wiki/MDL File Format.md` (normal/tangent decode, static
  layout) and `docs/wiki/Known Issues.md` (deferred shadow/HDR items).
- New wiki concept page for the 3D scene pipeline once verified.

## Non-goals

- Matching WE frame-for-frame on HDR/bloom/shadows in this milestone.
- Refactoring the puppet loader beyond what mesh-loader sharing needs.
- General cleanup from `Candidate Refactors.md`.
