"""Opt-in regression for degree-valued Vec3 angle script arguments."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


SCRIPT = '''
let initial, n = 0;
export function init(value) {
    if (!(value instanceof Vec3)) throw new Error('Angle init argument is not Vec3');
    if (Math.abs(value.z - 30) > .0001) throw new Error('Angle init must use degrees');
    initial = value;
    console.log('ANGLE_INIT_OK');
    return value.add(new Vec3(0, 0, 60));
}
export function update(value) {
    if (!(value instanceof Vec3)) throw new Error('Angle update argument is not Vec3');
    if (Math.abs(value.z - 90) > .0001) throw new Error('Angle update lost degrees');
    if (Math.abs(initial.z - 30) > .0001) throw new Error('Angle argument lost ownership');
    value.z = 90; // Mutating without returning a value must also propagate.
    if (++n === 3) console.log('ANGLE_UPDATE_OK');
}
'''


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ScriptAngles(unittest.TestCase):
    def test_vec3_arguments_keep_degrees_and_mutation_semantics(self):
        with tempfile.TemporaryDirectory(prefix='lwe-script-angles-') as directory:
            root = Path(directory)
            (root / 'project.json').write_text(json.dumps({
                'type': 'scene', 'file': 'scene.json', 'title': 'Angle arguments regression'}))
            (root / 'scene.json').write_text(json.dumps({
                'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': {'width': 320, 'height': 180}},
                'objects': [{'id': 1, 'name': 'Angle probe', 'text': 'ANGLE',
                             'origin': '160 90 0', 'pointsize': 10,
                             'angles': {'value': '0 0 0.5235987755982988', 'script': SCRIPT}}]}))
            log_path = root / 'engine.log'
            env = dict(os.environ, WPE_LOG_FILE='off', WPE_CONTROL_SOCKET=str(root / 'control.sock'),
                       WPE_HEALTH_REPORT=str(root / 'health.json'))
            with log_path.open('w') as log:
                process = subprocess.Popen([
                    str(Path(os.environ['LWE_TEST_BINARY']).resolve()), '--window', '0x0x320x180',
                    '--silent', '--fps', '10', '--no-full-screen-pause', str(root)],
                    env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 10
                    while process.poll() is None and time.monotonic() < deadline:
                        if 'ANGLE_UPDATE_OK' in log_path.read_text():
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
            self.assertIn('ANGLE_INIT_OK', output)
            self.assertIn('ANGLE_UPDATE_OK', output)
            self.assertNotIn('ScriptEngine [', output)


if __name__ == '__main__':
    unittest.main()
