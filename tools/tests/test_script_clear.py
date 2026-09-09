"""Opt-in clearenabled regression through SceneScript and retained framebuffer pixels.

Requires LWE_TEST_BINARY, a graphics session, and Pillow. All scenes and state are
private temporary fixtures; the user's wallpaper and stored settings are untouched.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ScriptClear(unittest.TestCase):
    def capture(self, root, *, source, initial=None, reenable=False, occlusion_control=False):
        from PIL import Image

        for name in ('models', 'materials'):
            (root / name).mkdir(parents=True, exist_ok=True)
        (root / 'project.json').write_text(json.dumps({
            'type': 'scene', 'file': 'scene.json', 'title': 'Scene clear regression'}))
        (root / 'models/depth.json').write_text(json.dumps({'material': 'materials/depth.json'}))
        (root / 'materials/depth.json').write_text(json.dumps({'passes': [{
            'shader': 'genericimage2', 'combos': {'VERSION': 1}, 'textures': ['depth'],
            'blending': 'normal', 'cullmode': 'nocull', 'depthtest': 'enabled', 'depthwrite': 'enabled'}]}))
        Image.new('RGBA', (64, 64), (255, 255, 255, 255)).save(root / 'materials/depth.png')
        (root / 'materials/depth.tex-json').write_text(json.dumps({'format': 'rgba8888', 'nomip': True}))
        initial_value = True if initial is None else initial
        script = '''
let n = 0;
export function init(value) {
    if (thisScene.clearenabled !== INITIAL) throw new Error('Authored/default clearenabled was lost');
    if (thisScene.bloom !== false) throw new Error('Clearing must be independent of bloom');
    thisScene.clearenabled = true;
    if (thisScene.clearenabled !== true) throw new Error('Clear flag write failed');
    console.log('CLEAR_INIT_OK');
    return SOURCE === 'property' ? true : value;
}
export function update(value) {
    // Startup update and the first rendered frame seed known color/depth values.
    // Subsequent frames draw the same image farther away: stale depth would hide it.
    const seed = ++n <= 2;
    const enabled = seed || (REENABLE && n >= 5);
    thisScene.clearcolor = seed ? new Vec3(1, 0, 0) : new Vec3(1, 1, 1);
    const layer = thisScene.getLayer('Depth probe');
    layer.origin = new Vec3(160, 90, seed ? 64 : 0);
    layer.color = seed ? new Vec4(0, 1, 0, 1) : new Vec4(0, 0, 1, 1);
    if (SOURCE === 'setter') {
        thisScene.clearenabled = enabled;
        if (thisScene.clearenabled !== enabled) throw new Error('Clear flag read/write differs');
    } else if (n >= 4 && thisScene.clearenabled !== (REENABLE && n >= 6)) {
        throw new Error('Scene clear script did not update its renderer setting');
    }
    if (n === 6) console.log('CLEAR_UPDATE_OK');
    return SOURCE === 'property' ? enabled : value;
}
'''.replace('INITIAL', json.dumps(initial_value)).replace('REENABLE', json.dumps(reenable)).replace(
            'SOURCE', json.dumps(source))
        # Perspective forces an actual scene depth attachment; 2D image-only scenes
        # intentionally have none, which would make the depth assertion vacuous.
        general = {'orthogonalprojection': None, 'nearz': .1, 'farz': 1000, 'fov': 90,
                   'customsortorder': True, 'clearcolor': '1 0 0', 'bloom': False}
        objects = [{'id': 1, 'name': 'Depth probe', 'image': 'models/depth.json',
                    'origin': '160 90 64', 'size': '64 64', 'color': '0 1 0'}]
        if occlusion_control:
            # Draw the near image first. Without depth testing, the later far
            # blue image would overwrite its center.
            objects.insert(0, {'id': 3, 'name': 'Near occluder', 'image': 'models/depth.json',
                               'origin': '160 90 64', 'size': '64 64', 'color': '0 1 0'})
        if source == 'property':
            general['clearenabled'] = {'value': initial_value, 'script': script}
        else:
            if initial is not None:
                general['clearenabled'] = initial
            objects.append({'id': 2, 'name': 'Clear controller', 'solid': True,
                            'visible': {'value': True, 'script': script}})
        (root / 'scene.json').write_text(json.dumps({
            'camera': {'eye': '160 90 128', 'center': '160 90 0', 'up': '0 1 0'},
            'general': general, 'objects': objects}))
        screenshot = root / 'frame.png'
        log_path = root / 'engine.log'
        env = dict(os.environ, WPE_LOG_FILE='off', XDG_STATE_HOME=str(root / 'state'),
                   WPE_CONTROL_SOCKET=str(Path(tempfile.gettempdir()) / f'lwe-clear-{os.getpid()}.sock'),
                   WPE_HEALTH_REPORT=str(root / 'health.json'))
        with log_path.open('w') as log:
            process = subprocess.Popen([
                str(Path(os.environ['LWE_TEST_BINARY']).resolve()), '--window', '0x0x320x180',
                '--silent', '--no-audio-processing', '--fps', '10',
                '--no-full-screen-pause', '--disable-mouse', '--screenshot', str(screenshot),
                '--screenshot-delay', '5', str(root)], env=env, stdout=log, stderr=subprocess.STDOUT)
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
        self.assertNotIn('Failed to setup object', output)
        self.assertEqual(output.count('CLEAR_INIT_OK'), 1, output)
        self.assertIn('CLEAR_UPDATE_OK', output)
        return Image.open(screenshot).convert('RGB')

    def test_clear_setting_and_script_preserve_color_but_still_clear_depth(self):
        with tempfile.TemporaryDirectory(prefix='lwe-clear-') as directory:
            control = self.capture(Path(directory) / 'depth-control', source='setter', occlusion_control=True)
            self.assertEqual(control.getpixel((160, 90)), (0, 255, 0),
                             'Depth control must keep the nearer green image in front of the later blue image')
            for source, initial in [('setter', None), ('setter', False), ('property', True)]:
                with self.subTest(source=source, initial=initial):
                    frame = self.capture(Path(directory) / f'{source}-{initial}', source=source, initial=initial)
                    self.assertEqual(frame.getpixel((20, 20)), (255, 0, 0),
                                     'Disabling clear must retain the initial red background')
                    self.assertEqual(frame.getpixel((160, 90)), (0, 0, 255),
                                     'Depth must clear so the farther blue image replaces the nearer green one')

    def test_clear_can_be_enabled_again(self):
        with tempfile.TemporaryDirectory(prefix='lwe-clear-enable-') as directory:
            for source in ('setter', 'property'):
                with self.subTest(source=source):
                    frame = self.capture(Path(directory) / source, source=source, reenable=True)
                    self.assertEqual(frame.getpixel((20, 20)), (255, 255, 255),
                                     'Re-enabling clear must replace the retained background')
                    self.assertEqual(frame.getpixel((160, 90)), (0, 0, 255))


if __name__ == '__main__':
    unittest.main()
