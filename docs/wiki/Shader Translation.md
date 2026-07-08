---
type: Rendering Concept
title: Shader Translation
description: Shader preprocessing and compatibility fixes for Wallpaper Engine HLSL-flavored GLSL.
resource: file:///home/admin/repos/linux-wallpaperengine/src/WallpaperEngine/Render/Shaders/ShaderUnit.cpp
tags: [linux-wallpaperengine, shaders, glsl, spirv-cross]
timestamp: 2026-07-06T20:00:00-04:00
---

# Shader Translation

Wallpaper Engine shaders are HLSL-flavored GLSL. The engine preprocesses them
(`ShaderUnit.cpp`), then round-trips glslang → SPIR-V → spirv-cross
(`GLSLContext::toGlsl`) to produce GLSL 330.

## v_TexCoord width mismatches

Stock vertex shaders declare `varying vec4 v_TexCoord`; many fragment shaders
declare `vec2`. glslang links the pair by **widening the fragment input to
vec4**, so any bare use of `v_TexCoord` where a vec2 is expected becomes an
illegal vec4→vec2 implicit truncation (HLSL allows it, GLSL doesn't).

`ShaderUnit::applyFragmentTexCoordCompatibility` handles two cases:

1. **Fragment declares vec2 but the linked vertex declares vec4** (workshop
   audio-visualizer in Gojo 3100265648): shield all declaration sites, replace
   every bare `v_TexCoord` (negative lookahead for `.`) with `v_TexCoord.xy`,
   widen the vec2 declaration to vec4, unshield.
   - The declaration shielding matters: `genericparticle.frag` declares BOTH
     widths in different `#if` branches; rewriting uses without shielding
     corrupts the vec4 declaration into invalid syntax.
2. **Fragment itself declares vec3/vec4** (legacy path): qualify uses adjacent
   to `CAST2()` and `vec2 x = v_TexCoord;` assignments with `.xy`.

## Symptoms map

| error | cause |
|---|---|
| `implicit cast from "vec4" to "vec2"` (C7011) | width mismatch above |
| `syntax error, unexpected DOT` | rewrite corrupted a declaration (shield first) |
| `must write to gl_Position` | vertex unit failed to parse → empty source compiled |

Object setup failures log `Failed to setup object <id>: ...` and the object
silently doesn't render — an "invisible layer" is often a shader translation
failure, not a positioning bug. Check the log before debugging geometry.
