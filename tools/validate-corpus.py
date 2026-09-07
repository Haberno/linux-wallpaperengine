#!/usr/bin/env python3
"""Corpus-wide parse/shader validator for linux-wallpaperengine.

Walks a directory of workshop items and, for each one:
  1. runs it in an isolated, windowed, muted engine process for a few seconds
     after scene loading completes (with a separate startup timeout), using
     WPE_HEALTH_REPORT (JSON exit summary) and WPE_DUMP_SHADERS enabled,
  2. stops it with SIGINT (a clean stop must exit 0 and still write the report),
  3. batch-compiles every dumped shader unit with glslang,
  4. classifies the run as PASS / WARN / FAIL.

Emits <out>/report.json plus a console summary, and exits non-zero if any
item failed.  Per-item artifacts (log, health report, failing shader dumps)
are kept under <out>/<id>/ for triage; shader dumps of passing items are
deleted unless --keep-all is given.

Fast full-corpus smoke test: --jobs 2 --shader-jobs 6 --duration 2
Identical shader sources are checked once per stage across the entire run.
Requires: python3 (stdlib only), glslang, a running display session.
"""

import argparse
import concurrent.futures
import hashlib
import json
import math
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

DEFAULT_CORPORA = [
    "~/.local/share/Steam/steamapps/workshop/content/431960",
    "~/.steam/steam/steamapps/workshop/content/431960",
]
SHADER_EXTS = {".vert", ".frag"}
# renderable standalone; workshop assets (effects, models, visualizers) are not
WALLPAPER_TYPES = {"scene", "video", "web"}
DETAIL_CAP = 2000  # chars of glslang output kept per failing shader
# This machine's Wayland/GLFW stack logs these through sLog.error on every run,
# regardless of the wallpaper. Counting them made every clean item a WARN.
BENIGN_ERRORS = (
    "Failed to initialize GLEW: No GLX display",
    "GLFW error 65548",
)
# detail line of a WPE_JSON_TELEMETRY report: "    general.bloomhdrfeather (x1)"
TELEMETRY_KEY = re.compile (r"^\s+(\S+) \(x(\d+)\)$")
TELEMETRY_TOP = 25  # unconsumed keys printed in the console summary


def find_corpus (explicit: str | None) -> Path:
    if explicit:
        path = Path (explicit).expanduser ()
        if not path.is_dir ():
            sys.exit (f"error: corpus directory does not exist: {path}")
        return path
    for candidate in DEFAULT_CORPORA:
        path = Path (candidate).expanduser ()
        if path.is_dir ():
            return path
    sys.exit ("error: no workshop corpus found; pass one explicitly")


def project_info (item: Path) -> tuple [str, str, str]:
    """Best-effort (title, type, scene file) from project.json."""
    try:
        with open (item / "project.json", encoding = "utf-8", errors = "replace") as fp:
            project = json.load (fp)
        return (str (project.get ("title", "?")), str (project.get ("type", "?")).lower (),
                str (project.get ("file", "scene.json")))
    except (OSError, json.JSONDecodeError, ValueError, AttributeError):
        return "?", "?", "scene.json"


def pkg_entry (pkg: Path, wanted: str) -> bytes | None:
    """Read one file out of a .pkg container.

    Layout: [u32 len][version][u32 count]{[u32 len][path][u32 offset][u32 size]}* then the
    data blob, entry offsets being relative to the start of that blob.
    """
    def u32 (fp) -> int:
        return int.from_bytes (fp.read (4), "little")

    try:
        with open (pkg, "rb") as fp:
            fp.seek (u32 (fp), 1)  # version string
            count = u32 (fp)
            if count > 1 << 20:  # not a pkg (or truncated); do not spin on garbage
                return None
            entries = {}
            for _ in range (count):
                path = fp.read (u32 (fp)).decode ("utf-8", errors = "replace")
                entries [path] = (u32 (fp), u32 (fp))
            if wanted not in entries:
                return None
            offset, size = entries [wanted]
            fp.seek (offset, 1)
            return fp.read (size)
    except (OSError, ValueError):
        return None


