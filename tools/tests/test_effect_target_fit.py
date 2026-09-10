"""Authored simulation target limits must reach the actual GPU allocation."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class EffectTargetFit(unittest.TestCase):
    def test_fitted_target_reports_its_capped_dimensions_to_the_next_pass(self):
        with tempfile.TemporaryDirectory(prefix='lwe-target-fit-') as directory:
            root = Path(directory)
            write_copy_assets(root)
            (root / 'shaders/size.vert').write_text((root / 'shaders/copy.vert').read_text())
            (root / 'shaders/size.frag').write_text('''
uniform vec4 g_Texture0Resolution;
void main() { gl_FragColor = vec4(g_Texture0Resolution.xy / 255.0, 0.0, 1.0); }
''')
            (root / 'materials/size.json').write_text(json.dumps({'passes': [{
                'shader': 'size', 'blending': 'normal', 'depthtest': 'disabled',
                'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
            (root / 'effects/probe.json').write_text(json.dumps({
                'fbos': [{'name': '_rt_simulation', 'fit': 64, 'format': 'rgba8888'}],
                'passes': [{'material': 'materials/copy.json', 'target': '_rt_simulation'},
                           {'material': 'materials/size.json',
                            'bind': [{'index': 0, 'name': '_rt_simulation'}]}]}))
            scene = base_scene([{'id': 1, 'name': 'Simulation', 'image': 'models/util/solidlayer.json',
                                 'size': '320 180', 'origin': '160 90 0',
                                 'effects': [{'id': 2, 'file': 'effects/probe.json'}]}])
            scene['general']['camerafade'] = False
            frame, _ = render_scene(self, root, scene)
            self.assertEqual(frame.getpixel((160, 90)), (64, 36, 0))


if __name__ == '__main__':
    unittest.main()
