"""Opt-in persistent SceneScript storage regression using real C++ vectors.

LWE_TEST_BINARY=/path/to/engine python3 -m unittest discover -s tools/tests -p test_script_storage.py
Requires a graphics session. Every engine process uses private temporary storage.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for render integration tests')
class SceneScriptStorage(unittest.TestCase):
    def run_fixture(self, project, state):
        script = '''
export function init(value) {
    const previous = localStorage.get('position');
    const enabled = localStorage.get('enabled');
    localStorage.set('position', new Vec3(12, 34, 56));
    localStorage.set('enabled', false);
    localStorage.set('nested', {positions: [new Vec2(7, 8), new Vec4(1, 2, 3, 4)]});
    const loaded = localStorage.get('position');
    console.log('SCENESCRIPT_STORAGE_RESULT ' + JSON.stringify({
        previous: previous === undefined ? null : previous,
        enabled: enabled === undefined ? null : enabled,
        loaded: loaded, plainObject: !(loaded instanceof Vec3),
        nested: localStorage.get('nested')
    }));
    return value;
}
'''
        project.mkdir(exist_ok=True)
        (project / 'project.json').write_text(json.dumps({
            'type': 'scene', 'file': 'scene.json', 'title': 'SceneScript storage regression'}))
        (project / 'scene.json').write_text(json.dumps({
            'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0'},
            'objects': [{'id': 1, 'name': 'Storage probe', 'solid': True,
                         'visible': {'value': True, 'script': script}}]}))
        log_path = project / 'engine.log'
        env = dict(os.environ, XDG_STATE_HOME=str(state),
                   WPE_CONTROL_SOCKET=str(project / 'control.sock'), WPE_LOG_FILE='off',
                   WPE_HEALTH_REPORT=str(project / 'health.json'))
        marker = 'SCENESCRIPT_STORAGE_RESULT '
        with log_path.open('w') as log:
            process = subprocess.Popen([
                str(Path(os.environ['LWE_TEST_BINARY']).resolve()), '--window', '0x0x320x180',
                '--silent', '--fps', '10', '--no-full-screen-pause', str(project)],
                env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline and process.poll() is None:
                    if marker in log_path.read_text():
                        break
                    time.sleep(.05)
            finally:
                if process.poll() is None:
                    process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        text = log_path.read_text()
        self.assertEqual(process.returncode, 0, text[-6000:])
        payloads = [line.split(marker, 1)[1] for line in text.splitlines() if marker in line]
        self.assertEqual(len(payloads), 1, text[-6000:])
        return json.loads(payloads[0])

    def test_vectors_and_booleans_survive_restart_with_project_isolation(self):
        with tempfile.TemporaryDirectory(prefix='lwe-script-storage-') as directory:
            root = Path(directory)
            state = root / 'state'
            first = self.run_fixture(root / 'first', state)
            expected = {'previous': None, 'enabled': None, 'loaded': {'x': 12, 'y': 34, 'z': 56},
                        'plainObject': True,
                        'nested': {'positions': [{'x': 7, 'y': 8}, {'x': 1, 'y': 2, 'z': 3, 'w': 4}]}}
            self.assertEqual(first, expected)
            restarted = self.run_fixture(root / 'first', state)
            self.assertEqual(restarted, dict(expected, previous=expected['loaded'], enabled=False))
            other = self.run_fixture(root / 'second', state)
            self.assertEqual(other, expected)


if __name__ == '__main__':
    unittest.main()
