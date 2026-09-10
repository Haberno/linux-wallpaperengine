"""Opt-in regression for parented effects cutting holes in additive backgrounds."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_occlusion import quad_model


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ModelTransparentSorting(unittest.TestCase):
    def test_parented_boost_uses_layer_origin_for_sorting(self):
        for alpha in (0, .5):
            with self.subTest(alpha=alpha), tempfile.TemporaryDirectory(prefix='lwe-model-sort-') as directory:
                root = Path(directory)
                (root / 'models').mkdir()
                (root / 'materials').mkdir()
                # The skybox origin is near the camera; its geometry is far away.
                # The boost's local origin sorts after the skybox, but its parent
                # puts its world origin farther away, reversing the old ordering.
                quad_model(root, 'skybox', 300, 300, '0 0 .5', z=-200)
                quad_model(root, 'boost', 30, 30, '.5 0 0')
                for name, opacity in (('skybox', 1), ('boost', alpha)):
                    path = root / f'materials/{name}.json'
                    material = json.loads(path.read_text())
                    material['passes'][0]['blending'] = 'additive'
                    material['passes'][0]['constantshadervalues']['alpha'] = opacity
                    path.write_text(json.dumps(material))
                frame, _ = render_scene(self, root, {
                    'camera': {'eye': '0 0 100', 'center': '0 0 0', 'up': '0 1 0'},
                    'general': {'orthogonalprojection': None, 'fov': 50,
                                'clearcolor': '0 0 0', 'transparentsorting': True},
                    'objects': [
                        {'id': 1, 'name': 'Skybox', 'model': 'models/skybox.mdl'},
                        {'id': 2, 'name': 'Sonic', 'solid': True, 'origin': '0 0 -60'},
                        {'id': 3, 'name': 'Boost', 'model': 'models/boost.mdl',
                         'parent': 2, 'origin': '0 0 20'}]})
                red, _, blue = frame.getpixel((160, 90))
                self.assertGreater(blue, 100, 'Boost depth must not cut a hole in the skybox')
                if alpha:
                    self.assertGreater(red, 40, 'The visible boost must still add its light')
                else:
                    self.assertLess(red, 5, 'The idle boost must remain invisible')


if __name__ == '__main__':
    unittest.main()
