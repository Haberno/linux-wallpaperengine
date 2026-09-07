---
type: Reference
title: Upstream Feature Inventory
description: Every feature Wallpaper Engine announced from launch to 2.8.42, grouped by subsystem, with which ones this fork must implement and where it stands.
resource: https://store.steampowered.com/news/app/431960
tags: [linux-wallpaperengine, wallpaper-engine, parity, upstream, changelog]
timestamp: 2026-08-19T00:00:00-04:00
---

# Upstream Feature Inventory

> **Consolidated checklist (2026-09-06):** [Project checklist](../../Organized%20all.md). It reconciles these notes with the current
> source and separates implementation, local work in progress, and pending
> verification. Dated measurements below remain historical snapshots.

What upstream Wallpaper Engine actually ships, derived from its official
announcements. Use this to check whether an unfamiliar authored key or scene
behavior corresponds to a real upstream feature before treating it as garbage
data, and to see which parity gaps are renderer work versus permanently
out of scope.

**Source and coverage.** Steam news feed for app 431960
(`ISteamNews/GetNewsForApp`), 76 unique posts spanning 2016-10-10 (Early
Access launch) to 2026-06-29 (2.8.42). That is every announcement Valve's
feed returns; small hotfix builds that were never posted as news are not
represented, and neither is anything shipped silently. Read 2026-08-19.

## How to read the fork column

This fork is a **runtime**, not an authoring suite. Roughly two thirds of the
upstream feature set is editor, Workshop, Windows-shell, or mobile surface
that has no renderer analogue here. Those rows are marked `n/a` and are not
gaps.

| Mark | Meaning |
|---|---|
| ✅ | Implemented in the fork (see [[Current Status]] for verification state) |
| ◐ | Partially implemented, or implemented with known divergence |
| ❌ | Renderer-relevant and not implemented |
| ? | Renderer-relevant, never assessed |
| n/a | Authoring / storefront / OS-shell feature with no runtime side |

Marks reflect [[Current Status]] as of 2026-07-30 plus a source scan on
2026-08-19; they are coarse by design. The per-feature detail lives in the
concept pages, not here.

## Wallpaper types

| Feature | Since | Fork |
|---|---|---|
| Scene wallpapers (`.pkg`, 2D/3D) | 1.0 launch | ✅ |
| Video wallpapers (mp4 / HEVC / 4K) | 1.0 launch | ✅ via mpv |
| WebM | 1.0.357 (2016-11) | ◐ mpv-dependent |
| Web wallpapers (CEF/Chromium) | 1.0 launch | ◐ CEF 150, always built (no `WITH_WEB` gate yet) |
| Web livestream mode (Edge WebView) | 1.7 (2021-09) | n/a Windows-only backend |
| Native GIF (sprite-sheet decoder, not Chromium) | 1.0.357 (2016-11) | ? |
| Static image wallpapers, mass import | 1.3.136 (2020-09) | n/a app feature |
| Application wallpapers (arbitrary `.exe`) | 1.0 launch | n/a |

**Application wallpapers were removed from the Workshop in 2.8.42
(2026-06-29)** after a malware incident — uploads and downloads are both
blocked, existing items were set friends-only. They affected ~0.5% of items.
Nothing to implement, and no reason to: the type is dead upstream.

## Scene rendering core

| Feature | Since | Fork |
|---|---|---|
| DirectX 11 only (DX9 + OpenGL removed) | 1.3 (2020-07) | n/a — fork is GL |
| Layer types: image, solid, text, sound, particle, model | through 1.2 | ✅ |
| Transform layer | 2.3 (2023-09) | ? |
| Composition layers (adjustable + full), post-processing layer | 2.0.97 rename | ◐ |
| Photoshop blend modes per layer and per effect | 1.0.675 (2017-04) | ✅ |
| Layer-as-texture-input / offscreen targets | 1.0.675 (2017-04) | ✅ |
| Layer hierarchy, parent/child attachment | 2.3 (2023-09) | ✅ |
| Layer locking / visibility / opacity / tint / alignment | various | ✅ |
| Auto-crop of transparent regions | 1.2 (2020-05) | n/a compile-time |
| LZ4 texture compression | 1.0.675 (2017-04) | ✅ |
| DXT1/DXT3 compression | 1.0.1329 (2018-06) | ✅ |
| Mip streaming, third-detail-level-first load | 2.6 (2025-02) | ◐ see [[Load Performance]] |
| HDR bloom (`hdr`, `bloomhdr*`), "Ultra" post-processing | 1.4 (2020-12) | ❌ backlogged; LDR bloom ✅ |
| Bloom tint | 2.4.82 (2024) | ✅ |
| Display HDR output | 2.0.97 (2022-01) | n/a |
| Reverse-Z depth buffer | 2.7 (2025-09) | ? |
| Frustum culling (main + shadow viewports) | 2.4 (2024-02) | ? |
| Transparent object sorting | 2.3 (2023-09) | ✅ |
| Video textures (mp4 as image layer) | 1.7 (2021-09) | ✅ see [[Video Texture Memory Growth]] |
| Texture variants (multiple textures per layer) | 2.6 (2025-02) | ? |
| Texture projection from spot lights | 2.4 (2024-02) | ◐ cookie targets exist |
| Texture formats: PNG/TGA/JPG/BMP/PSD/ICO/GIF/DDS | 1.0.291 (2016-11) | ✅ compiled to TEX — see [[Texture File Format]] |
| TIFF import | 2.4 (2024-02) | n/a import-side |

