"""Resizing an image must scale its pixel-based effects along with its texture."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ImageEffectPixels(unittest.TestCase):
    def test_effect_pixel_offsets_follow_source_pixels_on_a_stretched_layer(self):
        from PIL import Image

        with tempfile.TemporaryDirectory(prefix='lwe-image-pixels-') as directory:
            root = Path(directory)
            write_copy_assets(root)
            Image.new('RGBA', (32, 64), (0, 0, 0, 255)).save(root / 'materials/source.png')
            (root / 'materials/source.tex-json').write_text(json.dumps({'format': 'rgba8888', 'nomip': True}))
            (root / 'models/probe.json').write_text(json.dumps({'material': 'materials/base.json'}))
            material = {'passes': [{'shader': 'copy', 'textures': ['source'],
                                   'blending': 'normal', 'depthtest': 'disabled',
                                   'depthwrite': 'disabled', 'cullmode': 'nocull'}]}
            (root / 'materials/base.json').write_text(json.dumps(material))
            (root / 'shaders/pixels.vert').write_text((root / 'shaders/copy.vert').read_text())
            (root / 'shaders/pixels.frag').write_text('''
uniform vec4 g_Texture0Resolution;
varying vec2 v_TexCoord;
void main() {
    vec2 pixel = v_TexCoord * g_Texture0Resolution.zw;
    float mark = float(pixel.x >= 8.0 && pixel.x < 16.0 && pixel.y >= 16.0 && pixel.y < 32.0);
    gl_FragColor = vec4(mark, mark, mark, 1.0);
}
''')
            material['passes'][0].update(shader='pixels', textures=[])
            (root / 'materials/pixels.json').write_text(json.dumps(material))
            (root / 'effects/pixels.json').write_text(json.dumps({'passes': [
                {'material': 'materials/pixels.json'}, {'material': 'materials/copy.json'}]}))
            for size in (64, 128):
                with self.subTest(size=size):
                    (root / 'frame.png').unlink(missing_ok=True)
                    scene = base_scene([{'id': 1, 'name': 'Stretched image', 'image': 'models/probe.json',
                                         'origin': '160 90 0', 'size': f'{size} {size}',
                                         'effects': [{'id': 2, 'file': 'effects/pixels.json'}]}])
                    scene['general']['camerafade'] = False
                    frame, _ = render_scene(self, root, scene)
                    # Ignore the linear sampler's fringe around enlarged texels.
                    box = frame.convert('L').point(lambda value: 255 if value >= 128 else 0).getbbox()
                    self.assertIsNotNone(box)
                    self.assertAlmostEqual(box[2] - box[0], size / 4, delta=2)
                    self.assertAlmostEqual(box[3] - box[1], size / 4, delta=2)
                    self.assertAlmostEqual(box[0], 160 - size / 4, delta=2)


if __name__ == '__main__':
    unittest.main()
