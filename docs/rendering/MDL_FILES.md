# MDL Puppet Files

Wallpaper Engine uses MDL containers for 2D puppet-warp meshes as well as 3D
models. This page documents the 2D sections consumed by `CImage`.

All values are little-endian. Section headers are eight ASCII characters plus
a NUL terminator. The first DWORD after MDLS, MDAT, and MDLA is the absolute
file offset of the next section.

## MDLV — mesh

The official loader derives the vertex stride from the serialized format mask,
not from the MDLV version. The installed Workshop library currently contains:

| Format mask | Stride | Observed MDLV versions |
|---|---:|---|
| `0x00000000` | 52 | 0013, 0014 |
| `0x01800009` | 52 | 0016 |
| `0x0180000f` | 80 | 0017, 0019, 0021, 0023 |
| `0x0181000e` | 84 | 0023 |

Unknown masks must not be treated as one of these layouts merely because the
header starts with `MDLV00`; a wrong stride can still produce superficially
valid byte lengths while assigning vertices to unrelated bones.

```text
"MDLV00XX\0"
version-specific header and material path
DWORD vertexFormatMask
DWORD vertexByteLength
VERTEX vertices[]
DWORD indexByteLength
WORD indices[]
```

80-byte vertex layout:

| Offset | Field |
|---|---|
| 0 | position `vec3` — assembled model-space rest pose |
| 12 | unknown 28-byte block |
| 40 | blend indices `uvec4` |
| 56 | blend weights `vec4` |
| 72 | UV `vec2` |

The texture UVs may address a disassembled parts atlas even though the vertex
positions are already assembled. Vertex Z is part layering, not scene depth.

Both 52-byte formats store position at 0, blend indices at 12, blend weights at
28, and UV at 44. The 84-byte format stores position at 0, blend indices at 44,
blend weights at 60, and UV at 76.

## MDLS — skeleton

```text
"MDLS000X\0"
DWORD absoluteNextSectionOffset
DWORD boneCount
per bone:
  MDLS0004: name (NUL-terminated)
  MDLS0001/0002/0003: one leading byte
  DWORD type
  DWORD parentIndex (signed; -1 is root)
  DWORD matrixBytes (64)
  float localBind[16]
  MDLS0004: constraint descriptor (NUL-terminated; commonly empty, sometimes JSON)
  MDLS0001/0002/0003: name (NUL-terminated), often constraint JSON
```

The matrix is stored row-major with translation in its last row, which is
byte-identical to a GLM column-major matrix with translation in its last
column. Compose parent-first to obtain bone world transforms.

Bone names are often empty, which can hide a version mismatch. MDLS0004 moved
the name before the numeric fields but retains a separate descriptor after the
matrix; MDLS0001, MDLS0002, and MDLS0003 keep their single name/descriptor
field after the matrix.

## MDAT — named attachments

```text
"MDAT0001\0"
DWORD absoluteNextSectionOffset
WORD attachmentCount
per attachment:
  WORD boneIndex
  name (NUL-terminated)
  float localTransform[16]
```

Scene children opt in with `"attachment": "name"`. Their live attachment
transform is:

```text
animatedBoneWorld[boneIndex] * localTransform
```

Compose it between the parent's resolved scene transform and the child's own
local transform. This must be evaluated every frame and at every level of a
nested attachment chain.

## MDLA — animations

```text
"MDLA000X\0"
DWORD absoluteNextSectionOffset
DWORD animationCount
per animation:
  DWORD id
  DWORD unknown
  name (NUL-terminated)
  mode (NUL-terminated, for example "loop")
  float fps
  DWORD frameCount
  DWORD unknown
  DWORD boneCount
  per bone:
    DWORD bone flags
    DWORD frameBytes
    frames of 9 floats: translation vec3, rotation vec3, scale vec3
  version-specific footer
```

When no optional track metadata is present, MDLA0001 and MDLA0002 have a
four-byte zero footer, MDLA0003 has nine zero bytes, MDLA0004 has ten,
MDLA0005 has 34, and MDLA0006 has 35. These are empty encodings, not fixed
padding: newer files can place large versioned, length-prefixed track blocks in
the same area. `CImage` currently skips those unused blocks by locating and
fully validating the next core animation record. The files usually contain
`frameCount + 1` records per bone, with a repeated final record for a loop.

All three serialized Euler rotation components are significant. Wallpaper
Engine converts XYZ Euler values to quaternions before interpolating frames;
this avoids long rotations across angle wraparound. Rotation values retain
their serialized sign, and model space is converted to scene space after
skinning. Empty/`loop` modes wrap, `mirror` alternates direction, and `single`
clamps at the final frame.

Animation frames are absolute poses. The first authored layer's frame 0 is the
assembled visual baseline, which is not necessarily interchangeable with the
MDLS skinning bind transform. Its `blend` weights motion away from frame 0.
Later additive layers are converted to deltas from their own frame 0, weighted
from identity, and composed onto the accumulated pose; non-additive layers
blend toward their sampled absolute pose. Layer `visible`, `animation`, `rate`,
`blend`, `additive`, `blendin`, `blendout`, and `blendtime` values may change at
runtime and therefore must be evaluated continuously.

## Other observed sections

The current official binary contains markers for `MDMP0001` and `MDLE0002`,
and both occur in installed MDLV0023 files. They follow the core puppet
sections and are not currently consumed by `CImage`; their semantics still
need to be established before implementing them. Unknown trailing sections
are deliberately left untouched instead of being guessed from their version.

The binary also contains `MDLVS001`, but its sole code reference copies the
literal into an output buffer in a separate serializer path; it is not one of
the sections dispatched by the MDL puppet loader.