## Effects

Effects are **data**, not engine features: each ships as a shader plus a JSON
pass config inside the wallpaper or the stock `assets/`. Fork coverage is
therefore a property of the shader translator and the pass runner, not a
per-effect checklist — see [[Shader Translation]].

The complete upstream library, in release order:

- **2017**: water flow, shake (+opacity mask for eye blinks), god rays, film
  grain, clouds, nitro, fire
- **2018**: shine, VHS-era additions, dust motes, vapor, magic, lightning
  discharge, x-ray
- **2020–21**: VHS, refraction, iris movement, depth parallax (ML depth maps,
  ships as a separate free editor DLC because the model is multi-GB)
- **2022**: fluid simulation, cursor ripple, chromatic aberration, gradient
  blend, radial blur, swing, twirl
- **2024–25**: light shafts (rewritten), glitter, shimmer, water caustics,
  cloud motion

Plus the long-standing ones with gizmos and perspective variants: perspective,
spin, water ripple, water waves (dual wave since 2.3), reflection, blur /
precise blur (with drop-shadow and outline composition modes), blend (up to
six textures), foliage sway (per-pixel), pulse.

Two structural facts matter more than the list:

- **Custom effects are user-authorable.** The 1.2 shader editor (2020-05) lets
  creators write HLSL effects from scratch and publish them as Workshop
  assets, so the effect namespace is open-ended. Any translator that only
  handles the stock library will fail on Workshop content.
- **Effects carry paint-brush masks** (opacity, flow, color, blur, spin,
  pinch/spread, fixed-direction), authored in the editor and shipped as
  textures.

## Particles

| Feature | Since | Fork |
|---|---|---|
| Particle editor, SSE2 sim, event hierarchies | 1.0.746 (2017-05) | ✅ |
| Control points (inheritable, rotatable since 2.7) | 1.0.746 / 2.7 | ◐ |
| Renderers: sprite, sprite trail, rope, rope trail | through 1.0.1329 | ◐ |
| Sprite sheets, perspective rendering, fixed/upright orientation | 1.0.866 (2017-09) | ◐ |
| Audio-reactive emitters / initializers / operators | 1.0.1113 (2018-03) | ❌ CPU audio still TODO; source checked 2026-09-06 |
| `emitParticles()` script hook | 2.0.97 (2022-01) | ? |
| Real-time lighting on particles | 2.7 (2025-09) | ❌ |
| Collision operators | 2.7 (2025-09) | ❌ |
| Boids flocking operator | 2.7 (2025-09) | ❌ |
| Value inheritance parent→child, remap operators | 2.7 (2025-09) | ❌ |
| HSV / color-list initializers, cap velocity, image-layer emitter | 2.7 (2025-09) | ❌ |
| 3D particle editor mode + 3D defaults | 2.7 (2025-09) | n/a authoring |

The whole 2.7 particle block is recent enough that little Workshop content
depends on it yet. Treat it as low-priority until a case study needs it.

## Puppet warp (2D character rigging)

