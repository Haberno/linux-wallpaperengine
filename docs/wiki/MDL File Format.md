---
type: Format Reference
title: MDL File Format
description: Reverse-engineered MDLV, MDLS, and MDLA binary format notes for puppet rendering.
resource: file:///home/admin/repos/linux-wallpaperengine/docs/rendering/MDL_FILES.md
tags: [linux-wallpaperengine, mdl, reverse-engineering, binary-format]
timestamp: 2026-07-06T20:00:00-04:00
---

# MDL File Format

Reverse-engineered July 2026; verified against MDLV0017, MDLV0021, MDLV0023
(all share the layout). Canonical copy also in `docs/rendering/MDL_FILES.md`.
Consumed by the [[Puppet Warp Pipeline]].

## MDLV — mesh

```
"MDLV00XX\0"
3 DWORDs
material json path (NUL-terminated), zero padding
tag DWORD
DWORD vertexByteLength
VERTEX[vertexByteLength / 80]
DWORD indicesByteLength
WORD indices[...]        // triangles
```

VERTEX (stride 80):
| offset | field |
|---|---|
| 0 | position vec3 — assembled model-space rest pose; it does not generally mirror the texture atlas |
| 12 | 16 bytes unknown (mostly 0/1 constants) |
| 40 | blend indices uvec4 |
| 56 | blend weights vec4 (sum to 1) |
| 72 | uv vec2 |

`position.z` is part layering only (can reach ±700) — flatten when rendering.

In some files (e.g. MyGO eyes) extra data sits between the indices and MDLS:
a second per-vertex vec3 array (pure atlas positions) plus per-bone index
ranges and `masks/clipping_mask_*` texture references — an eyelid/clipping
system we don't consume yet.

## MDLS — skeleton

```
"MDLS000X\0", DWORD absoluteNextSectionOffset, DWORD boneCount
per bone:
  // MDLS0004: name (NUL) here
  // MDLS0001/0002/0003: one leading byte here, name (NUL) after the matrix
  DWORD type            // 0/1 observed
  DWORD parent (i32)    // always an earlier bone; -1 = root
  DWORD matrixBytes     // 64
  float[16]             // row-major local bind, translation in row 3
                        // (byte-identical to column-major w/ translation in last column)
  // MDLS0004: BYTE zero separator
  // MDLS0001/0002/0003: name (NUL), often constraint JSON with "tp"/"tm"
```

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
frame. This supports nested attachment chains and makes child layers follow
their parent's puppet animation.

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
    DWORD zero, DWORD frameBytes
    frames of 9 floats: T3 R3 S3 (rotation about z only)
    // frameCount+1 entries; last == first for loops
  zero footer           // MDLA0001: 4; MDLA0004: 10; MDLA0006: 35 bytes
```

Animation frames store absolute bone poses. Additive layers must be composed
as deltas from their own frame 0. MDLA0006 stores Z rotations with the
opposite sign from MDLA0001/0004; normalize that sign while parsing rather
than changing the scene-space transform convention globally.
