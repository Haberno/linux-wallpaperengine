---
type: Format Reference
title: MDL File Format
description: Reverse-engineered MDLV, MDLS, MDAT, and MDLA binary format notes shared by 2D puppets and 3D models.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine/src/WallpaperEngine/Data/Parsers/MdlParser.cpp
tags: [linux-wallpaperengine, mdl, reverse-engineering, binary-format]
timestamp: 2026-07-30T00:00:00-04:00
---

# MDL File Format

Reverse-engineered July 2026 and checked against the installed Workshop
library. This is the canonical copy. The official loader derives the vertex
stride from the format mask, not the MDLV version. `CImage` consumes the 2D
puppet layouts; `CModel` consumes static and skinned 3D layouts through the
shared mesh/animation parsers. See [[Puppet Warp Pipeline]] and
[[3D Scene Support]].

## MDLV — mesh

```
"MDLV00XX\0"
version-specific fields
material json path (NUL-terminated), zero padding
DWORD vertexFormatMask
DWORD vertexByteLength
VERTEX[vertexByteLength / stride]
DWORD indicesByteLength
WORD indices[...]        // triangles
```

Observed layouts:

| mask | stride | versions | blend indices | weights | UV |
|---|---:|---|---:|---:|---:|
| `0x00000000` | 52 | 0013, 0014 | 12 | 28 | 44 |
| `0x01800009` | 52 | 0016 | 12 | 28 | 44 |
| `0x0180000f` | 80 | 0017, 0019, 0021, 0023 | 40 | 56 | 72 |
| `0x0181000e` | 84 | 0023 | 44 | 60 | 76 |

VERTEX (stride 80) detail:
| offset | field |
|---|---|
| 0 | position vec3 — assembled model-space rest pose; it does not generally mirror the texture atlas |
| 12 | 16 bytes unknown (mostly 0/1 constants) |
| 40 | blend indices uvec4 |
| 56 | blend weights vec4 (sum to 1) |
| 72 | uv vec2 |

`position.z` is part layering only (can reach ±700) — flatten when rendering.

### MDLV0021+ auxiliary and draw-range tail

Revision 21 adds two optional payloads after the final submesh. Revision 23
then adds clipping descriptors which refer to the second payload:

```
// MDLV0021+
BYTE hasAuxiliaryPositions
if hasAuxiliaryPositions:
  DWORD auxiliaryHeader               // always 1 in the observed corpus; semantics unknown
  DWORD auxiliaryPositionBytes
  BYTE auxiliaryPositions[...]        // vec3[] in the clipping sample

BYTE hasDrawRanges
if hasDrawRanges:
  DWORD drawRangeBytes
  DrawRange ranges[drawRangeBytes / 16]

// MDLV0023+
DWORD clippingDescriptorCount
ClippingDescriptor descriptors[clippingDescriptorCount]
```

`DrawRange` is four little-endian u32 values:

| offset | field | evidence |
|---:|---|---|
| 0 | bone/group ID | matches the sole weighted bone for every referenced vertex in the MyGO model |
| 4 | submesh index | zero for every one-submesh puppet checked |
| 8 | first index | offset into the submesh index buffer, not a vertex offset |
| 12 | index count | triangle-list index count; zero-length bone groups are retained |

The official loader obtains the record count with `drawRangeBytes >> 4` and
rejects descriptor references outside that count. The table is not
clipping-specific. Across 57 MDLV0023 models from 11 installed workshops,
every model had a range table, while only 11 models had clipping descriptors.
The records partitioned each index buffer contiguously, including authored
groups with a zero index count. Group IDs need not be sorted and one bone can
own more than one record. Checks against both stride-80 and stride-84 vertex
layouts confirmed that the first field selects the bone whose weighted
geometry dominates that range; boundary vertices may also carry neighboring
bone weights.

