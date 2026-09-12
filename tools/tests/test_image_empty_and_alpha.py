"""Empty helper layers stay invisible; offscreen image chains retain alpha."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ImageEmptyAndAlpha(unittest.TestCase):
    def test_zero_size_helper_does_not_cover_its_visible_child(self):
        with tempfile.TemporaryDirectory(prefix='lwe-empty-helper-') as directory:
            root = Path(directory)
            scene = base_scene([
                {'id': 1, 'name': 'Mover', 'image': 'models/util/solidlayer.json',
                 'size': '0 0', 'origin': '160 90 0'},
                {'id': 2, 'name': 'Child', 'image': 'models/util/solidlayer.json',
                 'size': '32 32', 'origin': '0 0 0', 'parent': 1, 'color': '0 1 0'}])
            scene['general'].update(clearcolor='0 0 1', camerafade=False)
            frame, _ = render_scene(self, root, scene)
            self.assertEqual(frame.getpixel((160, 90)), (0, 255, 0))
            self.assertEqual(frame.getpixel((110, 90)), (0, 0, 255))

    def test_hidden_effect_chain_replaces_alpha_in_reused_pingpong_target(self):
        with tempfile.TemporaryDirectory(prefix='lwe-hidden-alpha-') as directory:
            root = Path(directory)
            write_copy_assets(root)
            (root / 'shaders/key.vert').write_text((root / 'shaders/copy.vert').read_text())
            (root / 'shaders/key.frag').write_text('''
varying vec2 v_TexCoord;
void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, v_TexCoord.x < 0.5 ? 0.0 : 1.0); }
''')
            for name, shader, texture, blend in (
                    ('source', 'copy', 'util/white', 'translucent'),
                    ('key', 'key', None, 'normal'),
                    ('consumer', 'copy', '_rt_imageLayerComposite_1_a', 'translucent')):
                material = {'passes': [{'shader': shader, 'blending': blend,
                                       'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull'}]}
                if texture:
                    material['passes'][0]['textures'] = [texture]
                (root / f'materials/{name}.json').write_text(json.dumps(material))
                (root / f'models/{name}.json').write_text(json.dumps({
                    'material': f'materials/{name}.json', 'width': 64, 'height': 64}))
            (root / 'effects/key.json').write_text(json.dumps({'passes': [
                {'material': 'materials/key.json'}, {'material': 'materials/copy.json'}]}))
            scene = base_scene([
                {'id': 1, 'name': 'Hidden keyed source', 'image': 'models/source.json',
                 'origin': '160 90 0', 'visible': False,
                 'effects': [{'id': 3, 'file': 'effects/key.json'}]},
                {'id': 2, 'name': 'Consumer', 'image': 'models/consumer.json', 'origin': '160 90 0'}])
            scene['general'].update(clearcolor='0 0 1', camerafade=False)
            frame, _ = render_scene(self, root, scene)
            self.assertEqual(frame.getpixel((145, 90)), (0, 0, 255))
            self.assertEqual(frame.getpixel((175, 90)), (255, 0, 0))


    def test_local_composition_effect_uses_authored_pixel_extent(self):
        with tempfile.TemporaryDirectory(prefix='lwe-local-composite-') as directory:
            root = Path(directory)
            write_copy_assets(root)
            (root / 'shaders/size.vert').write_text((root / 'shaders/copy.vert').read_text())
            (root / 'shaders/size.frag').write_text("""
