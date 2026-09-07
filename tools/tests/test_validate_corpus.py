"""Regression coverage for corpus scheduling, startup timing and shader deduplication."""
import concurrent.futures
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import threading
import types
import unittest

SPEC = importlib.util.spec_from_file_location ('validator', Path (__file__).parents [1] / 'validate-corpus.py')
validator = importlib.util.module_from_spec (SPEC)
SPEC.loader.exec_module (validator)


class ValidatorTests (unittest.TestCase):
    def setUp (self):
        self.temporary = tempfile.TemporaryDirectory ()
        self.addCleanup (self.temporary.cleanup)
        self.root = Path (self.temporary.name)

    def executable (self, name, source):
        path = self.root / name
        path.write_text ('#!/usr/bin/env python3\n' + source)
        path.chmod (0o755)
        return path

    def test_shader_cache_preserves_stage_and_per_file_failures (self):
        counter = self.root / 'calls'
        compiler_path = self.executable ('glslang', f'''import os, pathlib, sys
with open ({str(counter)!r}, 'a') as fp: fp.write ('call\\n')
if pathlib.Path (sys.argv [1]).suffix == '.frag':
    print ('invalid fragment')
    sys.exit (1)
''')
        compiler = validator.ShaderCompiler (str (compiler_path), 2)
        self.addCleanup (compiler.close)
        directories = [self.root / str (i) for i in range (2)]
        for directory in directories:
            directory.mkdir ()
            for name in ('one.vert', 'two.vert', 'bad.frag'):
                (directory / name).write_text ('same source')
        with concurrent.futures.ThreadPoolExecutor (max_workers = 2) as pool:
            results = list (pool.map (compiler.compile, directories))
        self.assertEqual (len (counter.read_text ().splitlines ()), 2)
        self.assertEqual (compiler.cache_hits, 4)
        for total, failures in results:
            self.assertEqual (total, 3)
            self.assertEqual (failures, [{'file': 'bad.frag', 'output': 'invalid fragment'}])

    def test_render_duration_starts_after_loading (self):
        engine = self.executable ('engine', '''import json, os, pathlib, signal, time
signal.signal (signal.SIGINT, lambda *_: (_ for _ in ()).throw (KeyboardInterrupt ()))
time.sleep (0.3)
pathlib.Path (os.environ ['WPE_CONTROL_SOCKET']).touch ()
start = time.monotonic ()
try:
    while True: time.sleep (0.01)
except KeyboardInterrupt:
    elapsed = time.monotonic () - start
    pathlib.Path (os.environ ['WPE_HEALTH_REPORT']).write_text (json.dumps ({
        'timing': {'frames': int (elapsed * 100)}, 'counters': {}}))
''')
        args = types.SimpleNamespace (telemetry = False, window = '0x0x64x64', fps = 15,
                                      duration = 0.2, startup_timeout = 3, grace = 2)
        facts = validator.run_engine (engine, self.root / 'item', self.root / 'out', args,
                                      threading.Event ())
        self.assertGreaterEqual (facts ['startup_seconds'], 0.3)
        self.assertGreaterEqual (facts ['health'] ['timing'] ['frames'], 19)
        self.assertEqual (validator.classify (facts, 0, []), ('PASS', []))

    def test_startup_timeout_is_explicit_failure (self):
        engine = self.executable ('stuck', '''import signal, time
signal.signal (signal.SIGINT, signal.SIG_IGN)
time.sleep (10)
''')
        args = types.SimpleNamespace (telemetry = False, window = '0x0x64x64', fps = 15,
                                      duration = 0.1, startup_timeout = 0.3, grace = 0.1)
        facts = validator.run_engine (engine, self.root / 'item', self.root / 'out', args,
                                      threading.Event ())
        status, reasons = validator.classify (facts, 0, [])
        self.assertEqual (status, 'FAIL')
        self.assertIn ('startup timed out before scene was ready', reasons)
        self.assertTrue (facts ['hung'])

    def test_early_exit_is_not_success (self):
        engine = self.executable ('early', 'pass\n')
        args = types.SimpleNamespace (telemetry = False, window = '0x0x64x64', fps = 15,
                                      duration = 0.1, startup_timeout = 3, grace = 1)
        facts = validator.run_engine (engine, self.root / 'item', self.root / 'out', args,
                                      threading.Event ())
        self.assertTrue (facts ['early_exit'])
        self.assertEqual (validator.classify (facts, 0, []) [0], 'FAIL')


if __name__ == '__main__':
    unittest.main ()
