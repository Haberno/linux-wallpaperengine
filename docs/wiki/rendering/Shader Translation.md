---
type: Rendering Concept
title: Shader Translation
description: Shader preprocessing and compatibility fixes for Wallpaper Engine HLSL-flavored GLSL.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine/src/WallpaperEngine/Render/Shaders/ShaderUnit.cpp
tags: [linux-wallpaperengine, shaders, glsl, spirv-cross]
timestamp: 2026-08-19T00:00:00-04:00
---

# Shader Translation

Wallpaper Engine shaders are HLSL-flavored GLSL. The engine preprocesses them
(`ShaderUnit.cpp`), then round-trips glslang → SPIR-V → spirv-cross
(`GLSLContext::toGlsl`) to produce GLSL 330.

## Macros the real engine injects but never ships

Some macros are used by Wallpaper Engine's own shipped headers yet are defined
**nowhere in the assets tree** — `wallpaper64.exe` injects them at translation
time. Any of them missing from our prelude fails the unit outright, and because
a failed vertex unit compiles as empty source, the reported error is the
downstream `must write to gl_Position`, not the real cause.

Currently injected (`ShaderUnit.cpp:57`):

```glsl
#define DECLARE_SAMPLER2D_PARAMETER(name) in sampler2D name
#define MAKE_SAMPLER2D_ARGUMENT(name)     name
```

These are the HLSL/GLSL cross-compat pair for passing a sampler into a
function: the declaration form at the parameter list, the call-site form at
the argument. `base/model_fragment_v1.h` and `base/model_vertex_v1.h` both use
them, so **every** shader reaching `ApplyReflection` or `ApplyMorphPosition*`
failed to compile until they were added (2026-08-18, `33ece832`). That is a
large fraction of 3D model shaders, so any corpus numbers measured before that
commit understate what renders.

When a stock header references an identifier that exists in no asset file,
suspect this class of injected macro before assuming the header is corrupt.

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

### Combo-conditional width variants must survive

A later correction (2026-07-31, `e76307ec`): when **both** units declare vec2
and vec4 variants behind matching combo branches, the widening above must not
run. Blindly widening the fragment's inactive vec2 declaration makes its active
interface disagree with the vertex shader whenever the combo selects the narrow
path — seen on Fragments' generic shader. Preserve the branches instead.

## Include handling

Two ordering rules, both learned from real breakage (2026-07-31):

- **Macros stay at the authored `#include` location; declarations and helper
  functions stay in `m_includes`** (`1be9f6bf`). Splitting them the other way
  hides uniforms the shader itself declares from the helpers that use them.
- **Relocated include macros must remain visible** (`8109c6c8`) after the move,
  or units that expanded correctly before the split stop resolving.

`ShaderIncludes.cpp` preserves line breaks in the late header body so
`WPE_DUMP_SHADERS` output still lines up with reported error line numbers.

### Include and combo input validation

As of 2026-09-08, include scanning recognizes actual directives outside line
and block comments, including `# include`. Quoted filenames must open and close
on the same line and be nonempty; malformed root or nested directives produce
`Malformed #include` with the shader name and line. Expansion preserves trailing
comments, including block comments that continue on later lines. Existing
permissive handling of unavailable stock headers remains unchanged.

Combo metadata with a missing, non-string, empty, or invalid identifier is
ignored with a diagnostic. Invalid names from material overrides and linked
units also cannot emit malformed `#define` lines. An otherwise valid combo with
no default still receives 0. Solid-color sampler defaults and the first-`#if`
include-placement question remain open.

Verification: 953 assertions across 118 C++ cases pass, including synthetic
malformed-input cases and successful compilation through glslang/SPIR-V.
A short three-wallpaper sweep took 18 seconds; all three rendered 45 frames and
exited normally. Error counters and shader failure lists matched the previous
full corpus exactly. The validator still reports WARN for Soulless and Mario,
and FAIL for Moon Lady's eight existing standalone shader failures; these are
not newly cleared visual-parity bugs.

Manual regression checks (exact installed titles):

| Workshop ID | Wallpaper | Check |
|---|---|---|
| 2639381674 | Soulless 4k {Artwork by Ilona Mencner} | Text and effects remain visible; switching away and back keeps animating. |
| 2924081598 | Super Mario Voxel | The 3D scene, textures, and lighting stay visible without new white/black surfaces. |
| 3107568889 | Moon Lady 4K [OC] [space sci-fi] [AI] | Existing cloud/fluid effects still animate; fullscreen or monitor sleep/wake resumes rendering. |

These wallpapers exercise ordinary rendering after the fixes. Malformed-input
failures are reproduced by synthetic tests, not by claiming these wallpapers
contain malformed includes or empty combo metadata. Temporary reports are in
`/tmp/lwe-checklist-20260908/`.

## Float arguments passed to int parameters

HLSL implicitly truncates float expressions passed to int-typed function
parameters; GLSL overload resolution has no float→int conversion, so glslang
fails with `no matching overloaded function found` (workshop
`multistage_wave.frag` in 3761619125: `calWaveData` takes an `int` wave-mask
that call sites feed from a float).

`ShaderUnit::applyIntParameterCallCompatibility` fixes this generically:

1. Scan user-defined function definitions (both `#if` branches of combo-guarded
   definitions are still present, so every signature variant contributes) and
   record int-typed parameter positions.
2. At each call site, wrap the argument at those positions in an explicit
   `int(...)` constructor — reproducing HLSL truncation. `int(int)` is valid
   GLSL, so over-wrapping already-integer arguments is harmless.
3. Definitions (`{` after the parameter list), prototypes (arguments that parse
   as parameter declarations), integer literals, and already-cast arguments are
   skipped.

## Symptoms map

| error | cause |
|---|---|
| `implicit cast from "vec4" to "vec2"` (C7011) | width mismatch above |
| `syntax error, unexpected DOT` | rewrite corrupted a declaration (shield first) |
| `must write to gl_Position` (C5145) | vertex unit failed to parse → empty source compiled. **Cascading symptom, never the cause** — read the *first* error in the unit. Most often a missing injected macro (above); otherwise a parse failure |
| `no matching overloaded function found` | float passed to int parameter (see above) |

## Translation cost

Measured 2026-07-26: translating a wallpaper's shaders costs **239 ms on average**
(median 229 ms, worst 925 ms) — roughly 175 ms in the compatibility passes above
and 52 ms in the glslang → SPIR-V → spirv-cross round trip. Per-pass
preprocessing averages 1.4 ms but reaches 424 ms on the 2555-pass wallpaper.

There is no persistent cache across process restarts. Source checked
2026-09-06: `ShaderUnit` already memoizes includes/preprocessing and
compatibility output, while `GLSLContext::toGlsl` caches linked translations
in memory, so repeated switches can reuse those results. The outstanding
work is a content-addressed disk cache; fixed regex patterns are already
`static`. → [[Load Performance]].

## Debugging: dumping composed sources

Set `WPE_DUMP_SHADERS=<dir>` to write every fully composed unit (post-header,
post-compat-passes) to `<dir>/<path-with-underscores>.<N>.{vert,frag}`.
`ShaderUnit::dumpFinalSource` runs on **both** the slow path and the compat
cache hit path — cache hits still produce distinct units (different combos in
the header), so without this every variant after the first would be invisible
when diagnosing per-combo compile failures.

Object setup failures log `Failed to setup object <id>: ...` and the object
silently doesn't render — an "invisible layer" is often a shader translation
failure, not a positioning bug. Check the log before debugging geometry.
