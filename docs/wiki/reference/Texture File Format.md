---
type: Format Reference
title: Texture File Format
description: Current TEXV/TEXI/TEXB texture-container notes and renderer support.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine/src/WallpaperEngine/Data/Parsers/TextureParser.cpp
tags: [linux-wallpaperengine, texture, binary-format, reverse-engineering]
timestamp: 2026-09-07T00:00:00-04:00
---

# Texture File Format

Wallpaper Engine's resource compiler stores images in a custom `.tex`
container. `TextureParser` supports `TEXV0005`/`TEXI0001`, container versions
`TEXB0001` through `TEXB0004`, and animation tables `TEXS0001` through
`TEXS0003`.

## Main header

| field | bytes | current interpretation |
|---|---:|---|
| magic | 9 | `TEXV0005` plus NUL |
| info magic | 9 | `TEXI0001` plus NUL |
| format | 4 | value from the table below |
| flags | 4 | bitmask from the flags table |
| texture width/height | 8 | allocated/padded dimensions |
| image width/height | 8 | authored dimensions |
| unknown | 4 | retained as unknown |
| container magic | 9 | `TEXB0001`…`TEXB0004` plus NUL |
| image count | 4 | number of image/mipmap groups |

## Formats

| value | format |
|---:|---|
| 0 | ARGB8888 |
| 1 | RGB888 |
| 2 | RGB565 |
| 4 | DXT5/BC3 |
| 6 | DXT3/BC2 |
| 7 | DXT1/BC1 |
| 8 | RG88 |
| 9 | R8 |
| 10 | RG16F |
| 11 | R16F |
| 12 | BC7 |
| 13 | RGB10A2 |
| 14 | RGBA16F |
| 15 | RGB16F |

The old documentation incorrectly described bit 0 as interpolation. It means
**no interpolation** in the current parser/render path.

| flag | meaning |
|---:|---|
| `0x1` | no interpolation |
| `0x2` | clamp UVs |
| `0x4` | animated/GIF-style frame table follows |
| `0x8` | clamp UVs to border |
| `0x20` | video-backed texture |
| `0x80000` | alpha-channel priority for R/RG formats |
| `0x100000`…`0x800000` | authored R/G/B/A component-presence bits |

Unknown flag bits are preserved instead of rejecting an otherwise valid
asset. Component-presence bits now feed material shader combos for packed
metallic, roughness, reflection, and emissive inputs.

## Container and mipmaps

`TEXB0003` stores a FreeImage format after the image count. For still textures,
`TEXB0004` adds a conditional-descriptor count, followed by that many records:
`group u32`, `id u32`, `flags u32`, then NUL-terminated JSON. Its `condition`
is either a boolean property name or `{"name": property, "condition": value}`.
The first matching descriptor in each group wins; no match retains the base.

Each image begins with a mip count. A normal mip entry contains width, height,
compression flag, unpacked size, stored size, then payload. Compression value 1
is LZ4. When conditional descriptors exist, **each mip's base payload is followed
by patch groups**, before the next mip header:

1. Group count (`u32`).
2. For each group: patch count (`u32`).
3. For each patch: record tag (observed `1`), variant id, x, y, width, height,
   FreeImage format, stored byte count (all `u32`), then the bytes.

Patch compression follows the base mip. Partial BC patches copy aligned block
rows; raw component textures copy component rows; PNG patches modify decoded
RGBA pixels. The renderer retains packed source bytes only for conditional
textures, binds them to each scene's property objects, and reuploads when the
selected ids change. Video-flagged textures keep their special embedded-payload
path. Version-4 still mipmaps do **not** contain the formerly documented JSON prefix.

This layout was checked against native readers `14015c8d0`/`14015c480` and all
41 authored variants across 412 TEXB4 textures/486 mip levels in the five named
wallpapers. GPU minification and live property switching passed; see
[Asset Texture Verification](../../Asset%20Texture%20Verification.md).

Stock compiled preview textures are fallback-only texture lookups. Missing
compiled textures can use PNG + `.tex-json` sources with supported RGBA8/RG8/R8
channel layouts, `nomip`, `clampuvs`, and `nointerpolation`. Preview scene/project/
material JSON is never mounted into the root namespace.

The renderer validates payload size before upload and distinguishes actual
BC-compressed blocks from malformed/uncompressed payloads carrying a DXT
format tag. Texture cache keys are project-scoped so identically named assets
from different wallpapers cannot alias after a live switch.

Decode is the dominant load cost: ~92 % of texture load time is `stb_image`,
which is why `TextureParser::decodeMipmaps` batches across a thread pool. A
single 4K wallpaper occupies ~576 MB of texture cache, which constrains how the
cache budget must be sized for live switches → [[Load Performance]].

## Animation data

When flag `0x4` is set, `TEXS0001`, `TEXS0002`, or `TEXS0003` follows the
image data. Entries map a frame number to duration and a rectangle inside the
texture. Version 3 stores explicit animation dimensions; older versions infer
them. The parser also derives a spritesheet grid when the rectangles form a
valid grid capable of holding every frame.
