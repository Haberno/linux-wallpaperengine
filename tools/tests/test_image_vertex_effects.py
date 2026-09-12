"""Vertex effects deform source pixels before the layer's scene transform."""
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


def fixture(root, scale=(1.5, .5), angle=0, size=64):
    write_copy_assets(root)
    pixels = bytes((255, 255, 255, 255)) * 64 * 64
    (root / 'materials/white.tex').write_bytes(
        b'TEXV0005\0TEXI0001\0' + struct.pack('<7I', 0, 2, 64, 64, 64, 64, 0)
        + b'TEXB0001\0' + struct.pack('<5I', 1, 1, 64, 64, len(pixels)) + pixels)
    (root / 'models/probe.json').write_text(json.dumps({'material': 'materials/base.json'}))
    (root / 'materials/base.json').write_text(json.dumps({'passes': [{
        'shader': 'copy', 'textures': ['white'], 'blending': 'normal',
        'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
    scene = base_scene([{'id': 1, 'image': 'models/probe.json', 'origin': '160 90 0',
        'size': f'{size} {size}', 'scale': f'{scale[0]} {scale[1]} 1', 'angles': f'0 0 {angle}',
        'effects': [{'id': 2, 'file': 'effects/skew/effect.json', 'passes': [{
            'combos': {'MODE': 1}, 'constantshadervalues': {
                'top': .25, 'bottom': .25, 'left': -.375, 'right': -.375}}]}]}])
    scene['general'].update(camerafade=False, hdr=False)
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ImageVertexEffects(unittest.TestCase):
    def test_skew_offsets_follow_layer_scale_rotation_and_stretch(self):
        with tempfile.TemporaryDirectory(prefix='lwe-vertex-') as directory:
            for index, (scale, angle, size) in enumerate([
                    ((1.5, .5), 0, 64), ((1.5, .5), math.pi / 2, 64),
                    ((.75, .5), 0, 128)]):
                with self.subTest(scale=scale, angle=angle, size=size):
                    root = Path(directory) / str(index)
                    frame, _ = render_scene(self, root, fixture(root, scale, angle, size))
                    bounds = frame.convert('L').point(lambda value: 255 if value >= 128 else 0).getbbox()
                    self.assertIsNotNone(bounds)
                    # Native keeps the source's 64-pixel offsets when size stretches
                    # the quad, then applies authored layer scale and rotation.
                    x, y = 64 * .25 * scale[0], 64 * -.375 * scale[1]
                    expected = (160 + math.cos(angle) * x - math.sin(angle) * y,
                                90 - math.sin(angle) * x - math.cos(angle) * y)
                    self.assertAlmostEqual((bounds[0] + bounds[2]) / 2, expected[0], delta=1)
                    self.assertAlmostEqual((bounds[1] + bounds[3]) / 2, expected[1], delta=1)


if __name__ == '__main__':
    unittest.main()
