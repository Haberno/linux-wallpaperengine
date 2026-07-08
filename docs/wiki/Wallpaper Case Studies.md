---
type: Regression Reference
title: Wallpaper Case Studies
description: Workshop wallpapers that drove rendering fixes and serve as regression cases.
resource: file:///home/admin/.steam/root/steamapps/workshop/content/431960
tags: [linux-wallpaperengine, regression, workshop, test-cases]
timestamp: 2026-07-08T13:30:00-04:00
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
- Still open: eyelid skin uses the clipping-mask system → [[Known Issues]].

## 3758354038 — honeycomb clock hub
- Text-heavy scene (5 clock/date/day font variants switched by user-property
  conditions), scale scripts on every text layer, vector-string `padding`.
- Drove: **JSON type-drift tolerance** (`optional<T>` was noexcept over a
  throwing conversion — one string-typed `padding` aborted the engine) and
  the vec2 padding parse. Good regression case for text layout +
  user-property visibility conditions.

## 3135984503 — One Piece (Luffy & Zoro)
- MDLV0017, Luffy 25 bones/1 anim, Zoro 31 bones/2 anims.
- Drove: **MDLV0017 acceptance**, **10-byte animation footer** (Zoro's second
  animation failed to parse), **multi-layer additive composition as deltas
  from each layer's own frame 0** (hair/arm double-assembled otherwise),
  removal of the locktransforms parallax exclusion.

## 3100265648 — Gojo (Jujutsu Kaisen)
- MDLV0017, 2182 verts, 21 bones, 3 simultaneous animation layers.
- Drove: **z-flatten** (face/hand had z up to ±700 → clipped invisible),
  **dedicated puppet composite pass** (pulse/foliagesway effects manipulate
  positions), **v_TexCoord widening fix** (both backgrounds failed to set up),
  **root default depth 1** (backgrounds didn't drift).
- Also exposed the media-update segfault → [[Known Issues]].

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
