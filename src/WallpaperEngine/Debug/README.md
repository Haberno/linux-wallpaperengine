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
| `WPE_JSON_TELEMETRY=1` | Records every JSON key name the parsers ask for via `JsonExtensions::require`/`optional`, then on each parsed document (`project`/`scene`/`effect`/`material`/`model`/particle files) reports every key nothing consumed — a frequency-rankable discovery signal for unimplemented fields. Matches by key name, not path; see the class doc comment for the raw-access allowlist and dynamic-key-container exclusions. Aggregated corpus-wide by `tools/validate-corpus.py --telemetry` into `report.json`'s `unknown_keys`. | `JsonExtensions::require`/`optional` (via `JsonTelemetry::recordAccess`), `*Parser::load`/`parseScene` (via `JsonTelemetry::scan`) |
| `WPE_HEALTH_REPORT=<path>` | Writes a machine-readable JSON health summary to `<path>` (`-` = stdout) on every process exit, via `atexit`. Report contract: `counters` (metric → count), `timing` (`frames`, `elapsed_seconds`, `avg_fps`, `worst_frame_ms`), `details` (metric → up to 10 unique samples, 300 chars each, UTF-8-safe escaping/truncation). Metrics so far: `log.error`, `log.exception`, `fatal.exception`, `run.start`, `startup.project_load` (ms), `startup.texture_prefetch` (texture count, read+decode ms, upload ms), `startup.scene_build` (ms), `startup.first_frame` (ms from process start to the first rendered frame — the whole load cost), `switch.prepare` (loader thread: project parse, read+decode, shared-context upload, texture count), `switch.apply` (render-thread stall, of which scene build, plus request-to-visible), `texture.sync_load` (one per texture the cache had to load on the calling thread — during a scene build that is the render thread, so a high count means the batch prefetch missed or trim() evicted them). Intended consumer: `tools/validate-corpus.py` (one background per process, aggregate the reports). | `Log::error`, `Log::exception`, `WallpaperApplication::render` (frame timing, `startup.first_frame`), `WallpaperApplication::WallpaperApplication` (`startup.project_load`), `WallpaperApplication::prepareOutputs` (`startup.texture_prefetch`, `startup.scene_build`), `WallpaperApplication::switchWorkerMain` (`switch.prepare`), `WallpaperApplication::applyPreparedSwitch` (`switch.apply`), `TextureCache::resolve` (`texture.sync_load`), `main` (`run.start`, `fatal.exception`) |
| `WPE_CONTROL_SOCKET=<path>` | Overrides the Unix control-socket path. Validators and transition reproductions use private paths so they do not replace the live desktop instance's socket. The socket's `memstats` command reports glibc heap, texture-cache (total/referenced), and live-FBO counts/bytes; `fbostats` reports the largest live render-target name/dimension groups. | `WallpaperApplication::setupControlSocket`, `tools/validate-corpus.py` |

Related but not env-gated: the `--render-debug` CLI modes (`pass-log`,
`object=<id>`, ...) — see the wiki Debugging Workflow page.

Wallpaper-specific crash, switch and memory findings from the July 2026 corpus
run are recorded in [WALLPAPER_FINDINGS.md](WALLPAPER_FINDINGS.md).