uniform vec4 g_Texture0Resolution;
void main() { gl_FragColor = vec4(g_Texture0Resolution.xy / 255.0, 0.0, 1.0); }
""")
            (root / 'materials/size.json').write_text(json.dumps({'passes': [{
                'shader': 'size', 'blending': 'normal', 'depthtest': 'disabled',
                'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
            (root / 'effects/size.json').write_text(json.dumps({'passes': [
                {'material': 'materials/size.json'}]}))
            scene = base_scene([{'id': 1, 'image': 'models/util/composelayer.json',
                                 'origin': '160 90 0', 'size': '64 64',
                                 'effects': [{'id': 2, 'file': 'effects/size.json'}]}])
            scene['general']['camerafade'] = False
            frame, _ = render_scene(self, root, scene)
            self.assertEqual(frame.getpixel((160, 90)), (64, 64, 0))

    def test_effectless_composition_publishes_cropped_background(self):
        with tempfile.TemporaryDirectory(prefix='lwe-compose-copy-') as directory:
            root = Path(directory)
            write_copy_assets(root)
            (root / 'materials/consumer.json').write_text(json.dumps({'passes': [{
                'shader': 'copy', 'textures': ['_rt_imageLayerComposite_2_a'],
                'blending': 'normal', 'depthtest': 'disabled', 'depthwrite': 'disabled',
                'cullmode': 'nocull'}]}))
            (root / 'models/consumer.json').write_text(json.dumps({
                'material': 'materials/consumer.json', 'width': 32, 'height': 32}))
            scene = base_scene([
                {'id': 1, 'image': 'models/util/solidlayer.json', 'size': '64 64',
                 'origin': '80 90 0', 'color': '0 1 0'},
                {'id': 2, 'image': 'models/util/composelayer.json', 'size': '32 32',
                 'origin': '80 90 0'},
                {'id': 3, 'image': 'models/consumer.json', 'origin': '240 90 0',
                 'dependencies': [2]}])
            scene['general'].update(clearcolor='0 0 1', camerafade=False)
            frame, _ = render_scene(self, root, scene)
            self.assertEqual(frame.getpixel((240, 90)), (0, 255, 0))
            self.assertEqual(frame.getpixel((160, 90)), (0, 0, 255))

    def test_composition_without_background_preserves_scene_under_rotating_effect(self):
        with tempfile.TemporaryDirectory(prefix='lwe-compose-alpha-') as directory:
            root = Path(directory)
            write_copy_assets(root)
            (root / 'shaders/check.vert').write_text((root / 'shaders/copy.vert').read_text())
            (root / 'shaders/check.frag').write_text("""
