"""Continuously emitted one-particle glows must not blink on lifetime expiry."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleRecycling(unittest.TestCase):
    def test_full_emitters_replace_expired_particles_in_the_same_frame(self):
        with tempfile.TemporaryDirectory(prefix='lwe-particle-recycle-') as directory:
            root = Path(directory)
            (root / 'particles').mkdir()
            (root / 'materials').mkdir()
            (root / 'materials/glow.json').write_text(json.dumps({'passes': [{
                'shader': 'genericparticle', 'textures': ['util/white'], 'blending': 'translucent',
                'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
            objects = []
            for index in range(12):
                # Stagger expirations so every sampled frame exercises replacement.
                lifetime = (index + 1) / 10
                (root / f'particles/glow{index}.json').write_text(json.dumps({
                    'maxcount': 1, 'material': 'materials/glow.json',
                    'emitter': [{'name': 'sphererandom' if index % 2 else 'boxrandom',
                                 'rate': 500, 'distancemin': 0, 'distancemax': 0}],
                    'initializer': [{'name': 'lifetimerandom', 'min': lifetime, 'max': lifetime},
                                    {'name': 'sizerandom', 'min': 16, 'max': 16}]}))
                objects.append({'id': index + 1, 'name': f'Glow {index}',
                                'particle': f'particles/glow{index}.json',
                                'origin': f'{40 + 80 * (index % 4)} {30 + 60 * (index // 4)} 0'})
            for frames in (15, 16):
                with self.subTest(frames=frames):
                    (root / 'frame.png').unlink(missing_ok=True)
                    frame, _ = render_scene(self, root, base_scene(objects), frames=frames)
                    for index in range(12):
                        pixel = (40 + 80 * (index % 4), 150 - 60 * (index // 4))
                        self.assertGreater(min(frame.getpixel(pixel)), 240, f'Glow {index} blinked')


if __name__ == '__main__':
    unittest.main()