| Feature | Since | Fork |
|---|---|---|
| Geometry analysis, bone/limb skeleton, weight painting | 1.4 (2020-12) | ✅ see [[Puppet Warp Pipeline]] |
| Physics / jiggle bones | 1.4 (2020-12) | ? verify simulation separately from skeletal clip playback |
| Inverse kinematics + bone blend rules | 2.1 (2022-06) | ? |
| Texture channel animations | 2.2 (2022-10) | ◐ single-row `g_BlendMap` overlay exists |
| Alpha bone animations | 2.2 (2022-10) | ? |
| Depth-map painting (replaced basic extrusion) | 2.2 (2022-10) | ◐ |
| Vertex editing + vertex/blend-shape animation | 2.4 (2024-02) | ❌ morph payloads unsupported |
| Animatable bone depth ordering | 2.5 (2024-05) | ? |
| Clipping masks | 2.6 (2025-02) | ◐ single-mask ✅, nested/nonzero-flags ❌ |
| Rope physics with global gravity + wind | 2.6 (2025-02) | ❌ |
| Auto mesh depth generation | 2.6 (2025-02) | n/a authoring |
| Character sheets, reference poses, cross-limb weight blending | 1.6 / 2.5 | ◐ |
| Attachment points to other assets | 2.3 (2023-09) | ✅ |
| Foreground separation, normal map generator | 1.6 (2021-06) | n/a authoring |
| Bone constraints (`"tp"` / `"tm"`, mouse-interactive) | — | ❌ open gap |

## 3D

| Feature | Since | Fork |
|---|---|---|
| Model import: FBX/OBJ/DAE/3DS/BLEND/X/LXO (assimp) | 1.0.291 (2016-11) | n/a — runtime is MDL, see [[MDL File Format]] |
| Skeletal animation | 2.3 (2023-09) | ✅ GPU-skinned |
| Blend-shape / morph animation | 2.3 (2023-09) | ❌ MDMP/MDLE unsupported |
| Animation clips, model editor | 2.3 (2023-09) | ✅ clip playback |
| PBR model shader | 2.3 (2023-09) | ✅ |
| Physics bones on models | 2.3 (2023-09) | ◐ |
| Model hitboxes + click events | 2.3 (2023-09) | ? |
| Camera system: multiple cameras, paths, timeline-animated | 2.3 (2023-09) | ✅ see [[Camera Path Playback]] |
| Extra model shaders: vegetation, fur, chroma | 2.4 (2024-02) | ? |
| Distance + height fog | 2.4 (2024-02) | ✅ |
| Model data / GPU buffer sharing | 2.8 (2026-05) | ✅ ported |
| Dynamic model generation from SceneScript (`createModelData`, `IModelData`) | 2.8 (2026-05) | ❌ |
| Perspective override FOV (`perspectiveoverridefov`) | — | ❌ open gap |

## Lighting

| Feature | Since | Fork |
|---|---|---|
| Real-time PBR lighting + reflections on 2D layers | 1.6 (2021-06) | ✅ LightingV1 |
| Light types: point, spot, tube, directional | 2.3 (2023-09) | ✅ all four |
| Light limit 4 → 12 | 2.3 (2023-09) | ✅ |
| Light limit removed, nearest-N rendering | 2.6 (2025-02) | ? |
| Shadow mapping (spot / point / directional) | 2.4 (2024-02) | ✅ all three paths |
| Alpha-to-coverage shadows (foliage) | 2.4 (2024-02) | ❌ shadow pass only admits normal/opaque model materials; alpha-to-coverage material blending exists |
| Volumetric lighting | 2.4 (2024-02) | ❌ material chain not orchestrated |
| Radius/intensity/falloff controls, light source size | 2.4 (2024-02) | ◐ |
| Mipmapped-framebuffer reflections | — | ❌ documented, not built |

## Animation and scripting

| Feature | Since | Fork |
|---|---|---|
| Keyframe timeline animation on every property | 1.4 (2020-12) | ◐ |
| Curve editor, auto-bezier, loop wrap, channel copy | 1.5–2.4 | n/a authoring |
| SceneScript (JavaScript via V8) | 1.1.154 (2019-04) | ✅ see [[SceneScript Runtime]] |
| Audio data + cursor access from script | 1.1.154 (2019-04) | ✅ |
| `setTimeout` / `setInterval` | 1.4 (2020-12) | ✅ |
| `emitParticles()` | 2.0.97 (2022-01) | ? |
| `playSingleAnimation()`, animation-end callbacks | 2.1 (2022-06) | ? |
| `lookAt`, `getChildren`, world matrix access | 2.4 (2024-02) | ✅ `a1e1a186`, source checked 2026-09-06; interaction verification pending |
| `localStorage` (persists across restarts) | 2.4 (2024-02) | ◐ in-memory implementation exists; restart persistence missing |
| Vector min/max/mix, Vec/Mat linear algebra | 2.4 / 2.8 | ✅ WEVector/WEMath |
| `applyGeneralSettings` (user language) | 2.7 (2025-09) | ? |
| Blend-shape / local bone transform manipulation per frame | 2.5 (2024-05) | ❌ |
| Script sharing as Workshop assets | 1.3.75 (2020-08) | n/a |

## Text

