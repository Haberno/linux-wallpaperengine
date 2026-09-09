"""Opt-in regressions for Vec3 angle arguments and image property rendering.

The image regression uses Pillow and engine screenshots from isolated windows.
"""
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
    def render_image_properties(self, root, scripted):
        from PIL import Image

        for name in ('models', 'materials'):
            (root / name).mkdir(parents=True, exist_ok=True)
        (root / 'project.json').write_text(json.dumps({
            'type': 'scene', 'file': 'scene.json', 'title': 'Image script binding regression'}))
        (root / 'models/rectangle.json').write_text(json.dumps({'material': 'materials/rectangle.json'}))
        (root / 'materials/rectangle.json').write_text(json.dumps({'passes': [{
            'shader': 'genericimage2', 'combos': {'VERSION': 1}, 'textures': ['rectangle'],
            'blending': 'normal', 'cullmode': 'nocull', 'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
        Image.new('RGBA', (48, 12), (255, 255, 255, 255)).save(root / 'materials/rectangle.png')
        (root / 'materials/rectangle.tex-json').write_text(json.dumps({'format': 'rgba8888', 'nomip': True}))
        objects = [
            {'id': 1, 'name': 'Angle image', 'origin': '64 64 0', 'color': '1 .5 0',
             'angles': {'value': '0 0 0.5235987755982988', 'script': SCRIPT} if scripted
             else '0 0 1.5707963267948966'},
            {'id': 2, 'name': 'Scale image', 'origin': '180 64 0', 'color': '0 1 .5',
             'scale': {'value': '1 1 1', 'script': '''
export function init(value) {
    console.log('SCALE_INIT_OK');
    return new Vec3(.5, 2, 1);
}'''} if scripted else '.5 2 1'},
            {'id': 3, 'name': 'Visibility image', 'origin': '128 180 0', 'color': '.5 0 1',
             'visible': {'value': True, 'script': '''
export function init(value) { console.log('VISIBLE_INIT_OK'); return false; }
'''} if scripted else False},
        ]
        for obj in objects:
            obj.update(image='models/rectangle.json', size='48 12')
        (root / 'scene.json').write_text(json.dumps({
            'camera': {'eye': '128 128 128', 'center': '128 128 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 256, 'height': 256},
                        'clearcolor': '0 0 0', 'bloom': False}, 'objects': objects}))
        screenshot = root / 'frame.png'
        screenshot.unlink(missing_ok=True)
        log_path = root / 'engine.log'
        socket = Path(tempfile.gettempdir()) / f'lwe-script-image-{os.getpid()}-{root.name}.sock'
        env = dict(os.environ, WPE_LOG_FILE='off', WPE_CONTROL_SOCKET=str(socket),
                   WPE_HEALTH_REPORT=str(root / 'health.json'))
        with log_path.open('w') as log:
            process = subprocess.Popen([
                str(Path(os.environ['LWE_TEST_BINARY']).resolve()), '--window', '0x0x512x512',
                '--silent', '--fps', '10', '--no-full-screen-pause', '--screenshot', str(screenshot),
                '--screenshot-delay', '1', str(root)], env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 12
                while process.poll() is None and time.monotonic() < deadline:
                    if screenshot.exists():
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
        self.assertTrue(screenshot.exists(), output)
        self.assertNotIn('ScriptEngine [', output)
        if scripted:
            self.assertIn('ANGLE_UPDATE_OK', output)
            for marker in ('ANGLE_INIT_OK', 'SCALE_INIT_OK', 'VISIBLE_INIT_OK'):
                self.assertEqual(output.count(marker), 1, output)
        return Image.open(screenshot).convert('RGB')

    def test_image_scripts_drive_rendered_angles_scale_and_visibility(self):
        from PIL import ImageChops

        with tempfile.TemporaryDirectory(prefix='lwe-script-image-properties-') as directory:
            root = Path(directory)
            control = self.render_image_properties(root / 'control', False)
            scripted = self.render_image_properties(root / 'scripted', True)
            self.assertIsNotNone(control.getbbox(), 'Control scene must render visible rectangles')
            self.assertIsNone(ImageChops.difference(control, scripted).getbbox(),
                              'Scripted image angles, scale and visibility must match authored values')

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
