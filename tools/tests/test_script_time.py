"""Opt-in SceneScript clock regression across switches in one engine process.

LWE_TEST_BINARY=/path/to/engine python3 -m unittest discover -s tools/tests -p test_script_time.py
Requires a graphics session. The test uses its own control socket and fixtures.
"""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ScriptTime(unittest.TestCase):
    def test_runtime_restarts_and_frametime_tracks_the_scene_after_switching(self):
        with tempfile.TemporaryDirectory(prefix='lwe-script-time-') as directory:
            root = Path(directory)
            for label in ('first', 'second'):
                fixture = root / label
                fixture.mkdir()
                script = '''
let previous;
let frames = 0;
export function init(value) {
    console.log('CLOCK_INIT ' + JSON.stringify({label: LABEL, time: engine.runtime}));
    return value;
}
export function update(value) {
    const time = engine.runtime;
    if (++frames <= 8) console.log('CLOCK_FRAME ' + JSON.stringify({
        label: LABEL, time, dt: engine.frametime,
        elapsed: previous === undefined ? null : time - previous
    }));
    previous = time;
    return value;
}
'''.replace('LABEL', json.dumps(label))
                (fixture / 'project.json').write_text(json.dumps({
                    'type': 'scene', 'file': 'scene.json', 'title': 'Scene clock ' + label}))
                (fixture / 'scene.json').write_text(json.dumps({
                    'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                    'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                                'camerafade': False, 'clearcolor': '0 0 0'},
                    'objects': [{'id': 1, 'name': 'clock probe', 'solid': True,
                                 'visible': {'value': True, 'script': script}}]}))
            control = root / 'control.sock'
            log_path = root / 'engine.log'
            binary = Path(os.environ['LWE_TEST_BINARY']).resolve()
            env = dict(os.environ, LD_LIBRARY_PATH=str(binary.parent), WPE_LOG_FILE='off',
                       WPE_CONTROL_SOCKET=str(control), XDG_STATE_HOME=str(root / 'state'))
            with log_path.open('w') as log:
                process = subprocess.Popen([
                    str(binary), '--window', '0x0x320x180', '--silent', '--fps', '10',
                    '--disable-mouse', '--no-full-screen-pause', str(root / 'first')],
                    env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    for phase, label in enumerate(('first', 'second', 'first')):
                        if phase:
                            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                                client.settimeout(5)
                                client.connect(str(control))
                                client.sendall(f'switch default fade {root / label}\n'.encode())
                                self.assertTrue(client.recv(4096).startswith(b'ok'))
                        deadline = time.monotonic() + 20
                        while process.poll() is None and time.monotonic() < deadline:
                            output = log_path.read_text()
                            if control.exists() and output.count('CLOCK_FRAME ') >= (phase + 1) * 8:
                                break
                            time.sleep(.05)
                        else:
                            self.fail('Scene failed to produce clock samples:\n' + log_path.read_text())
                        # Age the persistent process before switching away and back.
                        time.sleep(.3)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            output = log_path.read_text()
            if os.environ.get('LWE_TEST_LOG'):
                Path(os.environ['LWE_TEST_LOG']).write_text(output)
            self.assertEqual(process.returncode, 0, output)
            initial = [json.loads(row.split('CLOCK_INIT ', 1)[1])
                       for row in output.splitlines() if 'CLOCK_INIT ' in row]
            self.assertEqual([row['label'] for row in initial], ['first', 'second', 'first'], output)
            self.assertEqual([row['time'] for row in initial], [0, 0, 0], output)
            frames = [json.loads(row.split('CLOCK_FRAME ', 1)[1])
                      for row in output.splitlines() if 'CLOCK_FRAME ' in row]
            self.assertEqual(len(frames), 24, output)
            for row in frames:
                self.assertGreater(row['dt'], 0)
                if row['elapsed'] is not None and row['elapsed'] > .001:
                    self.assertAlmostEqual(row['dt'], row['elapsed'], places=5)
                if row['elapsed'] is None:
                    self.assertEqual(row['time'], 0, output)


if __name__ == '__main__':
    unittest.main()