The auxiliary payload is length-prefixed raw data rather than an implicit
vertex-count array. All 17 models in the corpus that enabled it stored exactly
`vertexCount * 12` bytes and a leading u32 of `1`. Decoding the target sample
as vec3 values produces the pre-assembly/source positions used by the eye
parts; the official loader retains the payload separately from the main vertex
buffer. Models without any clipping descriptors can also carry it, so it is
general puppet auxiliary geometry rather than descriptor-owned data. No
meaning for the leading u32 beyond the observed value `1` has been established.

### MDLV0023 clipping descriptors

Each descriptor has the following variable-length layout:

```
QWORD opaqueId
char maskAsset[]                       // NUL-terminated, no "materials/" prefix
DWORD flags
DWORD targetRangeCount
DWORD targetRangeIndices[targetRangeCount]
DWORD sourceRangeCount
DWORD sourceRangeIndices[sourceRangeCount]
```

Both lists contain zero-based indices into the `DrawRange` table, not bone IDs
or raw index-buffer offsets. Wallpaper Engine validates both lists while
loading. At runtime it also derives parent relationships between descriptors,
allowing one clipping result to be composed into another.

The flags are only partly named:

| bit | official runtime behavior |
|---:|---|
| 0 | selects one of two internal descriptor modes (`1` or `2`) |
| 1 | inverts the mask's red channel in `clippingmaskimage4.frag` |
| 2–3 | alter draw grouping/composition; their editor-facing meaning is not yet identified |

Nineteen corpus descriptors had flags zero. One descriptor in workshop
3629379075 had `flags=1`, proving bit 0 is authored data rather than a
loader-only state; no corpus sample exercised bits 1–3.

`opaqueId` really is one u64—the bytes previously read as the u32 pair
`191, 0`—not a per-entry prefix. Across 20 descriptors it varied by mask
definition and repeated when several descriptors shared the same mask asset.
That pattern is consistent with an editor-side mask-definition ID, but it is
retained without being read by any loader, grouping, composition, or draw
function traced in the 2026-07-08 `wallpaper64.exe` build. It therefore remains
deliberately opaque rather than being guessed as a material or bone ID.

#### Measured clipping sample

`models/13眼组_puppet.mdl` from workshop 3558034522 is MDLV0023, 144364
bytes, with `MDLS0004` at 72657:

| offset | contents |
|---:|---|
| 63905 | auxiliary flag `1` |
| 63906 | auxiliary header `1` |
| 63910 | byte length `8484`; 707 vec3 values at `[63914, 72398)` |
| 72398 | range-table flag `1` |
| 72399 | byte length `128`; eight records at `[72403, 72531)` |
| 72531 | clipping descriptor count `2` |
| 72535 | descriptor 0 |
| 72596 | descriptor 1 |
| 72657 | `MDLS0004` |

The eight range records are:

| range | group | submesh | first index | index count |
|---:|---:|---:|---:|---:|
| 0 | 1 | 0 | 0 | 582 |
| 1 | 2 | 0 | 582 | 291 |
| 2 | 3 | 0 | 873 | 240 |
| 3 | 4 | 0 | 1113 | 465 |
| 4 | 5 | 0 | 1578 | 822 |
| 5 | 6 | 0 | 2400 | 696 |
| 6 | 7 | 0 | 3096 | 243 |
| 7 | 8 | 0 | 3339 | 291 |

Consequently, the earlier `3339, 291, 2` interpretation crossed a structure
boundary: `3339, 291` are the final range's first index and index count, while
`2` at 72531 is the descriptor count.

Both descriptors have `opaqueId=191`, flags zero, and asset
`masks/clipping_mask_e5b07ba8`. Descriptor 0 has target list `[2]`;
descriptor 1 has target list `[3]`; both have source list `[0, 1]`. Resolving
those references gives:

| descriptor | target draw range | source draw ranges |
|---:|---|---|
| 0 | group/bone 3, indices `[873, 1113)` | groups/bones 1 and 2, indices `[0, 873)` |
| 1 | group/bone 4, indices `[1113, 1578)` | groups/bones 1 and 2, indices `[0, 873)` |

The apparent difference `2` versus `3` is therefore a **draw-range index**.
The two eyelid targets share the same source eye geometry and the same 428x475
mask atlas at `materials/masks/clipping_mask_e5b07ba8.png`.

