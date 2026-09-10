"""Opt-in text effect scripts, including changing glyphs during an opacity fade."""
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class TextEffectScripts(unittest.TestCase):
    def test_scripted_opacity_survives_glyph_rebuilds(self):
        from PIL import ImageChops

        with tempfile.TemporaryDirectory(prefix='lwe-text-fx-') as directory:
            root = Path(directory)
            images = []
            for scripted in (False, True):
                scene = {
                    'camera': {'eye': '160 90 100', 'center': '160 90 0', 'up': '0 1 0'},
                    'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                                'clearcolor': '0 0 0', 'bloom': False},
                    'objects': [{'id': 1, 'name': 'Changing clock', 'origin': '160 90 0',
                                 'pointsize': 24, 'color': '1 1 1', 'text': 'WWW',
                                 'effects': [{'file': 'effects/opacity/effect.json', 'id': 2,
                                              'passes': [{'constantshadervalues': {'alpha': .4}}]}]}]}
                if scripted:
                    scene['objects'][0]['text'] = {'value': 'I', 'script': '''
let n = 0;
export function update() { return ++n < 3 ? 'III' : 'WWW'; }
'''}
                    scene['objects'][0]['effects'][0]['passes'][0]['constantshadervalues']['alpha'] = {
                        'value': 1, 'script': '''
let n = 0;
export function init() { console.log('TEXT_EFFECT_INIT'); return 0; }
export function update() {
    console.log('TEXT_EFFECT_UPDATE');
    return ++n < 3 ? 0 : .4;
}
'''}
                image, log = render_scene(self, root / ('scripted' if scripted else 'static'), scene)
                images.append(image)
                if scripted:
                    self.assertEqual(log.count('TEXT_EFFECT_INIT'), 1, log)
                    self.assertGreaterEqual(log.count('TEXT_EFFECT_UPDATE'), 5, log)
            self.assertIsNotNone(images[0].getbbox(), 'The reference glyphs must be visible')
            self.assertIsNone(ImageChops.difference(*images).getbbox(),
                              'The scripted opacity must match the static value after glyph changes')


if __name__ == '__main__':
    unittest.main()