def scene_projection (item: Path, scene_file: str) -> str:
    """'3d' (perspective), '2d' (orthogonal), or '?' when the scene file is unreadable.

    @param scene_file project.json's "file" - usually scene.json, but a handful of items
                      ship gifscene.json/audiophile.json, loose or in a like-named .pkg.
    """
    raw = None
    try:
        loose = item / scene_file
        if loose.is_file ():
            raw = loose.read_bytes ()
        else:
            for pkg in sorted (item.glob ("*.pkg")):
                raw = pkg_entry (pkg, scene_file)
                if raw is not None:
                    break
    except OSError:
        raw = None
    if raw is None:
        return "?"

    try:
        scene = json.loads (raw.decode ("utf-8", errors = "replace").lstrip ("﻿"))
        # WE emits an orthogonalprojection block for 2D scenes and omits it for perspective ones
        return "2d" if scene ["general"].get ("orthogonalprojection") is not None else "3d"
    except (json.JSONDecodeError, ValueError, AttributeError, TypeError, KeyError):
        return "?"


def parse_telemetry (log: Path) -> dict [str, int]:
    """Unconsumed JSON key paths reported by WPE_JSON_TELEMETRY: path -> occurrences."""
    keys: dict [str, int] = {}
    try:
        text = log.read_text (encoding = "utf-8", errors = "replace")
    except OSError:
        return keys
    for line in text.splitlines ():
        match = TELEMETRY_KEY.match (line)
        if match:
            keys [match.group (1)] = keys.get (match.group (1), 0) + int (match.group (2))
    return keys