| Feature | Since | Fork |
|---|---|---|
| Text layers, HarfBuzz + FreeType shaping, multi-line | 1.1.232 (2019-09) | ◐ FreeType text/multiline implemented; full shaping not established |
| Custom fonts, emoji with modifiers | 1.1.232 (2019-09) | ◐ CJK fallback ❌ |
| Blur composition for drop shadow / outline on fonts | 1.1.232 (2019-09) | ✅ effect chains |
| Max width + row limits | 2.2 (2022-10) | ❌ open gap |
| MSDF smooth scaling, outlines, drop shadows, letter spacing, effect padding | 2.8 (2026-05) | ◐ ordinary effect shadows/padding exist; MSDF and letter spacing remain |
| Stock text assets: clock, 3D clock, countdown, greeting | 1.1.232 (2019-09) | ✅ render as ordinary scenes |

Vertical and RTL text have **never** been supported upstream — if a wallpaper
looks wrong in those scripts, that is authored-side, not a fork gap.

## Audio

| Feature | Since | Fork |
|---|---|---|
| FFT audio response (FFTS), 64-band | 1.0.513 (2017-02) | ✅ native-compatible mapping |
| Recording device selection | 1.0 launch | ◐ `--audio-device` not ported |
| Threshold / noise gate | 1.1.154 (2019-04) | ? |
| Smoothing + normalization | 1.0.1113 (2018-03) | ✅ |
| Scene sound layers: mp3 / ogg / wav / flac | 1.0.1113 (2018-03) | ✅ + dedupe and rotation |
| Sound spatialization | 2.7 (2025-09) | ❌ |
| Auto-mute when other apps play audio | 1.0.959 (2017-11) | ✅ `--noautomute`, corked-stream aware |
| Per-monitor mute, global mute | 1.3 (2020-07) | ◐ global `--silent`/`--volume` exist; independent per-output mute needs audit |
| Media / album cover integration (Windows Global Media Sessions) | 2.2 (2022-10) | ✅ ported (MPRIS-side) |

## User-facing wallpaper customization

| Feature | Since | Fork |
|---|---|---|
| User properties: color, slider, checkbox, combo, text input | 1.1.31 (2018-12) | ✅ `--set-property`, `--list-properties` |
| Texture / video user properties | 2.1 (2022-06) | ? |
| User shortcut property (launch local app/folder/URL on click) | 2.7 (2025-09) | ❌ |
| Foldable property groups | 2.4 (2024-02) | n/a UI |
| Per-monitor property values | 1.0.795 (2017-07) | ✅ |
| Structural rebuild on visibility-gating property change | — | ❌ open gap |
| Alignment: cover / fill / center / stretch / free | 1.0.562 → 1.4 | ✅ `--scaling` |
| Brightness / contrast / saturation / hue shift | 1.4 (2020-12) | ◐ contrast + saturation only |
| 25 image filters | 2.7 (2025-09) | ❌ |
| Scene flip (horizontal) | 1.3 (2020-07) | ❌ |
| Playback rate, volume, mouse parallax toggle | various | ◐ `--disable-parallax` |
| Save / load / share property sets | 1.0.795 (2017-07) | n/a |
| Workshop presets as first-class items | 1.0.1454 (2018-10) | n/a |

## Playback and desktop integration

| Feature | Since | Fork |
|---|---|---|
| Pause/stop on maximized, fullscreen, focus, audio | 1.0 launch | ✅ `--no-fullscreen-pause`, `--fullscreen-pause-only-active` |
| Per-application playback rules | 1.0.1369 (2018-07) | ◐ `--fullscreen-pause-ignore-appid`, `--automute-ignore` |
| Auto-load wallpaper/playlist/profile per launched app | 1.3 (2020-07) | ❌ |
| Battery / laptop mode, display-sleep behavior | 1.0.795 (2017-07) | n/a |
| VRAM exhaustion stop | 1.4 (2020-12) | ❌ |
| Safe start after crash / hibernation | 1.0.959 / 1.2 | n/a |
| FPS limit, quality presets, dynamic texture reduction | various | ◐ `--fps`, `--render-scale` |
| Global hotkeys | 1.0.1424 (2018-09) | n/a |
| Command-line control API | 1.1.301 (2019-12) | ✅ + control socket (`prop`) |
| Screensaver mode | 1.6 (2021-06) | n/a |
| Desktop icon opacity / hide, DWM accent color sync | 1.0.400 / 1.0.456 | n/a |
| Static lockscreen + system wallpaper sync | 1.7 (2021-09) | n/a |
| Keyboard / mouse input passthrough to web wallpapers | 1.1.0 (2018-11) | ◐ `--disable-mouse` |
| Per-virtual-desktop wallpapers (experimental) | 2.1 (2022-06) | ❌ |
| Play in window (for OBS), borderless | 1.0.959 / 2.2.18 | ✅ `--window` |

