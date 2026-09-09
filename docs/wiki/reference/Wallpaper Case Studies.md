---
type: Regression Reference
title: Wallpaper Case Studies
description: Workshop wallpapers that drove rendering fixes and serve as regression cases.
resource: file:///home/admin/.steam/root/steamapps/workshop/content/431960
tags: [linux-wallpaperengine, regression, workshop, test-cases]
timestamp: 2026-07-30T00:00:00-04:00
---

# Wallpaper Case Studies

Workshop wallpapers that drove fixes; useful as a regression suite.

## 3563790700 — Silksong (Hornet)
- MDLV0023, 1098 verts, 13 bones, 1 animation (~8.4 fps, 101 frames).
- Drove: initial skeleton/animation playback; **effects in image space, warp
  at final pass** (wave hit the hilt, glint missing, blade tip clipped by the
  image-sized FBO until the final-pass restructure).

## 3558034522 — MyGO eyes ([若叶睦]清夏)
- MDLV0023 eye layer `13眼组`: 707 verts, 9 bones, 30 s loop, blink at
  frames ~195–205 (eyelid bones scale-y → 0).
- Drove: **cull-face disable** (eyes vanished — cullmode normal + Y-flip
  winding), **fps fix** (blink was 4× too slow when float read as duration),
  **parallax depth inheritance** (character parts inherit parent's depth 0).
- 2026-07-08, clock/date/watermark text objects drove the CText overhaul:
  **parent-chain composition in the 2D path**, **parent visibility cascade**,
  **UTF-8 codepoint rasterization**, **pointsize = points at 300 DPI**,
  **scene y-up mapping** (day/date/time order + screen height), multi-line
  layout, and text effect chains (blurprecise glow). Its "Audio bar" object
  drove the capture drain + band auto-gain fixes.
- Drove the MDLV0023 auxiliary-position/range/descriptor decoder and ordinary
  single-mask clipping renderer. The eyelid skin is **user-verified working**;
  only nested/multi-mask composition remains open → [[Known Issues]].

## 3758354038 — honeycomb clock hub
- Text-heavy scene (5 clock/date/day font variants switched by user-property
  conditions), scale scripts on every text layer, vector-string `padding`.
- Drove: **JSON type-drift tolerance** (`optional<T>` was noexcept over a
  throwing conversion — one string-typed `padding` aborted the engine) and
  the vec2 padding parse. Good regression case for text layout +
  user-property visibility conditions.

## 3135984503 — One Piece (Luffy & Zoro)
- MDLV0017, Luffy 25 bones/1 anim, Zoro 31 bones/2 anims.
- Drove: **MDLV0017 acceptance**, **MDLS0002's pre-matrix bone-name layout**,
  **MDLA0004's 10-byte animation footer**
  (Zoro's second animation failed to parse), **multi-layer additive composition against
  one shared constraint-resolved reference pose** (hair/arm double-assembled otherwise),
  removal of the locktransforms parallax exclusion.

## 3463520581 — Sword Art Online (Asuna & Kirito)
- MDLV0023 with nested scene children bound through MDAT attachment names:
  `Attachment` → `Attachment bottom` → `head`, plus two back-hair levels.
- Drove: **MDLS named-bone parsing**, **MDLA0006's 35-byte footer**, and full
  **MDAT puppet attachments**. Ignoring MDAT left every mesh assembled in
  isolation but placed Asuna's hair and upper body down-left of their bones.
- Regression: all attachment levels must use the parent's live animated bone
  pose, so the assembled character remains intact during hair/breathing motion.

## 3294687155 — Elden Ring (Midra)
- MDLV0021 with MDLS0003 skeletons and MDLA0006 animations across the head,
  cape, arm, body, and weapon puppet meshes.
- Regression: MDLS0003 uses the legacy post-matrix bone-name layout, while
  MDLA0006 uses the newer inverted Z-rotation convention. It verifies that
  skeleton layout and animation rotation are selected independently.

## 2639381674 — Halloween puppet
- MDLV0013 compact vertices with MDLS0001 and MDLA0001.
- Regression: MDLS0001 uses the legacy post-matrix bone-name layout;
  MDLA0001 keeps the original rotation sign and a four-byte footer.

## 3100265648 — Gojo (Jujutsu Kaisen)
- MDLV0017, 2182 verts, 21 bones, 3 simultaneous animation layers.
- Drove: **z-flatten** (face/hand had z up to ±700 → clipped invisible),
  **dedicated puppet composite pass** (pulse/foliagesway effects manipulate
  positions), **v_TexCoord widening fix** (both backgrounds failed to set up),
  **root default depth 1** (backgrounds didn't drift).
- Also exposed the media-update segfault → [[Known Issues]].
- Gray left edge with the cursor at the far left: fixed the missing root-origin
  term in camera parallax. Its background root sits at X=525.984 on a 1920-wide
  canvas; both background halves share that origin. Native translation at the
  left extreme is +5.06 px, not the former +52.8 px. Zoom is correctly 1.0.
  Fixed-cursor engine capture now covers the edge; user confirmation pending.

## 3768356757 / 3755078205 — parallax overscan

- **Stratospheric Twilight [4K]** and **Dark Leaf | northway.** author
  `general.zoom` 1.03 and 1.05 respectively. The 2D parser formerly ignored
  these values and used 1.0, exposing the gray clear color near mouse corners.
- The parser now honors the authored zoom. Fixed-cursor engine screenshots
  reproduce the strips before the change and remove them after it.
- Dark Leaf additionally uses the `depthparallax` effect; its shader inputs and
  ray-march strength were not changed. User confirmation pending.


## 2665939987 — pinned-character scene
- The wallpaper that motivated the (wrong) locktransforms exclusion: its
  background is pinned by **explicit depth 0**, girl drifts at −0.05.

## 2488626583 — Last Train
- Keyframed origin property animation test case (~45 s loop, 8 s hold).

## 3005674933 — depth-map parallax background
- `bg` layer: `parallaxDepth 0 0` (pinned) + a `depthparallax` effect pass
  (`bg_depth` r8 depth texture, `center 0.3`, `scale 0.7 0.7`, `sens 1.0`).
- Drove: distinguishing per-object camera-parallax translation from the
  per-pixel `depthparallax` occlusion-mapping effect — this wallpaper's tilt
  is 100% the latter. Exposed that `g_ParallaxPosition` (shared by both
  systems) and the hardcoded-identity `g_EffectTextureProjectionMatrix` are
  candidate root causes for a weak/flat-feeling tilt → [[Parallax System]].

## 3589454154 — 土星 | Saturn - Sykm

- The first full 3D milestone scene: perspective camera, static MDLV models,
  depth-tested translucent rings, scripted moon orbits, directional/point
  lighting, skybox, fog, transparent sorting, and directional/point shadows.
- Drove: perspective scene parsing, static model rendering, depth attachments,
  generated LightingV1 code, world-space script transforms, camera selection,
  fog, translucent model sorting, double-sided lighting for thin translucent
  model passes, and the directional/point shadow paths.
- It authors HDR bloom, which remains intentionally backlogged; judge the base
  material/lighting path separately from the missing glow/post-processing.

## Sonic Rooftop — 3D material regression case

- Animated/skinned Sonic and the `Stage` model exercise GPU bone uploads,
  model attachments, tube lights, authored material metadata, per-project
  texture lookup, transparency, and the 3D shadow paths.
- Drove: shared 3D skeletal evaluation, GPU skinning, animated attachments,
  auxiliary submesh flag `0x400`, texture upload validation, material format
  combos, alpha-to-coverage, and tangent-basis normalization.
- **Still open:** the floor can look untextured and Sonic can appear inside a
  translucent bubble/mask. Treat this as ordinary texture/material/pass-state
  parity first; HDR and bloom remain a separate backlog item.

## OS Waves — 2D compatibility case

- Its scripted user settings and `parallaxDepth` wrapper drove live property
  evaluation and support for `UserSetting`-backed parallax values.
- Its clock is covered by the current CText implementation, and its logo pulse
  effects exercise the recovered audio-spectrum pipeline.
- Orthographic camera auto-size is now implemented. Still open: general image
  `autosize` and the passthrough-without-effects early return require parity
  work.
