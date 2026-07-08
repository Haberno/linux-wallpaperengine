# linux-wallpaperengine Claude Context

Use this file when Claude Code is started from `~/repos/linux-wallpaperengine`.

## Scope

This session is for the linux-wallpaperengine fork. Do not use unrelated workstation, HyprGlass, Noctalia, or Hermes context unless the user explicitly asks or the task directly involves desktop/runtime integration.

For project knowledge, read the local OKF wiki first:

1. `docs/wiki/index.md`
2. Follow only the linked concept files relevant to the current task.
3. For refactor cleanup, start with `docs/wiki/Candidate Refactors.md`.

## Working Rules

- Preserve existing local edits. This repo often has a dirty worktree.
- Before code edits, inspect the target files and `git status --short`.
- Keep changes scoped to the requested bug, feature, or refactor.
- Do not perform broad cleanup just because it is listed in the backlog.
- Do not commit unless the user asks.

## Asset Extraction

To inspect a workshop wallpaper's raw shaders/materials/models, use
`repkg extract <workshop-dir>/scene.pkg -o <output-dir>` (installed at
`~/.local/bin/repkg`). See `docs/wiki/Debugging Workflow.md` for details and
the manual-parsing fallback.

## Rendering Knowledge Entry Points

- Puppet/MDL work: `docs/wiki/Puppet Warp Pipeline.md` and `docs/wiki/MDL File Format.md`.
- Shader failures or invisible layers: `docs/wiki/Shader Translation.md`.
- Parallax behavior: `docs/wiki/Parallax System.md`.
- Regression examples: `docs/wiki/Wallpaper Case Studies.md`.
- Open issues and house rules: `docs/wiki/Known Issues.md`.
- Debug commands and verification: `docs/wiki/Debugging Workflow.md`.

## Verification Rules

- Do not use live-output grim/PNG diffs as final rendering proof.
- Engine `--screenshot` on isolated debug windows is acceptable for debugging.
- For live wallpaper appearance, ask the user to inspect the result.
- Watch for stale `(deleted)` `linux-wallpaperengine` processes before trusting visuals.

## Current Refactor Backlog

The user-supplied cleanup list lives in `docs/wiki/Candidate Refactors.md`.

Treat it as a backlog, not standing permission to edit all listed areas.
