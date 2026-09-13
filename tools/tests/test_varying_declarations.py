"""Native varying names with component suffixes still carry the complete vector."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class VaryingDeclarations(unittest.TestCase):
    def test_native_varying_components_survive_declaration_suffixes(self):
        for vertex_suffix, fragment_suffix in [('', ''), ('', '.xy'), ('', '.rgba'), ('.xy', '')]:
            with self.subTest(vertex=vertex_suffix, fragment=fragment_suffix):
                with tempfile.TemporaryDirectory(prefix='lwe-varying-suffix-') as directory:
                    root = Path(directory)
                    write_copy_assets(root)
                    (root / 'shaders/probe.vert').write_text('''
attribute vec2 a_TexCoord;
varying vec4 v_SizeSUFFIX;
void main() {
    v_Size = vec4(0.25, 0.5, 0.75, 1.0);
    gl_Position = vec4(a_TexCoord * 2.0 - 1.0, 0, 1);
}
'''.replace('SUFFIX', vertex_suffix))
                    (root / 'shaders/probe.frag').write_text('''
varying vec4 v_SizeSUFFIX;
void main() { gl_FragColor = v_Size; }
'''.replace('SUFFIX', fragment_suffix))
                    (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
                        'shader': 'probe', 'blending': 'normal', 'depthtest': 'disabled',
                        'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
                    (root / 'models/probe.json').write_text(json.dumps({'material': 'materials/probe.json'}))
                    scene = base_scene([{'id': 1, 'name': 'Varying probe',
                                         'image': 'models/probe.json', 'origin': '160 90 0',
                                         'size': '320 180'}])
                    scene['general']['camerafade'] = False
                    frame, _ = render_scene(self, root, scene)
                    # All four Windows 2.8.42 controls returned (64, 127, 191).
                    actual = frame.getpixel((160, 90))
                    self.assertLessEqual(max(abs(a-b) for a, b in zip(actual, (64, 127, 191))), 1,
                                         f'Native varying value differs: {actual}')


if __name__ == '__main__':
    unittest.main()
