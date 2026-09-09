"""Opt-in SceneScript color-module regression; set LWE_TEST_BINARY to the engine."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ScriptColor(unittest.TestCase):
    def test_imported_color_module_runs_in_property_scripts(self):
        script = '''
import * as WEColor from 'WEColor';
function check(actual, expected) {
    if (!(actual instanceof Vec3)) throw new Error('Color result is not Vec3');
    for (const key of ['x', 'y', 'z'])
        if (Math.abs(actual[key] - expected[key]) > 0.00001)
            throw new Error('Wrong color component ' + key + ': ' + actual[key]);
}
export function init(value) {
    check(WEColor.normalizeColor(new Vec3(255,128,0)), new Vec3(1,128/255,0));
    check(WEColor.expandColor(new Vec3(0,.5,1)), new Vec3(0,127.5,255));
    check(WEColor.rgb2hsv(new Vec3(.2,.6,.4)), new Vec3(5/12,2/3,.6));
    check(WEColor.hsv2rgb(new Vec3(1.25,.8,.5)), new Vec3(.3,.5,.1));
    console.log('COLOR_MODULE_INIT_OK');
    return value;
}
export function update(value) {
    check(WEColor.hsv2rgb(WEColor.rgb2hsv(new Vec3(.1,.3,.7))), new Vec3(.1,.3,.7));
    console.log('COLOR_MODULE_UPDATE_OK');
    return value;
}
'''
        with tempfile.TemporaryDirectory(prefix='lwe-script-color-') as directory:
            root = Path(directory)
            (root / 'project.json').write_text(json.dumps({
                'type': 'scene', 'file': 'scene.json', 'title': 'Color-module regression'}))
            (root / 'scene.json').write_text(json.dumps({
                'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                            'bloom': {'value': False, 'script': script}}, 'objects': []}))
            log_path = root / 'engine.log'
            env = dict(os.environ, WPE_LOG_FILE='off', WPE_CONTROL_SOCKET=str(root / 'control.sock'),
                       WPE_HEALTH_REPORT=str(root / 'health.json'))
            with log_path.open('w') as log:
                process = subprocess.Popen([
                    str(Path(os.environ['LWE_TEST_BINARY']).resolve()), '--window', '0x0x320x180',
                    '--volume', '0', '--fps', '10', '--no-full-screen-pause', str(root)],
                    env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 10
                    while process.poll() is None and time.monotonic() < deadline:
                        if 'COLOR_MODULE_UPDATE_OK' in log_path.read_text():
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
            self.assertIn('COLOR_MODULE_INIT_OK', output)
            self.assertIn('COLOR_MODULE_UPDATE_OK', output)
            self.assertNotIn('ScriptEngine [', output)


if __name__ == '__main__':
    unittest.main()
