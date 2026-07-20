#!/usr/bin/env python3
"""Corpus-wide parse/shader validator for linux-wallpaperengine.

Walks a directory of workshop items and, for each one:
  1. runs it in an isolated, windowed, muted engine process for a few seconds
     with WPE_HEALTH_REPORT (JSON exit summary) and WPE_DUMP_SHADERS enabled,
  2. stops it with SIGINT (a clean stop must exit 0 and still write the report),
  3. batch-compiles every dumped shader unit with glslang,
  4. classifies the run as PASS / WARN / FAIL.

Emits <out>/report.json plus a console summary, and exits non-zero if any
item failed.  Per-item artifacts (log, health report, failing shader dumps)
are kept under <out>/<id>/ for triage; shader dumps of passing items are
deleted unless --keep-all is given.

Requires: python3 (stdlib only), glslang, a running display session.
"""

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
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


def project_info (item: Path) -> tuple [str, str]:
    """Best-effort (title, type) from project.json."""
    try:
        with open (item / "project.json", encoding = "utf-8", errors = "replace") as fp:
            project = json.load (fp)
        return str (project.get ("title", "?")), str (project.get ("type", "?")).lower ()
    except (OSError, json.JSONDecodeError, ValueError):
        return "?", "?"


def run_engine (exe: Path, item_id: str, outdir: Path, args) -> dict:
    """Run one background in its own process; return raw run facts."""
    shaders = outdir / "shaders"
    shaders.mkdir (parents = True, exist_ok = True)
    health_path = outdir / "health.json"
    health_path.unlink (missing_ok = True)

    env = dict (os.environ)
    env ["WPE_HEALTH_REPORT"] = str (health_path)
    env ["WPE_DUMP_SHADERS"] = str (shaders)

    cmd = [str (exe), "--window", args.window, "--silent", "--fps", str (args.fps), item_id]
    facts = {"cmd": " ".join (cmd), "hung": False, "early_exit": False}

    with open (outdir / "log.txt", "wb") as log:
        proc = subprocess.Popen (cmd, stdout = log, stderr = subprocess.STDOUT, env = env)
        try:
            facts ["exit_code"] = proc.wait (timeout = args.duration)
            facts ["early_exit"] = True
        except subprocess.TimeoutExpired:
            proc.send_signal (signal.SIGINT)
            try:
                facts ["exit_code"] = proc.wait (timeout = args.grace)
            except subprocess.TimeoutExpired:
                proc.kill ()
                facts ["exit_code"] = proc.wait ()
                facts ["hung"] = True

    try:
        # decode with replacement so a mangled report degrades to FAIL/WARN data
        # for one item instead of aborting the whole corpus run
        facts ["health"] = json.loads (health_path.read_bytes ().decode ("utf-8", errors = "replace"))
    except (OSError, json.JSONDecodeError):
        facts ["health"] = None

    return facts


def compile_shaders (shaderdir: Path, glslang: str) -> tuple [int, list [dict]]:
    """glslang every dumped unit; return (total, failures)."""
    total = 0
    failures = []
    for unit in sorted (shaderdir.iterdir ()):
        if unit.suffix not in SHADER_EXTS:
            continue
        total += 1
        try:
            result = subprocess.run ([glslang, str (unit)], capture_output = True,
                                     text = True, timeout = 30)
            if result.returncode != 0:
                output = (result.stdout + result.stderr).strip () [:DETAIL_CAP]
                failures.append ({"file": unit.name, "output": output})
        except (OSError, subprocess.TimeoutExpired) as error:
            failures.append ({"file": unit.name, "output": f"glslang did not run: {error}"})
    return total, failures


def classify (facts: dict, shader_total: int, shader_failures: list [dict]) -> tuple [str, list [str]]:
    reasons = []
    health = facts ["health"]
    counters = (health or {}).get ("counters", {})
    frames = int ((health or {}).get ("timing", {}).get ("frames", 0))

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

    if counters.get ("log.error", 0) > 0:
        reasons.append (f"log.error x{counters ['log.error']} (see details in health.json)")
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
    parser.add_argument ("--duration", type = float, default = 8.0,
                         help = "seconds to let each background render (default: %(default)s)")
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
    if not items:
        sys.exit (f"error: no workshop items in {corpus}")

    out = Path (args.out)
    out.mkdir (parents = True, exist_ok = True)

    report = {
        "generated": time.strftime ("%Y-%m-%dT%H:%M:%S%z"),
        "exe": str (exe),
        "corpus": str (corpus),
        "duration_per_item": args.duration,
        "totals": {"PASS": 0, "WARN": 0, "FAIL": 0, "SKIP": 0},
        "items": [],
    }

    print (f"validating {len (items)} item(s) from {corpus}")
    for index, item in enumerate (items, 1):
        title, wp_type = project_info (item)
        outdir = out / item.name
        print (f"[{index}/{len (items)}] {item.name} ({wp_type}) {title!r} ... ",
               end = "", flush = True)

        if wp_type not in WALLPAPER_TYPES and not args.include_assets:
            print ("SKIP (not a standalone wallpaper; --include-assets to run anyway)")
            report ["totals"] ["SKIP"] += 1
            report ["items"].append ({
                "id": item.name,
                "title": title,
                "type": wp_type,
                "status": "SKIP",
                "reasons": ["no wallpaper type in project.json (workshop asset/effect)"],
            })
            continue

        try:
            facts = run_engine (exe, item.name, outdir, args)
            shader_total, shader_failures = compile_shaders (outdir / "shaders", args.glslang)
            status, reasons = classify (facts, shader_total, shader_failures)
        except Exception as error:  # noqa: BLE001 - one item must never abort the corpus run
            facts = {"exit_code": None, "health": None}
            shader_total, shader_failures = 0, []
            status, reasons = "FAIL", [f"validator error: {error!r}"]
        print (f"{status}" + (f" ({'; '.join (reasons)})" if reasons else ""))

        if status == "PASS" and not args.keep_all:
            shutil.rmtree (outdir / "shaders", ignore_errors = True)

        timing = (facts ["health"] or {}).get ("timing", {})
        report ["totals"] [status] += 1
        report ["items"].append ({
            "id": item.name,
            "title": title,
            "type": wp_type,
            "status": status,
            "reasons": reasons,
            "exit_code": facts ["exit_code"],
            "frames": timing.get ("frames"),
            "avg_fps": timing.get ("avg_fps"),
            "worst_frame_ms": timing.get ("worst_frame_ms"),
            "shaders_total": shader_total,
            "shaders_failed": len (shader_failures),
            "shader_failures": shader_failures,
            "counters": (facts ["health"] or {}).get ("counters", {}),
        })

    report_path = out / "report.json"
    with open (report_path, "w", encoding = "utf-8") as fp:
        json.dump (report, fp, indent = 2)

    totals = report ["totals"]
    print (f"\n{totals ['PASS']} pass, {totals ['WARN']} warn, {totals ['FAIL']} fail, "
           f"{totals ['SKIP']} skipped -> {report_path}")
    for entry in report ["items"]:
        if entry ["status"] in ("WARN", "FAIL"):
            print (f"  {entry ['status']} {entry ['id']}: {'; '.join (entry ['reasons'])}")

    return 1 if totals ["FAIL"] else 0


if __name__ == "__main__":
    sys.exit (main ())
