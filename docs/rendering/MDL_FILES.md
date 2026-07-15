# MDL Puppet Files

Wallpaper Engine uses MDL containers for 2D puppet-warp meshes as well as 3D
models. This page documents the 2D sections consumed by `CImage`.

All values are little-endian. Section headers are eight ASCII characters plus
a NUL terminator. The first DWORD after MDLS, MDAT, and MDLA is the absolute
file offset of the next section.

## MDLV — mesh

Observed versions MDLV0017, MDLV0021, and MDLV0023 share an 80-byte vertex
layout. MDLV0013 uses a compact 52-byte layout.

```text
"MDLV00XX\0"
version-specific header and material path
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

MDLV0013 instead stores position at 0, blend indices at 12, blend weights at
28, and UV at 44.

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
  MDLS0004: BYTE zero separator
  MDLS0001/0002/0003: name (NUL-terminated), often constraint JSON
```

The matrix is stored row-major with translation in its last row, which is
byte-identical to a GLM column-major matrix with translation in its last
column. Compose parent-first to obtain bone world transforms.

Bone names are often empty, which can hide a version mismatch. MDLS0004 moved
the name before the numeric fields; MDLS0001, MDLS0002, and MDLS0003 keep it
after the matrix.

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
    DWORD unknown
    DWORD frameBytes
    frames of 9 floats: translation vec3, rotation vec3, scale vec3
  version-specific zero footer
```

MDLA0001 uses a four-byte footer, MDLA0004 uses ten bytes, and MDLA0006 uses
35 bytes. The files usually contain `frameCount + 1` records per bone, with a
repeated final record for a loop. Animation layers are absolute poses, so
later additive layers are composed as deltas from their own first frames.
MDLA0006 also stores Z rotation with the opposite sign from MDLA0001/0004;
the loader normalizes it as frames are read.
