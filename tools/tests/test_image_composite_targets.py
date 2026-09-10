"""Opt-in image-layer consumers must sample the completed effect chain."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ImageCompositeTargets(unittest.TestCase):
    def test_hidden_composite_contains_every_effect_regardless_of_pass_count(self):
        with tempfile.TemporaryDirectory(prefix='lwe-image-composite-') as directory:
            for count, targeted, hidden_parent in ((0, False, False), (1, False, False),
                                                   (2, False, False), (3, False, False),
                                                   (1, True, False), (1, False, True)):
                with self.subTest(effect_count=count, targeted=targeted, hidden_parent=hidden_parent):
                    root = Path(directory) / f'effects-{count}-{targeted}-{hidden_parent}'
                    write_copy_assets(root)
                    (root / 'shaders/reduce.vert').write_text((root / 'shaders/copy.vert').read_text())
                    (root / 'shaders/reduce.frag').write_text('''
uniform sampler2D g_Texture0;
varying vec2 v_TexCoord;
void main() {
    gl_FragColor = texSample2D(g_Texture0, v_TexCoord) * vec4(0.5, 1.0, 1.0, 1.0);
}
''')
                    for name, texture, shader in (('source', 'util/white', 'copy'),
                                                   ('consumer', '_rt_imageLayerComposite_1_a', 'copy'),
                                                   ('reduce', None, 'reduce')):
                        material = {'passes': [{'shader': shader, 'blending': 'normal',
                                               'depthtest': 'disabled', 'depthwrite': 'disabled',
                                               'cullmode': 'nocull'}]}
                        if texture:
                            material['passes'][0]['textures'] = [texture]
                        (root / f'materials/{name}.json').write_text(json.dumps(material))
                        (root / f'models/{name}.json').write_text(json.dumps({
                            'material': f'materials/{name}.json', 'width': 64, 'height': 64}))
                    passes = [{'material': 'materials/clear.json', 'target': '_rt_scratch'}] if targeted else []
                    reduction = {'material': 'materials/reduce.json'}
                    if targeted:
                        reduction['bind'] = [{'index': 0, 'name': 'previous'}]
                    passes.append(reduction)
                    (root / 'effects/reduce.json').write_text(json.dumps({
                        'fbos': [{'name': '_rt_scratch', 'scale': 1, 'format': 'rgba8888'}],
                        'passes': passes}))
                    objects = [
                        {'id': 3, 'name': 'Parent', 'solid': True, 'visible': not hidden_parent},
                        {'id': 1, 'name': 'Hidden source', 'image': 'models/source.json',
                         'origin': '160 90 0', 'visible': hidden_parent, 'parent': 3,
                         'effects': [{'id': 10 + i, 'file': 'effects/reduce.json'} for i in range(count)]},
                        {'id': 2, 'name': 'Consumer', 'image': 'models/consumer.json', 'origin': '160 90 0'}]
                    frame, _ = render_scene(self, root, base_scene(objects))
                    red, green, blue = frame.getpixel((160, 90))
                    self.assertAlmostEqual(red, 255 * .5 ** count, delta=2)
                    self.assertGreater(green, 250)
                    self.assertGreater(blue, 250)


if __name__ == '__main__':
    unittest.main()