#### Official Wallpaper Engine render path

This was checked in the installed Windows binary
`wallpaper64.exe` (timestamp 2026-07-08, SHA-256
`40e2ce021e9352324fadb3b8f72b8ba2a7ee95b71cc571d5b9f84be75cd993b0`):

| Ghidra function | observed responsibility |
|---|---|
| `FUN_140261880` | main MDLV loader; gates auxiliary/range data at version 21 and descriptors at version 23 |
| `FUN_14020b720` | resolves target/source range lists into clipping draw groups |
| `FUN_14020cab0` | walks descriptor parents and emits simple-mask or nested-compose commands |
| `FUN_14020d6a0` | loads the descriptor's mask asset, applies flags, and draws the mask source |
| `FUN_140208670`, `FUN_140208c80` | execute the clipping command stream for the image render paths |

1. The source ranges are drawn with
   `materials/util/clippingmaskimage4.json`. Its fragment shader combines the
   source material's albedo alpha with the external mask texture's red channel
   into `_rt_FullAlphaMask`.
2. The target ranges use their original material with `CLIPPINGTARGET`; the
   generic image fragment shader samples that render target through
   `g_Texture8` in screen space and multiplies the target alpha by its red
   channel.
3. Nested descriptors use `_rt_FullAlphaMaskIntermediate` and
   `CLIPPINGCOMPOSE` commands before the final target draw.

So this feature is a render-target mask pipeline, not a stencil block and not
a direct “bone 2/bone 3” mask lookup. `MdlParser::parse` now reads the
version-gated auxiliary positions, draw-range table, and clipping descriptors.
The renderer implements the ordinary single-mask, zero-flags path with the
official mask shader and `g_Texture8`; nested composition and nonzero
descriptor flags currently fall back to the unmasked puppet draw. The
repeatable structural decoder is `tools/reversing/inspect-mdl-clipping.py`.

### 3D CModel layouts and submeshes

3D model containers may hold multiple submeshes, each with its own material,
flags, bounding box, vertex payload, and index payload. Currently supported:

| vertex tag | stride | attributes | rendering |
|---|---:|---|---|
| `15` | 48 | position@0, normal@12, tangent4@24, UV@40 | static |
| `0x0180000f` | 80 | same, blend indices@40, weights@56, UV@72 | GPU skinned |

Submesh flag bit 0 selects 32-bit indices; otherwise indices are 16-bit and
widened by the parser. The independent `0x400` auxiliary flag appears on
BoostModel assets and does **not** change index width. Unknown bits, mixed
vertex layouts, invalid byte lengths, and out-of-range indices are rejected.
The parser unions the serialized per-submesh bounding boxes. `CModel` exposes
the resulting extent as the legacy `thisLayer.size` SceneScript property used
by older Workshop scenes that bind 2D pivot scripts to 3D models.

For a skinned CModel, `MdlAnimationParser` loads MDLS/MDAT/MDLA from the same
container. `MdlAnimationEvaluator` produces world and inverse-bind-relative
skin matrices; CModel uploads affine mat4x3 values to `g_Bones`. The same pose
drives named attachments and the shadow caster shader.

## MDLS — skeleton

```
"MDLS000X\0", DWORD absoluteNextSectionOffset, DWORD boneCount
per bone:
  // MDLS0002/0004: name (NUL) here
  // MDLS0001/0003: one leading byte here, name (NUL) after the matrix
  DWORD type            // 0/1 observed
  DWORD parent (i32)    // always an earlier bone; -1 = root
  DWORD matrixBytes     // 64
  float[16]             // row-major local bind, translation in row 3
                        // (byte-identical to column-major w/ translation in last column)
  // MDLS0002/0004: constraint descriptor (NUL), often empty but sometimes JSON
  // MDLS0001/0003: name (NUL), often followed by constraint data
```

The parser deliberately selects this layout from the MDLS version, independent
of the MDLV or MDLA versions. This corrects an older note that incorrectly
grouped `MDLS0002` with the post-matrix-name variants.

