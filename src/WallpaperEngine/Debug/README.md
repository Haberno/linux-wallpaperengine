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
| `WPE_HEALTH_REPORT=<path>` | Writes a machine-readable JSON health summary to `<path>` (`-` = stdout) on every process exit, via `atexit`. Report contract: `counters` (metric → count), `timing` (`frames`, `elapsed_seconds`, `avg_fps`, `worst_frame_ms`), `details` (metric → up to 10 unique samples, 300 chars each). Metrics so far: `log.error`, `log.exception`. Intended consumer: the batch validator (one background per process, aggregate the reports). | `Log::error`, `Log::exception`, `WallpaperApplication::render` (frame timing) |

Related but not env-gated: the `--render-debug` CLI modes (`pass-log`,
`object=<id>`, ...) — see the wiki Debugging Workflow page.
