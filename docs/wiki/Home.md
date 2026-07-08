---
type: Bundle Overview
title: linux-wallpaperengine Wiki Home
description: Original Obsidian landing page for the rendering work on this fork.
resource: file:///home/admin/repos/linux-wallpaperengine/docs/wiki
tags: [linux-wallpaperengine, wiki, rendering]
timestamp: 2026-07-06T20:00:00-04:00
---

# linux-wallpaperengine Wiki

Obsidian-style knowledge base for the rendering work on this fork. Open this
folder (`docs/wiki/`) as an Obsidian vault, or read the notes as plain markdown.

## Core systems
- [[Puppet Warp Pipeline]] — how MDL puppet meshes are skinned and rendered
- [[MDL File Format]] — the reverse-engineered binary format (MDLV/MDLS/MDLA)
- [[Parallax System]] — hierarchical depth resolution and conventions
- [[Shader Translation]] — HLSL→GLSL quirks and the v_TexCoord widening fix

## Reference
- [[Wallpaper Case Studies]] — the workshop wallpapers that drove each fix
- [[Debugging Workflow]] — how to isolate objects, screenshot, and verify
- [[Known Issues]] — open bugs and deferred work
- [[Candidate Refactors]] — cleanup/refactor backlog from the user

## Session history
- 2026-07-06: puppet warp rewrite (skeleton + animation playback), MDLV0017
  support, multi-layer additive animations, z-flatten, cull fix, shader
  widening fix, parallax overhaul. See git log for the commits.
