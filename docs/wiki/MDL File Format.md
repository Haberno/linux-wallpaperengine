---
type: Format Reference
title: MDL File Format
description: Reverse-engineered MDLV, MDLS, and MDLA binary format notes for puppet rendering.
resource: file:///home/admin/repos/linux-wallpaperengine/docs/rendering/MDL_FILES.md
tags: [linux-wallpaperengine, mdl, reverse-engineering, binary-format]
timestamp: 2026-07-06T20:00:00-04:00
---

# MDL File Format

Reverse-engineered July 2026 and checked against all MDL files in the installed
Workshop library. The official loader derives the vertex stride from the
format mask, not the MDLV version. Canonical copy also in
`docs/rendering/MDL_FILES.md`. Consumed by the [[Puppet Warp Pipeline]].

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
  // MDLS0004: constraint descriptor (NUL), often empty but sometimes JSON
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
    DWORD boneFlags, DWORD frameBytes
    frames of 9 floats: T3 R3 S3
    // frameCount+1 entries; last == first for loops
  versioned optional track metadata
```

With no optional metadata, the trailing encodings are 4 bytes for MDLA0001/2,
9 for MDLA0003, 10 for MDLA0004, 34 for MDLA0005, and 35 for MDLA0006. They
must not be treated as fixed zero padding: newer files store large
length-prefixed per-track blocks there. The renderer skips metadata it does not
yet consume by locating and fully validating the next core animation record.

Animation frames store absolute bone poses. All XYZ Euler components are
converted to quaternions before interpolation. Rotation values retain their
serialized sign; the model-to-scene Y conversion happens after skinning.
Empty/`loop` modes wrap, `mirror` ping-pongs, and `single` clamps.

The first authored animation's frame 0 is the assembled visual baseline; MDLS
is the skinning bind transform and cannot safely replace that baseline. The
first layer's blend weights motion from its frame 0. Later additive layers are
composed as weighted deltas from their own frame 0, while later non-additive
layers blend toward their sampled absolute pose. Layer visibility, animation,
rate, blend, additive, and blend-transition properties are live values and are
re-evaluated while rendering.

## Other observed sections

`MDMP0001` and `MDLE0002` appear after the core sections in some MDLV0023
files and are also named by the current official binary. `CImage` does not yet
consume them; they remain documented as unknown rather than being parsed with
guessed semantics.

`MDLVS001` is another binary literal, but its single code reference writes it
to an output buffer in a separate serializer path. It is not dispatched as a
puppet section by the main MDL loader.
