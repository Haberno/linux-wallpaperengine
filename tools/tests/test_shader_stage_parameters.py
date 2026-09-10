"""Opt-in regressions for distinct material controls sharing a shader uniform name."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ShaderStageParameters(unittest.TestCase):
    def test_stage_defaults_and_overrides_stay_independent(self):
        cases = [
            (0, {}, {}, (0, 76, 64)),
            (1, {}, {}, (255, 76, 64)),
            (1, {'strength': 50, 'strengthuv': .6}, {}, (128, 153, 64)),
            (1, {'strength': 50, 'strengthuv': .6},
             {'strength': 25, 'strengthuv': .9}, (64, 230, 64)),
        ]
        for mode, constants, overrides, expected in cases:
            with self.subTest(mode=mode, constants=constants, overrides=overrides):
                with tempfile.TemporaryDirectory(prefix='lwe-shader-stages-') as directory:
                    root = Path(directory)
                    for folder in ('shaders', 'materials', 'effects'):
                        (root / folder).mkdir()
                    # Foliage sway declares both controls unconditionally. The
                    # vertex strength is inactive in UV mode but its metadata
                    # must still not replace the fragment default of .3 with 100.
                    (root / 'shaders/stages.vert').write_text('''
attribute vec3 a_Position;
uniform mat4 g_ModelViewProjectionMatrix;
uniform float g_Strength; // {"material":"strength","default":100}
uniform float g_Power; // {"material":"power","default":1}
varying float v_VertexStrength;
void main() {
#if MODE == 1
    v_VertexStrength = g_Strength * .01 * g_Power;
#else
    v_VertexStrength = 0.0;
#endif
    gl_Position = mul(vec4(a_Position, 1.0), g_ModelViewProjectionMatrix);
}
''')
                    (root / 'shaders/stages.frag').write_text('''
uniform float g_Strength; // {"material":"strengthuv","default":0.3}
uniform float g_Power; // {"material":"power","default":1}
uniform float g_StrengthExtra; // {"material":"extra","default":0.25}
varying float v_VertexStrength;
#define SWAY g_Strength
void main() {
    gl_FragColor = vec4(v_VertexStrength, SWAY * g_Power, g_StrengthExtra, 1.0);
}
''')
                    (root / 'materials/stages.json').write_text(json.dumps({'passes': [{
                        'shader': 'stages', 'blending': 'normal', 'depthtest': 'disabled',
                        'depthwrite': 'disabled', 'cullmode': 'nocull', 'combos': {'MODE': mode},
                        'constantshadervalues': constants}]}))
                    (root / 'effects/stages.json').write_text(json.dumps({
                        'passes': [{'material': 'materials/stages.json'}]}))
                    scene = {
                        'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                        'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                                    'camerafade': False, 'clearcolor': '0 0 0'},
                        'objects': [{'id': 1, 'name': 'Stage parameter probe',
                                     'image': 'models/util/solidlayer.json', 'origin': '160 90 0',
                                     'size': '320 180', 'effects': [{
                                         'id': 2, 'file': 'effects/stages.json',
                                         'passes': [{'constantshadervalues': overrides}]}]}]}
                    frame, _ = render_scene(self, root, scene)
                    actual = frame.getpixel((160, 90))
                    for value, reference in zip(actual, expected):
                        self.assertAlmostEqual(value, reference, delta=1,
                                               msg=f'Got {actual}, expected {expected}')


if __name__ == '__main__':
    unittest.main()
