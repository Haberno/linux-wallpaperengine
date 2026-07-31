# Reverse Engineering Tools
These are the tools and binaries that I use for understanding what's going behind the scenes:

## d3dcompiler_47
A small shim to debug interactions with the d3dcompiler.
It's used to add extra information to wallpaper's themselves, like include processed shader before It's compiled
by d3d. Especially useful when something shader-related doesn't work so It can be inspected either manually
(with the logfile it creates) or in RenderDoc with shader names, source...

For it to work it has to live alongside the real d3dcompiler_74 dll renamed to d3dcompiler_47original.dll

## RenderDoc.cap
A base capture file that allows for Wallpaper Engine to run backgrounds, when used in conjunction with the
d3dcompiler_47 shim allows for taking captures of rendering, inspecting all the rendering performed by Wallpaper Engine.

## inspect-mdl-clipping.py

Read-only inspector for the optional auxiliary/draw-range tail added in
MDLV0021 and the clipping descriptors added in MDLV0023:

```sh
python3 tools/reversing/inspect-mdl-clipping.py path/to/model_puppet.mdl
```

It prints byte offsets, auxiliary payload dimensions, 16-byte draw-range
records, both descriptor range-index lists, and the next section marker. It
also rejects malformed lengths and out-of-range descriptor references.

## Missing effect behavior

The `wallpaper64.exe` string `Failed loading effect: %s` is referenced by
`FUN_1401e7170`. That function attempts to load one effect definition; when the
load fails it only logs the message and does not append an effect object.
Its caller, `FUN_1401e6f50`, iterates the complete authored `effects` array and
continues with the next entry after every call. This is the reference for
`ObjectParser::parseEffects`: skip only the failed effect, preserving the base
layer and valid effects before and after it.
