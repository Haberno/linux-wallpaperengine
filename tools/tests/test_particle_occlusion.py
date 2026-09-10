"""Opt-in regression for bubbles cut off at character silhouettes."""
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

from scene_render import render_scene


def quad_model(root, name, half_width, half_height, color, extra_blend=None, z=0):
    material = f'materials/{name}.json'
    vertices = [(-half_width, -half_height), (half_width, -half_height),
                (half_width, half_height), (-half_width, half_height)]
    data = bytearray(b'MDLV0023\0')
    data += struct.pack('<III', 15, 1, 1) + material.encode() + b'\0'
    data += struct.pack('<I6fII', 0, -half_width, -half_height, z,
                        half_width, half_height, z, 15, 4 * 48)
    for x, y in vertices:
        data += struct.pack('<12f', x, y, z, 0, 0, 1, 1, 0, 0, 1, 0, 0)
    data += struct.pack('<I6HBBI', 12, 0, 1, 2, 0, 2, 3, 0, 0, 0)
    (root / f'models/{name}.mdl').write_bytes(data)
    passes = [{
        'shader': 'generic4', 'textures': ['util/white'],
        'constantshadervalues': {'color': color}, 'combos': {'LIGHTING': 0, 'FOG': 0},
        'blending': 'normal', 'depthtest': 'enabled', 'depthwrite': 'enabled',
        'cullmode': 'nocull'}]
    if extra_blend:
        passes.append({**passes[0], 'blending': extra_blend, 'depthwrite': 'disabled',
                       'constantshadervalues': {'color': '0 0 0', 'alpha': 0}})
    (root / material).write_text(json.dumps({'passes': passes}))


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleOcclusion(unittest.TestCase):
    def test_bubbles_survive_over_open_water_but_respect_foreground_depth(self):
        with tempfile.TemporaryDirectory(prefix='lwe-particle-occlusion-') as directory:
            cases = [('in-front', 60, None), ('behind-character', 10, None),
                     ('mixed-translucent-background', 60, 'translucent'),
                     ('mixed-additive-background', 60, 'additive')]
            for name, z, extra_blend in cases:
                with self.subTest(name=name):
                    root = Path(directory) / name
                    for subdir in ('models', 'materials', 'particles'):
                        (root / subdir).mkdir(parents=True)
                    # Like the underwater environment, this model's origin can
                    # be closer than its actual walls. Its transparent material
                    # must not delay the opaque wall until after the bubble.
                    background_origin = 80 if extra_blend else 0
                    quad_model(root, 'background', 100, 100, '0 0 .2',
                               extra_blend, z=-background_origin)
                    quad_model(root, 'character', 5, 20, '0 .5 0')
                    (root / 'particles/bubble.json').write_text(json.dumps({
                        'maxcount': 1, 'material': 'materials/bubble.json',
                        'emitter': [{'name': 'sphererandom', 'rate': 10,
                                     'distancemin': 0, 'distancemax': 0}],
                        'initializer': [{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                                        {'name': 'sizerandom', 'min': 40, 'max': 40}],
                        'renderer': [{'name': 'sprite'}]}))
                    (root / 'materials/bubble.json').write_text(json.dumps({'passes': [{
                        'shader': 'genericparticle', 'textures': ['util/white'],
                        'blending': 'translucent' if extra_blend else 'additive',
                        'depthtest': 'enabled',
                        'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
                    frame, _ = render_scene(self, root, {
                        'camera': {'eye': '0 0 100', 'center': '0 0 0', 'up': '0 1 0'},
                        'general': {'orthogonalprojection': None, 'fov': 50,
                                    'clearcolor': '0 0 0', 'bloom': False,
                                    'transparentsorting': True},
                        'objects': [
                            {'id': 1, 'name': 'Character', 'model': 'models/character.mdl', 'origin': '0 0 30'},
                            {'id': 2, 'name': 'Bubble', 'particle': 'particles/bubble.json', 'origin': f'0 0 {z}'},
                            {'id': 3, 'name': 'Water', 'model': 'models/background.mdl',
                             'origin': f'0 0 {background_origin}'}]})
                    # Red is zero in both model materials and white in the bubble.
                    row = [frame.getpixel((x, 90))[0] for x in range(320)]
                    # Both samples sit beyond the character's silhouette, inside
                    # the bubble even when perspective makes the rear one smaller.
                    for x in (142, 177):
                        self.assertGreater(row[x], 200,
                                           'Background geometry must not erase the bubble in open water')
                    if name == 'behind-character':
                        self.assertLess(row[160], 20, 'A closer character must still occlude the bubble')
                    else:
                        self.assertGreater(row[160], 200)


if __name__ == '__main__':
    unittest.main()
