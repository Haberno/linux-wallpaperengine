"""Opt-in hidden text source and dynamic-size composite regressions."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class TextComposite(unittest.TestCase):
    def test_hidden_text_publishes_the_final_result_after_resizing(self):
        from PIL import ImageChops

        with tempfile.TemporaryDirectory(prefix='lwe-text-source-') as directory:
            for with_effect in (False, True):
                images = []
                for dynamic in (False, True):
                    root = Path(directory) / f'effect-{with_effect}-dynamic-{dynamic}'
                    write_copy_assets(root)
                    (root / 'models/consumer.json').write_text(json.dumps({'material': 'materials/consumer.json'}))
                    (root / 'materials/consumer.json').write_text(json.dumps({'passes': [{
                        'shader': 'copy', 'textures': ['_rt_imageLayerComposite_1_a'],
                        'blending': 'normal', 'cullmode': 'nocull',
                        'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
                    source = {'id': 1, 'name': 'Clock source', 'text': 'WWWW', 'pointsize': 24,
                              'visible': False, 'origin': '160 90 0'}
                    if with_effect:
                        source['effects'] = [{'id': 3, 'file': 'effects/opacity/effect.json',
                                              'passes': [{'constantshadervalues': {'alpha': .4}}]}]
                    if dynamic:
                        source['text'] = {'value': 'I', 'script': '''
let n = 0;
export function update() { return ++n < 3 ? 'I' : 'WWWW'; }
'''}
                    image, _ = render_scene(self, root, base_scene([source, {
                        'id': 2, 'name': 'Clock display', 'image': 'models/consumer.json',
                        'origin': '160 90 0', 'size': '260 100', 'dependencies': [1]}]))
                    images.append(image)
                self.assertIsNotNone(images[0].getbbox(), 'Hidden source text must render in its consumer')
                self.assertIsNone(ImageChops.difference(*images).getbbox(),
                                  'Consumers must follow the text result through glyph-size changes')


if __name__ == '__main__':
    unittest.main()
