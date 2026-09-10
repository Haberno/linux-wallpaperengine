"""Native text brightness applies only when HDR bloom is enabled."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class TextBrightness(unittest.TestCase):
    def test_zero_brightness_keeps_ldr_text_visible_with_and_without_effects(self):
        with tempfile.TemporaryDirectory(prefix='lwe-text-brightness-') as directory:
            for hdr, bloom in [(False, False), (False, True), (True, False), (True, True)]:
                for effect in (False, True):
                    with self.subTest(hdr=hdr, bloom=bloom, effect=effect):
                        root = Path(directory) / f'{hdr}-{bloom}-{effect}'
                        write_copy_assets(root)
                        (root / 'effects/copy.json').write_text(json.dumps({
                            'passes': [{'material': 'materials/copy.json'}]}))
                        text = {'id': 1, 'name': 'Clock', 'text': '12:34', 'pointsize': 40,
                                'origin': '160 90 0', 'brightness': 0}
                        if effect:
                            text['effects'] = [{'id': 2, 'file': 'effects/copy.json'}]
                        scene = base_scene([text])
                        scene['general'].update(hdr=hdr, bloom=bloom, camerafade=False)
                        frame, _ = render_scene(self, root, scene)
                        if hdr and bloom:
                            self.assertIsNone(frame.getbbox())
                        else:
                            self.assertGreater(max(frame.getdata())[0], 240)


if __name__ == '__main__':
    unittest.main()
