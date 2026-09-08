"""Render regression: LWE_TEST_BINARY=/path/to/engine python3 -m unittest discover
-s tools/tests -p test_engine_getters.py. Requires a desktop graphics session.
"""
import json
import os
from pathlib import Path
import resource
import signal
import socket
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for render integration tests')
class EngineGetters(unittest.TestCase):
    def test_layer_getters_survive_inheritance_and_wallpaper_replacement(self):
        with tempfile.TemporaryDirectory(prefix='lwe-getters-') as directory:
            root = Path(directory)
            for token in (1, 2):
                fixture = root / str(token)
                fixture.mkdir()
                script = '''
let checked = false;
export function update(value) {
    // Layer scripts already inherit engine; another level exercises delegated getters.
    const inherited = Object.create(engine);
    if (inherited.userProperties.testtoken !== TOKEN)
        throw new Error('Getter resolved the wrong project');
    if (inherited.canvasSize.x !== 640 || inherited.canvasSize.y !== 360)
        throw new Error('Wrong canvas size');
    if (!(inherited.screenResolution.x > 0 && inherited.screenResolution.y > 0))
        throw new Error('Wrong screen resolution');
    if (!checked) console.log('NATIVE_ENGINE_GETTERS_OK TOKEN');
    checked = true;
    return 'Engine getters work';
}
'''.replace('TOKEN', str(token))
                (fixture / 'project.json').write_text(json.dumps({
                    'type': 'scene', 'file': 'scene.json', 'title': 'Engine getter regression',
                    'general': {'properties': {'testtoken': {'type': 'slider', 'value': token}}}}))
                (fixture / 'scene.json').write_text(json.dumps({
                    'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                    'general': {'orthogonalprojection': {'width': 640, 'height': 360},
                                'clearcolor': '0 0 0', 'clearenabled': True},
                    'objects': [{'id': 1, 'name': 'Inherited engine getter test',
                                 'origin': '320 180 0', 'pointsize': 20,
                                 'text': {'value': 'Starting', 'script': script}}]}))
            control = root / 'control.sock'
            log_path = root / 'engine.log'
            env = dict(os.environ, WPE_CONTROL_SOCKET=str(control), WPE_LOG_FILE='off',
                       WPE_HEALTH_REPORT=str(root / 'health.json'))
            binary = str(Path(os.environ['LWE_TEST_BINARY']).resolve())
            with log_path.open('w') as log:
                process = subprocess.Popen([
                    binary, '--window', '0x0x320x180', '--silent', '--fps', '30',
                    '--no-full-screen-pause', str(root / '1')], env=env,
                    stdout=log, stderr=subprocess.STDOUT,
                    preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                try:
                    def wait_marker(marker, count=1):
                        deadline = time.monotonic() + 15
                        while time.monotonic() < deadline and process.poll() is None:
                            if log_path.read_text().count(marker) >= count:
                                return
                            time.sleep(.05)
                        self.fail(f'Engine exit={process.poll()}; missing {marker}\n{log_path.read_text()[-6000:]}')

                    wait_marker('NATIVE_ENGINE_GETTERS_OK 1')
                    for token in (2, 1):
                        # Allow the transition to finish before replacing the next scene.
                        time.sleep(1.2)
                        with socket.socket(socket.AF_UNIX) as client:
                            client.settimeout(3)
                            client.connect(str(control))
                            client.sendall(f'switch default fade {root / str(token)}\n'.encode())
                            self.assertTrue(client.recv(4096).startswith(b'ok'))
                        wait_marker(f'NATIVE_ENGINE_GETTERS_OK {token}', 2 if token == 1 else 1)
                    time.sleep(1.2)
                    self.assertIsNone(process.poll())
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            self.assertEqual(process.returncode, 0)
            health = json.loads((root / 'health.json').read_text())
            self.assertEqual(health['counters']['switch.apply'], 2)
            self.assertNotIn('log.exception', health['counters'])


if __name__ == '__main__':
    unittest.main()