uniform sampler2D g_Texture0;
varying vec2 v_TexCoord;
void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, texSample2D(g_Texture0, v_TexCoord).a); }
""")
            (root / 'materials/check.json').write_text(json.dumps({'passes': [{
                'shader': 'check', 'blending': 'normal', 'depthtest': 'disabled',
                'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
            (root / 'effects/check.json').write_text(json.dumps({'passes': [
                {'material': 'materials/check.json'}]}))
            scene = base_scene([{'id': 1, 'image': 'models/util/composelayer.json',
                'size': '64 64', 'origin': '160 90 0', 'copybackground': False,
                'effects': [{'id': 2, 'file': 'effects/check.json'}]}])
            scene['general'].update(clearcolor='0 0 1', camerafade=False)
            frame, _ = render_scene(self, root, scene)
            self.assertEqual(frame.getpixel((160, 90)), (0, 0, 255))

    def test_legacy_lit_image_keeps_world_position_through_effect_chain(self):
        with tempfile.TemporaryDirectory(prefix='lwe-legacy-prelighting-') as directory:
            pixels = []
            for effected in (False, True):
                root = Path(directory) / str(effected)
                write_copy_assets(root)
                (root / 'materials/lit.json').write_text(json.dumps({'passes': [{
                    'shader': 'genericimage2', 'textures': ['util/white'],
                    'combos': {'LIGHTING': 1, 'REFLECTION': 0}, 'blending': 'normal',
                    'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
                (root / 'models/lit.json').write_text(json.dumps({
                    'material': 'materials/lit.json', 'width': 64, 'height': 64}))
                plane = {'id': 1, 'image': 'models/lit.json', 'origin': '80 90 0',
                         'size': '64 64'}
                if effected:
                    plane['effects'] = [{'id': 3, 'file': 'effects/copy.json'}]
                    (root / 'effects/copy.json').write_text(json.dumps({'passes': [
                        {'material': 'materials/copy.json'}]}))
                scene = base_scene([plane, {'id': 2, 'light': 'lpoint',
                    'origin': '80 90 15', 'radius': 30, 'intensity': 2, 'color': '1 1 1'}])
                scene['general'].update(ambientcolor='0 0 0', skylightcolor='0 0 0', camerafade=False)
                frame, _ = render_scene(self, root, scene)
                pixels.append(frame.getpixel((80, 90)))
            self.assertGreater(min(pixels[0]), 40)
            self.assertLessEqual(max(abs(a-b) for a,b in zip(*pixels)), 3, pixels)

    def test_perspective_lighting_survives_an_image_effect(self):
        with tempfile.TemporaryDirectory(prefix='lwe-perspective-prelighting-') as directory:
            pixels = []
            for effected in (False, True):
                root = Path(directory) / str(effected)
                write_copy_assets(root)
                (root / 'materials/lit.json').write_text(json.dumps({'passes': [{
                    'shader': 'genericimage3', 'textures': ['util/white'],
                    'combos': {'LIGHTING': 1}, 'blending': 'normal',
                    'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
                (root / 'models/lit.json').write_text(json.dumps({
                    'material': 'materials/lit.json', 'width': 64, 'height': 64}))
                plane = {'id': 1, 'image': 'models/lit.json', 'origin': '0.8 0 0',
                         'scale': '0.025 0.025 0.025', 'size': '64 64'}
                if effected:
                    plane['effects'] = [{'id': 3, 'file': 'effects/copy.json'}]
                    (root / 'effects/copy.json').write_text(json.dumps({'passes': [
                        {'material': 'materials/copy.json'}]}))
                scene = {'camera': {'eye': '0 0 5', 'center': '0 0 0', 'up': '0 1 0'},
                         'general': {'ambientcolor': '0 0 0', 'skylightcolor': '0 0 0',
                                     'camerafade': False, 'orthogonalprojection': None, 'clearcolor': '0 0 1',
                                     'fov': 50, 'nearz': .1, 'farz': 100},
                         'objects': [plane, {'id': 2, 'light': 'lpoint',
                             'origin': '0.8 0 1', 'radius': 2, 'intensity': .1, 'color': '1 1 1'}]}
                frame, _ = render_scene(self, root, scene)
                pixels.append(frame.getpixel((191, 90)))
            self.assertGreater(min(pixels[0]), 20)
            self.assertLessEqual(max(abs(a-b) for a,b in zip(*pixels)), 3, pixels)

    def test_dynamic_effect_blends_against_the_scene_at_the_layer_position(self):
        with tempfile.TemporaryDirectory(prefix='lwe-dynamic-blend-') as directory:
            root = Path(directory)
            write_copy_assets(root)
            (root / 'effects/copy.json').write_text(json.dumps({'passes': [
                {'material': 'materials/copy.json'}]}))
            scene = base_scene([
                {'id': 1, 'image': 'models/util/solidlayer.json', 'size': '96 96',
                 'origin': '80 90 0', 'color': '0 1 0'},
                {'id': 2, 'image': 'models/util/solidlayer.json', 'size': '64 64',
                 'origin': '80 90 0', 'colorBlendMode': 2, 'alpha': 0.19,
                 'effects': [{'id': 3, 'file': 'effects/copy.json',
                     'visible': {'value': True, 'script':
                         'export function update() { return true; }'}}]}])
            scene['general'].update(clearcolor='0 0 1', camerafade=False)
            frame, _ = render_scene(self, root, scene)
            self.assertEqual(frame.getpixel((80, 90)), (0, 255, 0))
            self.assertEqual(frame.getpixel((65, 90)), (0, 255, 0))

    def test_masked_composition_blur_keeps_the_cropped_previous_input(self):
        with tempfile.TemporaryDirectory(prefix='lwe-compose-masked-blur-') as directory:
            root = Path(directory)
            scene = base_scene([
                {'id': 1, 'image': 'models/util/solidlayer.json', 'size': '96 96',
                 'origin': '80 90 0', 'color': '0 1 0'},
                {'id': 2, 'image': 'models/util/composelayer.json', 'size': '64 64',
                 'origin': '80 90 0', 'effects': [
                     {'id': 3, 'file': 'effects/blurprecise/effect.json', 'passes': [
                         {'id': 4, 'constantshadervalues': {'scale': '0 0'}},
                         {'id': 5, 'constantshadervalues': {'scale': '0 0'},
                          'textures': [None, None, 'util/black']}]}]}])
            scene['general'].update(clearcolor='0 0 1', camerafade=False)
            frame, _ = render_scene(self, root, scene)
            self.assertEqual(frame.getpixel((80, 90)), (0, 255, 0))


if __name__ == '__main__':
    unittest.main()
