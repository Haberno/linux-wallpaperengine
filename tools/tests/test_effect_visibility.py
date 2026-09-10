"""Opt-in scripted effect routing, including initially hidden and named-target passes."""
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class EffectVisibility(unittest.TestCase):
    def test_hidden_broken_effect_is_lazy_and_cannot_remove_its_image(self):
        from PIL import Image

        for activate in (False, True):
            with self.subTest(activate=activate), tempfile.TemporaryDirectory(prefix='lwe-hidden-effect-') as directory:
                root = Path(directory)
                write_copy_assets(root)
                Image.new('RGBA', (32, 32), (0, 0, 255, 255)).save(root / 'materials/source.png')
                (root / 'materials/source.tex-json').write_text(json.dumps({'format': 'rgba8888', 'nomip': True}))
                (root / 'models/source.json').write_text(json.dumps({'material': 'materials/source.json'}))
                (root / 'materials/source.json').write_text(json.dumps({'passes': [{
                    'shader': 'genericimage2', 'textures': ['source'], 'blending': 'normal',
                    'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
                for name, source in [('red', 'gl_FragColor = vec4(1,0,0,1);'), ('broken', 'invalid shader;')]:
                    (root / f'shaders/{name}.vert').write_text((root / 'shaders/copy.vert').read_text())
                    (root / f'shaders/{name}.frag').write_text('void main() { ' + source + ' }')
                    (root / f'materials/{name}.json').write_text(json.dumps({'passes': [{
                        'shader': name, 'blending': 'normal', 'depthtest': 'disabled',
                        'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
                (root / 'effects/optional.json').write_text(json.dumps({'passes': [
                    {'material': 'materials/red.json'}, {'material': 'materials/broken.json'}]}))
                script = 'let n=0; export function update(value) { return ' + ('++n > 3' if activate else 'false') + '; }'
                objects = [{'id': 1, 'image': 'models/source.json', 'origin': '160 90 0', 'size': '120 100',
                            'effects': [{'id': 2, 'file': 'effects/optional.json',
                                         'visible': {'value': False, 'script': script}}]}]
                dumps = root / 'shader-dumps'
                with mock.patch.dict(os.environ, {'WPE_DUMP_SHADERS': str(dumps)}):
                    image, output = render_scene(self, root, base_scene(objects), frames=10)
                self.assertEqual(image.getpixel((160, 90)), (0, 0, 255), 'Bypass the whole failed effect')
                self.assertEqual(output.count('Disabling effect on object 1 after shader setup failed'), int(activate))
                self.assertEqual(bool(list(dumps.glob('broken.*.frag'))), activate)
                self.assertNotIn('Failed to setup object 1:', output)

    def test_switching_effects_reroutes_the_complete_chain(self):
        from PIL import Image

        with tempfile.TemporaryDirectory(prefix='lwe-effect-visibility-') as directory:
            root = Path(directory)
            write_copy_assets(root)
            Image.new('RGBA', (32, 32), (0, 0, 255, 255)).save(root / 'materials/source.png')
            (root / 'materials/source.tex-json').write_text(json.dumps({'format': 'rgba8888', 'nomip': True}))
            (root / 'models/source.json').write_text(json.dumps({'material': 'materials/source.json'}))
            (root / 'materials/source.json').write_text(json.dumps({'passes': [{
                'shader': 'genericimage2', 'textures': ['source'], 'blending': 'normal',
                'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
            for name, color in [('red', '1,0,0,1'), ('green', '0,1,0,1')]:
                (root / f'shaders/{name}.vert').write_text((root / 'shaders/copy.vert').read_text())
                (root / f'shaders/{name}.frag').write_text('void main() { gl_FragColor = vec4(' + color + '); }')
                (root / f'materials/{name}.json').write_text(json.dumps({'passes': [{
                    'shader': name, 'blending': 'normal', 'depthtest': 'disabled',
                    'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
            (root / 'effects/red.json').write_text(json.dumps({
                'passes': [{'material': 'materials/red.json'}]}))
            (root / 'effects/green.json').write_text(json.dumps({
                'fbos': [{'name': '_rt_green', 'format': 'rgba8888', 'scale': 1}],
                'passes': [{'material': 'materials/green.json', 'target': '_rt_green'},
                           {'material': 'materials/copy.json', 'bind': [{'index': 0, 'name': '_rt_green'}]}]}))
            (root / 'effects/copy.json').write_text(json.dumps({
                'passes': [{'material': 'materials/copy.json'}]}))
            objects = [{'id': 1, 'name': 'Switched image', 'image': 'models/source.json',
                        'origin': '160 90 0', 'size': '120 100', 'effects': [
                            {'id': 2, 'file': 'effects/red.json', 'visible': {'value': False, 'script':
                                'let n=0; export function update(value) { return (++n % 3) === 1; }'}},
                            {'id': 3, 'file': 'effects/green.json', 'visible': {'value': True, 'script':
                                'let n=0; export function update(value) { return (++n % 3) === 2; }'}},
                            {'id': 4, 'file': 'effects/copy.json'}]}]
            # All three states recur: each run has already enabled and disabled both effects.
            for frames, expected in [(6, (255, 0, 0)), (7, (0, 255, 0)), (8, (0, 0, 255))]:
                with self.subTest(frames=frames):
                    (root / 'frame.png').unlink(missing_ok=True)
                    image, _ = render_scene(self, root, base_scene(objects), frames=frames)
                    for pixel in [(110, 60), (160, 90), (210, 120)]:
                        self.assertEqual(image.getpixel(pixel), expected)
                    self.assertEqual(image.getpixel((20, 20)), (0, 0, 0), 'Scene geometry must remain fixed')


if __name__ == '__main__':
    unittest.main()
