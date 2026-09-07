---
type: Bundle Index
title: linux-wallpaperengine Knowledge Base
description: Entry point for current status, renderer internals, investigations, and local debugging workflows.
resource: file:///home/admin/Projects/repos/linux-wallpaperengine/docs/wiki
tags: [linux-wallpaperengine, wiki, index]
timestamp: 2026-08-19T00:00:00-04:00
---


# linux-wallpaperengine Knowledge Base

This is the local source of truth for parity work on the fork. Open
`docs/wiki/` as the Obsidian vault or read the Markdown directly. Git history
is the session log; the wiki records current behavior and unresolved work.

## Which repo this describes

Two checkouts of the same project live on this machine and they are not
interchangeable:

| Path | What it is |
|---|---|
| `~/Projects/repos/linux-wallpaperengine` | **This fork** (`Haberno/linux-wallpaperengine`). Everything in this wiki describes it. |
| `~/repos/linux-wallpaperengine` | The original upstream author's tree, kept for reference only. |

Every `resource:` link, file path, and command on these pages resolves against
the **fork** path. If a path here does not resolve, check you are not sitting in
the upstream checkout before assuming the doc is wrong.

## Start here

- [Full corpus validation](../Full%20Corpus%20Validation.md) — latest complete
  installed-wallpaper sweep, per-item outcomes and failure follow-up.

- [Project checklist](../Organized%20all.md) — consolidated checkboxes for all
  project docs, reconciled against source on 2026-09-06; start here for what
  is done, open, in progress, or awaiting verification.

- [Current Status](status/Current%20Status.md) — concise dashboard and visual
  verification queue.
- [Known Issues](status/Known%20Issues.md) — active bugs, deferred features,
  fixed items, and project house rules.
- [TODO Backlog](status/TODO%20Backlog.md) — prioritized implementation and
  verification backlog.
- [Candidate Refactors](status/Candidate%20Refactors.md) — optional cleanup;
  not standing permission to change unrelated code.

Status wording is deliberate: **implemented** means the code/tests exist,
**verified** means the relevant live wallpaper was checked, **open** means the
problem remains, and **outdated/reverted** means a historical note must not be
treated as current behavior.

## Renderer systems

- [3D Scene Support](rendering/3D%20Scene%20Support.md) — camera, models,
  lighting, fog, sorting, skinning, attachments, and shadows.
- [MDL File Format](rendering/MDL%20File%20Format.md) — reverse-engineered MDLV,
  MDLS, MDAT, and MDLA layouts.
- [Puppet Warp Pipeline](rendering/Puppet%20Warp%20Pipeline.md) — 2D puppet
  skinning and effect composition.
- [Parallax System](rendering/Parallax%20System.md) — camera response, layer
  depth, and depth-map effects.
- [Camera Path Playback](rendering/Camera%20Path%20Playback.md) — curve and
  legacy shot parsing, per-frame Bezier evaluation, and loop/mirror/single
  playback recovered from the reference binary.
- [Shader Translation](rendering/Shader%20Translation.md) — Wallpaper Engine
  shader compatibility and setup failures.
- [Load Performance](rendering/Load%20Performance.md) — cold-start and
  live-switch cost, texture-cache sizing, and how to re-measure.
- [SceneScript Runtime](rendering/SceneScript%20Runtime.md) — property-script
  lowering, builtins-vs-module divergence, vector operand order, and the
  lifecycle traps that disable scripts silently.

## Reference and regression evidence

- [WE Reference Mining](reference/WE%20Reference%20Mining.md) — high-level
  findings from the locally installed Windows binary and stock assets.
- [Upstream Feature Inventory](reference/Upstream%20Feature%20Inventory.md) —
  every feature announced from launch to 2.8.42, grouped by subsystem, marked
  for whether it is renderer work here or permanently out of scope.
- [Wallpaper Case Studies](reference/Wallpaper%20Case%20Studies.md) — named
  wallpapers that exercise specific fixes.
- [Texture File Format](reference/Texture%20File%20Format.md) — TEX container,
  format, flag, mipmap, and animation notes.
- [OKF Format Source](references/okf-format-source.md) — convention source.

## Workflow and investigations

- [Debugging Workflow](workflow/Debugging%20Workflow.md) — build/test, object
  isolation, screenshots, extraction, fresh-process checks, Ghidra entry
  points, permanent env-gated debug hooks, and the corpus-wide validator.
- [Audio Capture Silence Investigation](investigations/Audio%20Capture%20Silence%20Investigation.md)
  — historical evidence from the now-resolved flat-capture symptom; retained
  in case it recurs.
- [Video Texture Memory Growth](investigations/Video%20Texture%20Memory%20Growth.md)
  — embedded-video wallpapers leak via an upstream libmpv GL fence bug fixed for
  mpv 0.42.0 according to the recorded investigation; includes the local patch
  recipe and the smaller residual traced to NVIDIA EGL event queues. Current
  installed-library/driver status still needs verification.

## Maintenance rules

- Update the [project checklist](../Organized%20all.md) and relevant
  status/concept pages when behavior changes; do not add a dated session log file.
- Mark uncertain visual results as pending verification instead of fixed.
- Merge a narrow historical note into the relevant concept/case/backlog page
  before removing it.
- Keep machine-specific commands in `.claude/lwe-terminal-runbook.md` and
  reusable Ghidra scripts in `.claude/tools/ghidra/`.
