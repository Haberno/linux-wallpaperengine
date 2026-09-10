"""Opt-in rendered 3D image alignment under changes in scale."""
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ImageAlignment(unittest.TestCase):
    def test_aligned_edge_stays_fixed_when_scaling(self):
        with tempfile.TemporaryDirectory(prefix='lwe-align-') as directory:
            root = Path(directory)
            for alignment, edge in (('left', 0), ('right', 2), ('top', 1), ('bottom', 3)):
                bounds = []
                for scale in (1, 2):
                    image, _ = render_scene(self, root / f'{alignment}-{scale}', {
                        'camera': {'eye': '0 0 100', 'center': '0 0 0', 'up': '0 1 0'},
                        'general': {'orthogonalprojection': None, 'fov': 50,
                                    'clearcolor': '0 0 0', 'bloom': False},
                        'objects': [{'id': 1, 'name': 'Bar', 'image': 'models/util/solidlayer.json',
                                     'alignment': alignment, 'origin': '0 0 0', 'size': '20 8',
                                     'scale': f'{scale} {scale} 1', 'color': '1 1 1'}]})
                    box = image.point(lambda value: 255 if value > 50 else 0).getbbox()
                    self.assertIsNotNone(box, f'{alignment}: bar must be visible')
                    bounds.append(box)
                self.assertLessEqual(abs(bounds[0][edge] - bounds[1][edge]), 1,
                                     f'{alignment} edge moved: {bounds}')
                opposite = (edge + 2) % 4
                self.assertGreater(abs(bounds[0][opposite] - bounds[1][opposite]), 8,
                                   f'{alignment}: bar must grow away from the anchor')


if __name__ == '__main__':
    unittest.main()