def run_engine (exe: Path, item: Path, outdir: Path, args, cancelled: threading.Event) -> dict:
    """Run one background in its own process; return raw run facts."""
    shaders = outdir / "shaders"
    # A reused output directory must not contribute stale shader failures.
    if shaders.exists ():
        shutil.rmtree (shaders)
    shaders.mkdir (parents = True, exist_ok = True)
    health_path = outdir / "health.json"
    health_path.unlink (missing_ok = True)

    env = dict (os.environ)
    env ["WPE_HEALTH_REPORT"] = str (health_path)
    env ["WPE_DUMP_SHADERS"] = str (shaders)
    # A validation process must not replace the live desktop engine's socket.
    socket_path = Path (f"/tmp/lwe-validator-{os.getpid ()}-{hashlib.sha256 (str (item).encode ()).hexdigest () [:16]}.sock")
    socket_path.unlink (missing_ok = True)
    env ["WPE_CONTROL_SOCKET"] = str (socket_path)
    env ["WPE_LOG_FILE"] = "off"
    if args.telemetry:
        env ["WPE_JSON_TELEMETRY"] = "1"

    # Pass the discovered item path, not just its directory name. The latter
    # silently redirected explicit/custom corpora back to Steam's Workshop
    # directory whenever the item happened to use a numeric name.
    cmd = [str (exe), "--window", args.window, "--silent", "--no-fullscreen-pause",
           "--fps", str (args.fps), str (item)]
    facts = {"cmd": " ".join (cmd), "hung": False, "early_exit": False,
             "startup_timeout": False, "startup_seconds": None}
    started = time.monotonic ()

    with open (outdir / "log.txt", "wb") as log:
        proc = subprocess.Popen (cmd, stdout = log, stderr = subprocess.STDOUT, env = env,
                                 start_new_session = True)
        try:
            ready_at = None
            while proc.poll () is None and not cancelled.is_set ():
                now = time.monotonic ()
                # setupControlSocket runs after prepareOutputs builds the whole scene.
                # Startup may take much longer than a short render smoke test.
                if ready_at is None and socket_path.exists ():
                    ready_at = now
                    facts ["startup_seconds"] = round (now - started, 3)
                if ready_at is not None and now - ready_at >= args.duration:
                    break
                if ready_at is None and now - started >= args.startup_timeout:
                    facts ["startup_timeout"] = True
                    break
                cancelled.wait (0.05)
            facts ["early_exit"] = proc.poll () is not None
        finally:
            if proc.poll () is None:
                try:
                    proc.send_signal (signal.SIGINT)
                except ProcessLookupError:
                    pass
            try:
                facts ["exit_code"] = proc.wait (timeout = args.grace)
            except subprocess.TimeoutExpired:
                # Also reap stuck CEF/video helpers belonging to this test process.
                try:
                    os.killpg (proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                facts ["exit_code"] = proc.wait ()
                facts ["hung"] = True
            socket_path.unlink (missing_ok = True)
    facts ["seconds"] = round (time.monotonic () - started, 3)

    try:
        # decode with replacement so a mangled report degrades to FAIL/WARN data
        # for one item instead of aborting the whole corpus run
        facts ["health"] = json.loads (health_path.read_bytes ().decode ("utf-8", errors = "replace"))
    except (OSError, json.JSONDecodeError):
        facts ["health"] = None

    # health.json only carries the log.error count (details are capped), so the
    # baseline lines have to be counted from the log to be subtracted from it
    try:
        log_text = (outdir / "log.txt").read_bytes ().decode ("utf-8", errors = "replace")
    except OSError:
        log_text = ""
    facts ["benign_errors"] = sum (log_text.count (pattern) for pattern in BENIGN_ERRORS)

    return facts


class ShaderCompiler:
    """Bounded, shared stage/source cache; every dumped unit retains its own result."""

    def __init__ (self, glslang: str, jobs: int):
        self.glslang = glslang
        self.pool = concurrent.futures.ThreadPoolExecutor (max_workers = jobs)
        self.lock = threading.Lock ()
        self.cache = {}
        self.cache_hits = 0

    def _compile (self, unit: Path) -> str | None:
        try:
            result = subprocess.run ([self.glslang, str (unit)], capture_output = True,
                                     text = True, errors = "replace", timeout = 30)
            if result.returncode != 0:
                return (result.stdout + result.stderr).replace (str (unit), "<shader>").strip () [:DETAIL_CAP]
        except (OSError, subprocess.TimeoutExpired) as error:
            return f"glslang did not run: {error}"
        return None

    def compile (self, shaderdir: Path) -> tuple [int, list [dict]]:
        pending = []
        for unit in sorted (shaderdir.iterdir ()):
            if unit.suffix not in SHADER_EXTS:
                continue
            # Stage is part of the key: the same text may be valid as one stage only.
            key = (unit.suffix, hashlib.sha256 (unit.read_bytes ()).digest ())
            with self.lock:
                future = self.cache.get (key)
                if future is None:
                    future = self.pool.submit (self._compile, unit)
                    self.cache [key] = future
                else:
                    self.cache_hits += 1
            pending.append ((unit.name, future))
        failures = []
        for name, future in pending:
            output = future.result ()
            if output is not None:
                failures.append ({"file": name, "output": output})
        return len (pending), failures

    def close (self):
        self.pool.shutdown (wait = True)


def validate_item (exe: Path, item: Path, out: Path, args, compiler: ShaderCompiler,
                   cancelled: threading.Event, info: tuple, projection: str) -> dict:
    title, wp_type, _ = info
    entry = {"id": item.name, "title": title, "type": wp_type, "projection": projection}
    if wp_type not in WALLPAPER_TYPES and not args.include_assets:
        return dict (entry, status = "SKIP",
                     reasons = ["no wallpaper type in project.json (workshop asset/effect)"])
    if cancelled.is_set ():
        return dict (entry, status = "SKIP", reasons = ["validation interrupted"])
    outdir = out / item.name
    try:
        facts = run_engine (exe, item, outdir, args, cancelled)
        shader_total, shader_failures = compiler.compile (outdir / "shaders")
        status, reasons = classify (facts, shader_total, shader_failures)
    except Exception as error:  # one bad item must never abort the corpus
        facts = {"exit_code": None, "health": None}
        shader_total, shader_failures = 0, []
        status, reasons = "FAIL", [f"validator error: {error!r}"]
    if status == "PASS" and not args.keep_all:
        shutil.rmtree (outdir / "shaders", ignore_errors = True)
    unknown = parse_telemetry (outdir / "log.txt") if args.telemetry else {}
    timing = (facts ["health"] or {}).get ("timing", {})
    return dict (entry, status = status, reasons = reasons, exit_code = facts ["exit_code"],
                 frames = timing.get ("frames"), avg_fps = timing.get ("avg_fps"),
                 worst_frame_ms = timing.get ("worst_frame_ms"),
                 startup_seconds = facts.get ("startup_seconds"), seconds = facts.get ("seconds"),
                 shaders_total = shader_total, shaders_failed = len (shader_failures),
                 shader_failures = shader_failures, unknown_keys = len (unknown),
                 telemetry = unknown, counters = (facts ["health"] or {}).get ("counters", {}))


def classify (facts: dict, shader_total: int, shader_failures: list [dict]) -> tuple [str, list [str]]:
    reasons = []
    health = facts ["health"]
    counters = (health or {}).get ("counters", {})
    frames = int ((health or {}).get ("timing", {}).get ("frames", 0))

    if facts.get ("startup_timeout"):
        reasons.append ("startup timed out before scene was ready")
    if facts ["hung"]:
        reasons.append (f"engine ignored SIGINT and was killed")
    if facts ["early_exit"]:
        reasons.append (f"engine exited early (code {facts ['exit_code']})")
    elif not facts ["hung"] and facts ["exit_code"] != 0:
        reasons.append (f"unclean shutdown (code {facts ['exit_code']})")
    if health is None:
        reasons.append ("no health report produced (hard crash?)")
    else:
        if counters.get ("fatal.exception", 0) > 0:
            reasons.append (f"fatal.exception x{counters ['fatal.exception']}")
        if frames == 0:
            reasons.append ("no frames rendered")
    if shader_failures:
        reasons.append (f"{len (shader_failures)}/{shader_total} shader units failed glslang")

    if reasons:
        return "FAIL", reasons

    errors = counters.get ("log.error", 0) - facts.get ("benign_errors", 0)
    if errors > 0:
        reasons.append (f"log.error x{errors} (see details in health.json)")
        return "WARN", reasons
    return "PASS", reasons


def main () -> int:
    parser = argparse.ArgumentParser (description = __doc__,
                                      formatter_class = argparse.RawDescriptionHelpFormatter)
    parser.add_argument ("corpus", nargs = "?", help = "workshop content directory "
                         "(default: autodetect the Steam workshop folder)")
    parser.add_argument ("--exe", default = "build/output/linux-wallpaperengine",
                         help = "engine executable (default: %(default)s)")
    parser.add_argument ("--out", default = "validation-output",
                         help = "artifact/report directory (default: %(default)s)")
    parser.add_argument ("--ids", nargs = "*", help = "only validate these workshop ids")
    parser.add_argument ("--projection", choices = ("any", "2d", "3d"), default = "any",
                         help = "only validate scenes with this projection; 3d selects the "
                         "perspective (model) corpus (default: %(default)s)")
    parser.add_argument ("--telemetry", action = "store_true",
                         help = "run with WPE_JSON_TELEMETRY and rank the JSON keys no parser "
                         "consumed, per item and corpus-wide, in report.json")
    parser.add_argument ("--duration", type = float, default = 8.0,
                         help = "seconds to render AFTER scene loading (default: %(default)s)")
    parser.add_argument ("--startup-timeout", type = float, default = 90.0,
                         help = "maximum scene-loading seconds (default: %(default)s)")
    parser.add_argument ("--jobs", type = int, default = 1,
                         help = "concurrent wallpaper processes; increase with care for RAM/VRAM (default: %(default)s)")
    parser.add_argument ("--shader-jobs", type = int, default = min (8, os.cpu_count () or 1),
                         help = "parallel glslang workers, shared by all wallpapers (default: %(default)s)")
    parser.add_argument ("--grace", type = float, default = 10.0,
                         help = "seconds to wait after SIGINT before SIGKILL (default: %(default)s)")
    parser.add_argument ("--window", default = "0x0x640x360",
                         help = "window geometry XxYxWxH (default: %(default)s)")
    parser.add_argument ("--fps", type = int, default = 15,
                         help = "fps cap for validation runs (default: %(default)s)")
    parser.add_argument ("--glslang", default = "glslang", help = "glslang binary to use")
    parser.add_argument ("--keep-all", action = "store_true",
                         help = "keep shader dumps of passing items too")
    parser.add_argument ("--include-assets", action = "store_true",
                         help = "also run items without a wallpaper type in project.json "
                         "(workshop assets: effects, models, audio visualizers)")
    args = parser.parse_args ()
    for name in ("duration", "startup_timeout", "grace", "jobs", "shader_jobs", "fps"):
        value = getattr (args, name)
        if not math.isfinite (value) or value <= 0:
            option = name.replace ("_", "-")
            parser.error (f"--{option} must be positive and finite")

    exe = Path (args.exe).resolve ()
    if not exe.is_file ():
        sys.exit (f"error: engine executable not found: {exe}")
    if shutil.which (args.glslang) is None:
        sys.exit (f"error: {args.glslang} not found in PATH")

    corpus = find_corpus (args.corpus)
    items = sorted (path for path in corpus.iterdir () if path.is_dir ())
    if args.ids:
        wanted = set (args.ids)
        items = [path for path in items if path.name in wanted]
        missing = wanted - {path.name for path in items}
        if missing:
            sys.exit (f"error: ids not found in corpus: {', '.join (sorted (missing))}")
    infos = {path.name: project_info (path) for path in items}
    projections = {path.name: scene_projection (path, infos [path.name] [2])
                   if infos [path.name] [1] == "scene" else "?" for path in items}
    if args.projection != "any":
        items = [path for path in items if projections [path.name] == args.projection]
    if not items:
        sys.exit (f"error: no workshop items in {corpus} matching the given filters")

    out = Path (args.out).resolve ()
    out.mkdir (parents = True, exist_ok = True)

    report = {
        "generated": time.strftime ("%Y-%m-%dT%H:%M:%S%z"),
        "exe": str (exe),
        "corpus": str (corpus),
        "duration_per_item": args.duration,
        "startup_timeout": args.startup_timeout,
        "jobs": args.jobs,
        "shader_jobs": args.shader_jobs,
        "complete": False,
        "items_discovered": len (items),
        "projection": args.projection,
        "totals": {"PASS": 0, "WARN": 0, "FAIL": 0, "SKIP": 0},
        "items": [],
    }
    # unconsumed JSON key path -> [wallpapers reporting it, total occurrences]
    unknown_keys: dict [str, list [int]] = {}

    print (f"validating {len (items)} item(s) from {corpus}; "
           f"{args.jobs} renderer(s), {args.shader_jobs} shader worker(s)", flush = True)
    report_path = out / "report.json"
    started = time.monotonic ()
    cancelled = threading.Event ()
    compiler = ShaderCompiler (args.glslang, args.shader_jobs)
    pool = concurrent.futures.ThreadPoolExecutor (max_workers = args.jobs)

    def save_report ():
        report ["elapsed_seconds"] = round (time.monotonic () - started, 3)
        report ["shader_checks_unique"] = len (compiler.cache)
        report ["shader_cache_hits"] = compiler.cache_hits
        report ["items"].sort (key = lambda entry: entry ["id"])
        temporary = report_path.with_suffix (".json.tmp")
        temporary.write_text (json.dumps (report, indent = 2), encoding = "utf-8")
        temporary.replace (report_path)

    save_report ()
    try:
        pending = {pool.submit (validate_item, exe, item, out, args, compiler, cancelled,
                                infos [item.name], projections [item.name]): item for item in items}
        for index, future in enumerate (concurrent.futures.as_completed (pending), 1):
            entry = future.result ()
            unknown = entry.pop ("telemetry", {})
            for key, count in unknown.items ():
                tally = unknown_keys.setdefault (key, [0, 0])
                tally [0] += 1
                tally [1] += count
            report ["totals"] [entry ["status"]] += 1
            report ["items"].append (entry)
            elapsed = time.monotonic () - started
            reasons = f" ({'; '.join (entry ['reasons'])})" if entry ["reasons"] else ""
            print (f"[{index}/{len (items)} {elapsed:.0f}s] {entry ['id']} "
                   f"({entry ['type']}/{entry ['projection']}) {entry ['title']!r}: "
                   f"{entry ['status']}{reasons}", flush = True)
            save_report ()
    except KeyboardInterrupt:
        cancelled.set ()
        print ("\nInterrupted; stopping validator processes and preserving partial report.", flush = True)
    finally:
        pool.shutdown (wait = True, cancel_futures = True)
        compiler.close ()
    report ["complete"] = not cancelled.is_set () and len (report ["items"]) == len (items)

    ranked = sorted (unknown_keys.items (), key = lambda entry: (-entry [1] [0], entry [0]))
    report ["unknown_keys"] = [{"key": key, "wallpapers": seen, "occurrences": total}
                               for key, (seen, total) in ranked]

    save_report ()

    totals = report ["totals"]
    print (f"\n{totals ['PASS']} pass, {totals ['WARN']} warn, {totals ['FAIL']} fail, "
           f"{totals ['SKIP']} skipped -> {report_path}")
    for entry in report ["items"]:
        if entry ["status"] in ("WARN", "FAIL"):
            print (f"  {entry ['status']} {entry ['id']}: {'; '.join (entry ['reasons'])}")

    if ranked:
        print (f"\ntop {min (TELEMETRY_TOP, len (ranked))} of {len (ranked)} JSON keys no parser consumed:")
        for key, (seen, total) in ranked [:TELEMETRY_TOP]:
            print (f"  {seen:4d} wallpaper(s) {total:6d}x  {key}")

    return 130 if cancelled.is_set () else (1 if totals ["FAIL"] else 0)


if __name__ == "__main__":
    sys.exit (main ())
