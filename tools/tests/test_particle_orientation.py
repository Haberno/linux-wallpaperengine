"""Opt-in fixed particle plane, authored axis, and orientation flag regression."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleOrientation(unittest.TestCase):
    def render_plane(self, root, renderer, angles='0 0 0', orthographic=False):
        (root / 'particles').mkdir(parents=True)
        (root / 'materials').mkdir()
        (root / 'particles/probe.json').write_text(json.dumps({
            'maxcount': 1, 'material': 'materials/probe.json',
            'emitter': [{'name': 'sphererandom', 'rate': 10,
                         'distancemin': 0, 'distancemax': 0}],
            'initializer': [{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                            {'name': 'sizerandom', 'min': 20, 'max': 20},
                            {'name': 'velocityrandom', 'min': '0 0 0', 'max': '0 0 0'}],
            'renderer': [{'name': 'sprite', **renderer}]}))
        (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
            'shader': 'genericparticle',
            'textures': ['particle/light/light_shafts_0' if orthographic else 'util/white'],
            'blending': 'additive' if orthographic else 'normal', 'cullmode': 'nocull',
            'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
        image, _ = render_scene(self, root, {
            'camera': {'eye': '0 0 1' if orthographic else '0 30 100',
                       'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180} if orthographic else None,
                        'fov': 50,
                        'clearcolor': '0 0 0', 'bloom': False},
            'objects': [
                {'id': 1, 'name': 'Parent', 'solid': True, 'angles': angles},
                {'id': 2, 'name': 'Fixed plane', 'parent': 1,
                 'particle': 'particles/probe.json',
                 'origin': '160 90 0' if orthographic else '0 0 0'}]})
        if orthographic:
            self.assertIsNotNone(image.getbbox())
            return image
        box = image.point(lambda value: 255 if value > 50 else 0).getbbox()
        self.assertIsNotNone(box, 'The particle plane must be visible')
        self.assertGreater(box[2] - box[0], 10)
        return (box[3] - box[1]) / (box[2] - box[0])

    def test_fixed_default_lies_in_depth_instead_of_facing_camera(self):
        with tempfile.TemporaryDirectory(prefix='lwe-orientation-') as directory:
            root = Path(directory)
            screen = self.render_plane(root / 'screen', {})
            fixed = self.render_plane(root / 'fixed', {'orientation': 'fixed'})
            self.assertAlmostEqual(screen, 1, delta=.06)
            self.assertLess(fixed, .4, 'Fixed default uses the XZ plane, not a screen billboard')

    def test_fixed_axis_changes_the_particle_plane(self):
        with tempfile.TemporaryDirectory(prefix='lwe-orientation-') as directory:
            root = Path(directory)
            facing = self.render_plane(root / 'axis-z', {'orientation': 'fixed', 'axis': '0 0 5'})
            fallback = self.render_plane(root / 'axis-zero', {'orientation': 'fixed', 'axis': '0 0 0'})
            self.assertGreater(facing, .85)
            self.assertLess(fallback, .4, 'Zero axis must recover the native Y-axis default')

    def test_fixed_orientation_flag_controls_parent_rotation(self):
        with tempfile.TemporaryDirectory(prefix='lwe-orientation-') as directory:
            root = Path(directory)
            local = self.render_plane(root / 'local', {'orientation': 'fixed'}, '1.5707963 0 0')
            world = self.render_plane(root / 'world', {'orientation': 'fixed', 'flags': 1}, '1.5707963 0 0')
            self.assertGreater(local, .85, 'The default fixed plane follows its parent rotation')
            self.assertLess(world, .4, 'Orientation flag 1 keeps the plane fixed in world space')

    def test_orthographic_axis_z_preserves_texture_direction(self):
        from PIL import Image, ImageChops

        with tempfile.TemporaryDirectory(prefix='lwe-orientation-') as directory:
            root = Path(directory)
            # The native draw helper gives both modes the same basis for an
            # unrotated orthographic layer. An asymmetric texture catches Y flips.
            screen = self.render_plane(root / 'ortho-screen', {}, orthographic=True)
            fixed = self.render_plane(root / 'ortho-fixed',
                                      {'orientation': 'fixed', 'axis': '0 0 1'}, orthographic=True)
            self.assertIsNotNone(ImageChops.difference(
                screen, screen.transpose(Image.Transpose.FLIP_TOP_BOTTOM)).getbbox())
            self.assertEqual(screen.tobytes(), fixed.tobytes())


if __name__ == '__main__':
    unittest.main()