## Multi-monitor

| Feature | Since | Fork |
|---|---|---|
| Per-monitor wallpapers | 1.0 launch | ✅ `--screen-root` |
| Monitor groups and splits, bezel correction | 1.0.795 (2017-07) | ◐ `--screen-span` |
| Clone mode + source display selection | 1.0.1182 / 1.5 | ❌ |
| Horizontal flip of a cloned display | 2.5 (2024-05) | ❌ |
| Layout profiles (save/load/hotkey) | 1.0.1424 (2018-09) | n/a |
| Copy / swap wallpapers and settings between monitors | 1.5 (2021-02) | n/a |
| Per-monitor mute | 1.3 (2020-07) | ? distinguish independent per-output mute from global controls and soundtrack dedupe |
| Parallax clamped to the owning screen | 1.3 (2020-07) | ? see [[Parallax System]] |
| Four+ monitor identification strategies | 1.0.823 → 2.2 | n/a Windows-specific |

Dual-monitor frame cost on this machine is a compute limit, not an engine
overhead problem — that investigation is parked, not open.

## Playlists

| Feature | Since | Fork |
|---|---|---|
| Order / random, timer intervals | 1.0.513 (2017-02) | ✅ `--playlist`, timer + sequential |
| Time-of-day mode with draggable timeline | 1.0.1394 (2018-08) | ◐ parsed |
| Day-of-week mode | 2.0 (2021-11) | ◐ parsed |
| Intro wallpaper (first entry plays once at startup) | 1.6 (2021-06) | ? |
| Freeze / manual advance, duplicates allowed | 1.0.1394 (2018-08) | ? |
| Per-entry preset overrides | 1.0.1454 (2018-10) | n/a |
| Shared timer sync across monitors | 1.1.31 (2018-12) | ? |
| Playlist state persists across restarts | 1.6 (2021-06) | ? |
| ~27 shader-based transitions + random | 2.5 (2024-05) | ◐ fork has its own transitions |

## Workshop, browser, editor, hardware, mobile

All `n/a`. Listed so the inventory is complete and so nobody mistakes an
absence here for a gap.

- **Browser/Workshop**: in-app browser, Discover tab (curated / seasonal /
  by-creator / "Best of 20XX" / followed authors), filters (genre, type,
  resolution with per-setup recommendations, age rating, anime filter,
  mobile-compatible), folders, multi-select, thumbnail cache, browse-by-author,
  local author blocking, in-app reporting and content rating, offline Workshop
  cache, fallback query server for Steam outages.
- **Editor**: snap layout, undo/redo, gizmos (multi, perspective, rotary,
  vertex), asset browser and asset packs, HLSL shader editor, Monaco script
  editor with autocomplete, brush panel, particle previews and docs, project
  cleaning, publish flow with GIF preview recording, user-property test window.
- **Asset sharing** (1.2, 2020-05): creators publish effects, particles,
  scripts, models, and sounds as standalone Workshop items. This is why
  unknown effect names appear in Workshop scenes.
- **Hardware**: Corsair iCUE (2018-11) and Razer Chroma (2020-02) LED sync
  across Scene, Video, and Web wallpapers, with a dedicated LED layer, LED
  boost, brightness, and source-monitor selection.
- **Mobile**: free Android companion app (2.0, 2021-11) — wireless pairing,
  `.mpkg` export, gyroscope parallax, Material You theming, high-performance
  export at device resolution. No Workshop access, all wallpapers muted.
  Android 10+ since 2.8. No macOS, iOS, or official Linux build has ever
  existed.

## Platform history worth knowing

- Windows 7 and 8 support was **dropped in 2.5** (2024-05), following Steam.
- DirectX 9 and OpenGL were **removed in 1.3** (2020-07) as buggy; DX11 has
  been the only backend since.
- The app went 64-bit by default in 2.7 and moved the UI process to 64-bit in
  2.8 (`ui32.exe` → `wallpaperui.exe`).
- 2.8 mentions overhauling the shader precompiler and "optimizing the HLSL
  translator" — the only upstream acknowledgement of the translation layer
  [[Shader Translation]] reimplements.

## Related

- [[WE Reference Mining]] — what the shipped Windows binary and assets reveal,
  which is authoritative where this page is only announcement-level.
- [[Current Status]] — verification state for every ✅ and ◐ above.
- [[TODO Backlog]] — the ❌ rows that are actually queued.
- [[Wallpaper Case Studies]] — installed wallpapers that exercise specific
  features listed here.
