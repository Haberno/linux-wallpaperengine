"""Opt-in IScene property regression through the actual renderer and QuickJS.

LWE_TEST_BINARY=/path/to/engine python3 -m unittest discover -s tools/tests -p test_script_scene_fields.py
Requires a graphics session and Pillow. clearenabled is tracked separately with
the renderer's clear-enable implementation.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


FIELDS = {
    'bloom': False, 'bloomstrength': 1.25, 'bloomthreshold': .625,
    'clearcolor': [0, .25, .5], 'ambientcolor': [.1, .2, .3], 'skylightcolor': [.8, .6, .4],
    'fov': 66.5, 'nearz': .25, 'farz': 1500.5, 'camerafade': False,
    'camerashake': True, 'camerashakespeed': .3, 'camerashakeamplitude': .75,
    'camerashakeroughness': .125, 'cameraparallax': True, 'cameraparallaxamount': .8,
    'cameraparallaxdelay': .2, 'cameraparallaxmouseinfluence': .375,
}


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class SceneFields(unittest.TestCase):
    def test_scene_fields_read_write_independently_and_drive_clear_color(self):
        from PIL import Image

        with tempfile.TemporaryDirectory(prefix='lwe-scene-fields-') as directory:
            root = Path(directory)
            script = '''
const fields = FIELDS;
let frames = 0;
function snapshot() {
    const result = {};
    for (const name of Object.keys(fields)) {
        const value = thisScene[name];
        result[name] = Array.isArray(fields[name]) ? [value.x, value.y, value.z] : value;
    }
    return result;
}
export function init(value) {
    console.log('SCENE_FIELDS_INITIAL ' + JSON.stringify(snapshot()));
    for (const name of Object.keys(fields)) {
        const next = fields[name];
        thisScene[name] = Array.isArray(next) ? new Vec3(...next) : next;
    }
    thisScene.bloom = true;
    if (thisScene.bloom !== true) throw new Error('Bloom write failed');
    thisScene.bloom = false;
    thisScene.bloomstrength += .05;
    return value;
}
export function update(value) {
    if (++frames === 3) console.log('SCENE_FIELDS_UPDATED ' + JSON.stringify(snapshot()));
    return value;
}
'''.replace('FIELDS', json.dumps(FIELDS), 1)
            (root / 'project.json').write_text(json.dumps({
                'type': 'scene', 'file': 'scene.json', 'title': 'IScene fields regression'}))
            (root / 'scene.json').write_text(json.dumps({
                'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                            'bloom': False, 'bloomstrength': .5, 'bloomthreshold': .65,
                            'clearcolor': '.1 .2 .3', 'ambientcolor': '.2 .3 .4',
                            'skylightcolor': '.7 .8 .9'},
                'objects': [{'id': 1, 'name': 'IScene fields probe', 'solid': True,
                             'visible': {'value': True, 'script': script}}]}))
            screenshot = root / 'frame.png'
            log_path = root / 'engine.log'
            env = dict(os.environ, WPE_LOG_FILE='off', XDG_STATE_HOME=str(root / 'state'),
                       WPE_CONTROL_SOCKET=str(root / 'control.sock'),
                       WPE_HEALTH_REPORT=str(root / 'health.json'))
            with log_path.open('w') as log:
                process = subprocess.Popen([
                    str(Path(os.environ['LWE_TEST_BINARY']).resolve()), '--window', '0x0x320x180',
                    '--silent', '--fps', '10', '--no-full-screen-pause', '--screenshot', str(screenshot),
                    '--screenshot-delay', '5', str(root)], env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 12
                    while process.poll() is None and time.monotonic() < deadline:
                        if screenshot.exists() and 'SCENE_FIELDS_UPDATED ' in log_path.read_text():
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
            output = log_path.read_text()
            self.assertEqual(process.returncode, 0, output)
            self.assertNotIn('ScriptEngine [', output)
            def read_result(marker):
                rows = [line.split(marker, 1)[1] for line in output.splitlines() if marker in line]
                self.assertEqual(len(rows), 1, output)
                return json.loads(rows[0])
            initial = read_result('SCENE_FIELDS_INITIAL ')
            self.assertAlmostEqual(initial['bloomstrength'], .5, places=5)
            self.assertAlmostEqual(initial['bloomthreshold'], .65, places=5)
            self.assertAlmostEqual(initial['ambientcolor'][0], .2, places=5)
            self.assertAlmostEqual(initial['skylightcolor'][0], .7, places=5)
            updated = read_result('SCENE_FIELDS_UPDATED ')
            expected = dict(FIELDS, bloomstrength=1.3)
            for name, value in expected.items():
                with self.subTest(field=name):
                    if isinstance(value, list):
                        for actual, component in zip(updated[name], value):
                            self.assertAlmostEqual(actual, component, places=5)
                    elif isinstance(value, bool):
                        self.assertIs(updated[name], value)
                    else:
                        self.assertAlmostEqual(updated[name], value, places=5)
            self.assertTrue(screenshot.exists(), output)
            pixel = Image.open(screenshot).convert('RGB').getpixel((160, 90))
            for actual, expected in zip(pixel, (0, 64, 128)):
                self.assertLessEqual(abs(actual - expected), 1, 'Allow one byte of framebuffer quantization')


if __name__ == '__main__':
    unittest.main()
