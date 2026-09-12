"""Image FOG does not implicitly opt into the native FOG_COMPUTED branch."""
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

from scene_render import render_scene


def fog_scene(root, *, composite, visible, effect, lighting=False, fog=True, computed=None,
              model=False, solid=False, perspective=False):
    for folder in ('models', 'materials'):
        (root / folder).mkdir(parents=True, exist_ok=True)
    for name, texture, combos in (
            ('source', 'util/white', {'LIGHTING': int(lighting), 'REFLECTION': 0}),
            ('consumer', '_rt_imageLayerComposite_10_a', {'FOG': 0})):
        if name == 'source':
            if fog is not None:
                combos['FOG'] = int(fog)
            if computed is not None:
                combos['FOG_COMPUTED'] = int(computed)
        (root / f'models/{name}.json').write_text(json.dumps({
            'material': f'materials/{name}.json', 'solidlayer': solid}))
        (root / f'materials/{name}.json').write_text(json.dumps({'passes': [{
            'shader': 'generic4' if model and name == 'source' else 'genericimage4',
            'textures': [texture], 'combos': combos,
            'blending': 'normal', 'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
    source = {'id': 10, 'name': 'source', 'image': 'models/source.json', 'origin': '0 0 0',
              'size': '256 256', 'scale': '0.015625 0.015625 0.015625', 'visible': visible,
              'perspective': perspective}
    if model:
        vertices = b''.join(struct.pack('<12f', x, y, 0, 0, 0, 1, 1, 0, 0, 1, u, v)
                            for x, y, u, v in ((-2, -2, 0, 1), (2, -2, 1, 1),
                                               (2, 2, 1, 0), (-2, 2, 0, 0)))
        indices = struct.pack('<6H', 0, 1, 2, 0, 2, 3)
        (root / 'models/source.mdl').write_bytes(
            b'MDLV0016\0' + struct.pack('<3I', 15, 1, 1) + b'materials/source.json\0'
            + struct.pack('<3I', 0, 15, len(vertices)) + vertices
            + struct.pack('<I', len(indices)) + indices + b'\0')
        source = {'id': 10, 'name': 'model fog control', 'model': 'models/source.mdl',
                  'origin': '0 0 0'}
    if effect:
        source['effects'] = [{'id': 40, 'file': 'effects/scroll/effect.json', 'passes': [
            {'constantshadervalues': {'speedx': 0, 'speedy': 0}}]}]
    objects = [source]
    if composite:
        objects.append({'id': 11, 'name': 'consumer', 'image': 'models/consumer.json',
                        'origin': '0 0 0', 'size': '4 4', 'dependencies': [10]})
    objects.append({'id': 99, 'name': 'camera', 'camera': 'default', 'origin': '0 0 10',
                    'angles': '0 0 0', 'fov': 50})
    return {'camera': {'eye': '0 0 10', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': None, 'nearz': .1, 'farz': 100, 'fov': 50,
                        'clearcolor': '0 0 0', 'camerafade': False, 'bloom': False, 'hdr': False,
                        'ambientcolor': '1 1 1', 'skylightcolor': '0 0 0',
                        'fogdistance': True, 'fogdistancecolor': '0 1 0',
                        'fogdistancestart': 0, 'fogdistanceend': 1,
                        'fogdistancestartdensity': 1, 'fogdistanceenddensity': 1},
            'objects': objects}


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ImageCompositeFog(unittest.TestCase):
    def test_image_fog_requires_the_separate_computed_combo(self):
        # Full green distance fog makes any accidental bake into the white
        # exported texture visible through a consumer whose material opts out.
        for name, settings, expected in (
                ('hidden', dict(composite=True, visible=False, effect=True), (255, 255, 255)),
                ('hidden-default-fog', dict(composite=True, visible=False, effect=True, fog=None), (255, 255, 255)),
                ('visible', dict(composite=True, visible=True, effect=True), (255, 255, 255)),
                ('lit-hidden', dict(composite=True, visible=False, effect=True, lighting=True), (255, 255, 255)),
                ('direct', dict(composite=False, visible=True, effect=False), (255, 255, 255)),
                ('effect', dict(composite=False, visible=True, effect=True), (255, 255, 255)),
                ('lit-effect', dict(composite=False, visible=True, effect=True, lighting=True), (255, 255, 255)),
                ('solid-effect', dict(composite=False, visible=True, effect=True, solid=True), (255, 255, 255)),
                ('perspective-effect', dict(composite=False, visible=True, effect=True, perspective=True), (255, 255, 255)),
                ('effect-opt-out', dict(composite=False, visible=True, effect=True, fog=False), (255, 255, 255)),
                ('explicit-computed', dict(composite=False, visible=True, effect=False, computed=True), (0, 255, 0)),
                ('explicit-computed-off', dict(composite=False, visible=True, effect=False, computed=False), (255, 255, 255)),
                ('model', dict(composite=False, visible=True, effect=False, model=True), (0, 255, 0))):
            with self.subTest(name=name), tempfile.TemporaryDirectory(prefix='lwe-composite-fog-') as directory:
                root = Path(directory)
                frame, _ = render_scene(self, root, fog_scene(root, **settings))
                actual = frame.getpixel((160, 90))
                self.assertLessEqual(max(abs(a - b) for a, b in zip(actual, expected)), 1,
                                     f'{name}: expected {expected}, got {actual}')


if __name__ == '__main__':
    unittest.main()