World bind = walk hierarchy. Skinning uses `inverse(worldBind)`.

## MDAT — named attachments

`scene.json` children may specify `"attachment": "name"`. MDAT binds that
name to a puppet bone and a local transform:

```
"MDAT0001\0", DWORD absoluteNextSectionOffset, WORD attachmentCount
per attachment:
  WORD boneIndex
  name (NUL-terminated)
  float[16] localTransform
```

The live attachment transform is
`animatedBoneWorld[boneIndex] * localTransform`. It is composed between the
parent object's transform and the attached child's local transform every
frame. This supports nested attachment chains and makes child layers/lights
follow either a 2D puppet or a skinned 3D model.

## MDLA — animations

```
"MDLA000X\0", DWORD absoluteNextSectionOffset, DWORD animationCount
per animation:
  DWORD id              // matches scene.json animationlayers[].animation
  DWORD unknown
  name (NUL), mode (NUL, e.g. "loop")
  float fps             // NOT duration
  DWORD frameCount, DWORD unknown, DWORD boneCount
  per bone:
    DWORD boneFlags, DWORD frameBytes
    frames of 9 floats: T3 R3 S3
    // frameCount+1 entries; last == first for loops
  DWORD blendTrackCount
  per blend track:
    DWORD trackType, DWORD trackBytes
    float samples[trackBytes / 4]
  // MDLA0002+
  BYTE hasPerBoneScalarTracks
  if hasPerBoneScalarTracks:
    per bone:
      DWORD trackFlags, DWORD trackBytes
      float samples[trackBytes / 4]
  versioned optional track metadata
```

Every observed blend and per-bone scalar track stores `frameCount+1` floats.
The former feeds `g_BlendMap` for `puppettexturechannels`; the latter's runtime
meaning is not yet consumed, but the official loader reads it before the
remaining version-specific event/constraint data. With no optional metadata,
the rest of the trailing encodings are 4 bytes for MDLA0001/2, 9 for
MDLA0003, 10 for MDLA0004, 34 for MDLA0005, and 35 for MDLA0006. They must not
be treated as fixed zero padding: newer files store large length-prefixed
per-track blocks there. The renderer steps over the known scalar stream, then
locates and fully validates the next core animation record across metadata it
does not yet consume.

Workshop 3577990983 is the regression sample for this boundary. Its
MDLA0006 section contains clips `1678`, `2078`, `258`, and `3194`; clip `258`
is the 300-frame, 30 FPS eye loop. Before the per-bone scalar stream was
recognized, a byte sequence inside the first stream looked enough like a
header to produce three bogus clips with ID `16256`, invalid UTF-8 names, and
a positive subnormal FPS. Candidate records now require valid UTF-8 and a
normal finite FPS, so all four authored clips reach the animation evaluator.

Animation frames store absolute bone poses. All XYZ Euler components are
converted to quaternions before interpolation. Rotation values retain their
serialized sign; the model-to-scene Y conversion happens after skinning.
Empty/`loop` modes wrap, `mirror` ping-pongs, and `single` clamps.

Wallpaper Engine keeps one persistent reference-pose buffer for the whole
model. The first authored bone sample is the usable constraint-resolved
form of that reference in this renderer; raw MDLS is still retained for the
skinning inverse bind. Every additive layer is a component-wise weighted delta
from the same shared reference, while non-additive layers blend toward their
sampled absolute pose. This distinction preserves later one-shot entrance
clips (Lucy, 3521337568) without double-assembling constrained MDLS0002 bones
(3135984503, 3100265648, 3176098264). Layer visibility, animation, rate, blend,
additive, and blend-transition properties are live values and are re-evaluated
while rendering.

## Other observed sections

`MDMP0001` and `MDLE0002` appear after the core sections in some MDLV0023
files and are also named by the current official binary. Neither CImage nor
CModel consumes them yet; they are likely related to the still-missing morph
path and remain documented as unknown rather than parsed with guessed
semantics.

`MDLVS001` is another binary literal seen in reference material, but this
fork's code never references it. It is not dispatched as a puppet section by
the main MDL loader.
