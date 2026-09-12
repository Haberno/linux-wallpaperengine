"""Comma-separated numeric shader constants retain their vector components."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ShaderVectorConstants(unittest.TestCase):
    def test_comma_vectors_in_materials_and_effect_overrides(self):
        for effect in (False, True):
            with self.subTest(effect=effect), tempfile.TemporaryDirectory(prefix='lwe-comma-vector-') as directory:
                root = Path(directory)
                write_copy_assets(root)
                (root / 'shaders/probe.vert').write_text((root / 'shaders/copy.vert').read_text())
                (root / 'shaders/probe.frag').write_text('''
uniform vec2 uAngles; // {"material":"angles","default":"0 0"}
uniform vec3 uTint; // {"material":"tint","default":"0 0 0"}
uniform vec4 uRect; // {"material":"rect","default":"0 0 0 1"}
void main() { gl_FragColor = vec4(uAngles.y / 360.0, uTint.y, uRect.z, 1.0); }
''')
                constants = {'angles': '0.0, 360.0', 'tint': {'value': '0.1, 0.6, 0.9'},
                             'rect': '0,0,0.4,1'}
                material = {'shader': 'probe', 'blending': 'normal', 'depthtest': 'disabled',
                            'depthwrite': 'disabled', 'cullmode': 'nocull'}
                if not effect:
                    material['constantshadervalues'] = constants
                (root / 'materials/probe.json').write_text(json.dumps({'passes': [material]}))
                (root / 'models/probe.json').write_text(json.dumps({
                    'material': 'materials/copy.json' if effect else 'materials/probe.json',
                    'width': 64, 'height': 64}))
                layer = {'id': 1, 'image': 'models/probe.json', 'origin': '160 90 0', 'size': '64 64'}
                if effect:
                    (root / 'effects/probe.json').write_text(json.dumps({
                        'passes': [{'material': 'materials/probe.json'}]}))
                    layer['effects'] = [{'id': 2, 'file': 'effects/probe.json', 'passes': [
                        {'constantshadervalues': constants}]}]
                scene = base_scene([layer])
                scene['general']['camerafade'] = False
                frame, _ = render_scene(self, root, scene)
                self.assertEqual(frame.getpixel((160, 90)), (255, 153, 102))


if __name__ == '__main__':
    unittest.main()
