"""Opt-in graphics regressions for image visibility and offscreen consumers."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ScriptVisibility(unittest.TestCase):
    def capture(self, root, visible, *, consumer=False, effect=False, direct_control=False):
        from PIL import Image

        for name in ('models', 'materials', 'effects'):
            (root / name).mkdir(parents=True, exist_ok=True)
        (root / 'project.json').write_text(json.dumps({
            'title': 'Image visibility regression', 'type': 'scene', 'file': 'scene.json'}))
        alpha = 128 if consumer else 255
        Image.new('RGBA', (32, 16), (255, 255, 255, alpha)).save(root / 'materials/source.png')
        Image.new('RGBA', (32, 16), (0, 255, 0, alpha)).save(root / 'materials/green.png')
        for texture in ('source', 'green'):
            (root / 'materials' / (texture + '.tex-json')).write_text(json.dumps({
                'format': 'rgba8888', 'nomip': True}))

        def material(name, texture, blending='normal'):
            (root / 'models' / (name + '.json')).write_text(json.dumps({
                'material': 'materials/' + name + '.json'}))
            (root / 'materials' / (name + '.json')).write_text(json.dumps({'passes': [{
                'shader': 'genericimage2', 'textures': [texture], 'blending': blending,
                'combos': {'VERSION': 1},
                'cullmode': 'nocull', 'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))

        material('source', 'source')
        material('consumer', 'green' if direct_control else '_rt_imageLayerComposite_1_a', 'translucent')
        material('copy', 'source')
        objects = []
        if not direct_control:
            objects.append({'id': 1, 'name': 'Source', 'image': 'models/source.json',
                            'origin': '64 128 0', 'size': '32 16', 'visible': visible,
                            'color': {'value': '1 0 0', 'script':
                                      'export function update(value) { return new Vec4(0,1,0,1); }'}})
        if consumer:
            objects.append({'id': 2, 'name': 'Consumer', 'image': 'models/consumer.json',
                            'origin': '192 128 0', 'size': '32 16',
                            'dependencies': [] if direct_control else [1]})
        if effect:
            # A named target in the middle of the effect graph must survive rerouting.
            (root / 'effects/chain.json').write_text(json.dumps({
                'name': 'Visibility target chain',
                'fbos': [{'name': '_rt_visibility_intermediate', 'format': 'rgba8888', 'scale': 1}],
                'passes': [
                    {'material': 'materials/copy.json', 'target': '_rt_visibility_intermediate',
                     'bind': [{'index': 0, 'name': 'previous'}]},
                    {'material': 'materials/copy.json',
                     'bind': [{'index': 0, 'name': '_rt_visibility_intermediate'}]},
                ]}))
            objects[0]['effects'] = [{'id': 10, 'file': 'effects/chain.json'}]
        (root / 'scene.json').write_text(json.dumps({
            'camera': {'eye': '128 128 128', 'center': '128 128 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 256, 'height': 256},
                        'clearcolor': '0 0 .25', 'bloom': False}, 'objects': objects}))
        return self.render(root)

    def render(self, root):
        from PIL import Image

        screenshot = root / 'frame.png'
        screenshot.unlink(missing_ok=True)
        log_path = root / 'engine.log'
        env = dict(os.environ, WPE_LOG_FILE='off',
                   WPE_CONTROL_SOCKET=str(Path(tempfile.gettempdir()) / f'lwe-visibility-{os.getpid()}.sock'),
                   WPE_HEALTH_REPORT=str(root / 'health.json'), XDG_STATE_HOME=str(root / 'state'))
        with log_path.open('w') as log:
            process = subprocess.Popen([
                str(Path(os.environ['LWE_TEST_BINARY']).resolve()), '--window', '0x0x256x256',
                '--silent', '--fps', '10', '--no-full-screen-pause', '--disable-mouse',
                '--screenshot', str(screenshot), '--screenshot-delay', '5', str(root)],
                env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 15
                while process.poll() is None and not screenshot.exists() and time.monotonic() < deadline:
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
        return Image.open(screenshot).convert('RGB')

    def test_initially_hidden_image_can_become_visible_with_effect_targets(self):
        from PIL import ImageChops

        with tempfile.TemporaryDirectory(prefix='lwe-visibility-show-') as directory:
            root = Path(directory)
            for effect in (False, True):
                with self.subTest(effect=effect):
                    control = self.capture(root / f'control-{effect}', True, effect=effect)
                    scripted = self.capture(root / f'scripted-{effect}', {
                        'value': False, 'script': 'export function init(value) { return true; }'}, effect=effect)
                    self.assertGreater(control.getpixel((64, 128))[1], 200)
                    self.assertLess(control.getpixel((64, 128))[0], 20)
                    self.assertLess(control.getpixel((64, 128))[2], 20)
                    self.assertIsNone(ImageChops.difference(control, scripted).getbbox())

    def test_hidden_layers_keep_current_rgba_for_texture_consumers(self):
        from PIL import ImageChops

        with tempfile.TemporaryDirectory(prefix='lwe-visibility-consumer-') as directory:
            root = Path(directory)
            control = self.capture(root / 'control', False, consumer=True, direct_control=True)
            for name, visible in [
                ('hidden', False),
                ('hidden-in-init', {'value': True, 'script': 'export function init(value) { return false; }'}),
                ('hidden-after-draw', {'value': True, 'script':
                    'let n=0; export function update(value) { return ++n <= 2; }'}),
            ]:
                with self.subTest(name=name):
                    actual = self.capture(root / name, visible, consumer=True)
                    self.assertIsNone(ImageChops.difference(control, actual).getbbox(),
                                      'Hiding the source must retain its updated RGBA composite texture')


    @unittest.skipUnless(os.environ.get('LWE_TEST_WORKSHOP'), 'Set LWE_TEST_WORKSHOP for installed puppet regression')
    def test_clipped_puppet_can_hide_and_show_again(self):
        from PIL import ImageChops

        package = Path(os.environ['LWE_TEST_WORKSHOP']) / '3558034522/scene.pkg'
        if not package.exists():
            self.skipTest('MyGO wallpaper 3558034522 is not installed')
        with tempfile.TemporaryDirectory(prefix='lwe-visibility-puppet-') as directory:
            root = Path(directory)
            frames = []
            for name, visible in [
                ('control', True),
                ('initially-hidden', {'value': False, 'script': 'export function init(value) { return true; }'}),
                ('hide-show', {'value': True, 'script':
                    "let n=0; export function update(value) { ++n; if(n===3) console.log('PUPPET_RESHOWN'); return n!==2; }"}),
            ]:
                folder = root / name
                folder.mkdir()
                (folder / 'scene.pkg').symlink_to(package)
                (folder / 'project.json').write_text(json.dumps({
                    'title': 'Installed puppet visibility regression', 'type': 'scene',
                    'file': 'visibility-puppet.json'}))
                (folder / 'visibility-puppet.json').write_text(json.dumps({
                    'camera': {'eye': '0 0 0', 'center': '0 0 -1', 'up': '0 1 0'},
                    'general': {'orthogonalprojection': {'width': 1024, 'height': 1024},
                                'clearcolor': '0 0 .25', 'bloom': False},
                    'objects': [{'id': 106, 'name': 'Installed clipped eyes',
                                 'image': 'models/13眼组.json', 'origin': '512 512 0',
                                 'size': '857 951', 'visible': visible,
                                 'animationlayers': [{'id': 216, 'animation': 211, 'rate': 0,
                                                      'blend': 1, 'visible': True}]}]}))
                frames.append(self.render(folder))
                output = (folder / 'engine.log').read_text()
                self.assertEqual(output.count('Loaded puppet clipping '), 1, output)
                if name == 'hide-show':
                    self.assertIn('PUPPET_RESHOWN', output)
            self.assertGreater(len(frames[0].getcolors(65536)), 1, 'Puppet must render visible geometry')
            for frame in frames[1:]:
                self.assertIsNone(ImageChops.difference(frames[0], frame).getbbox(),
                                  'Puppet clipping must survive both first show and hide/show')


if __name__ == '__main__':
    unittest.main()
