# Debug hooks

Centralized home for permanently-shipped debug logic (`WallpaperEngine::Debug`).
Rules:

- every hook is env-gated and a strict no-op when its variable is unset
- no debug logic inlined elsewhere in the engine — add a helper here, call it,
  and record the hook in the ledger below

## Ledger

| Variable | What it does | Call sites |
|----------|--------------|------------|
| `WPE_DUMP_SHADERS=<dir>` | Writes every fully composed shader unit (including compat-cache hits) to `<dir>` as `<file>.<n>.vert\|frag` and logs each path. Batch-compile the dumps with glslang for offline triage. | `ShaderUnit::dumpFinalSource` |

Related but not env-gated: the `--render-debug` CLI modes (`pass-log`,
`object=<id>`, ...) — see the wiki Debugging Workflow page.
