"""Model material scripts control only the timelines in their own material pass."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_occlusion import quad_model
from test_script_property_animation import animation


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ModelMaterialScripts(unittest.TestCase):
    def test_material_scripts_start_pause_and_receive_only_their_own_events(self):
        with tempfile.TemporaryDirectory(prefix='lwe-model-material-') as directory:
            root = Path(directory)
            for folder in ('models', 'materials', 'shaders'):
                (root / folder).mkdir()
            quad_model(root, 'probe', 30, 30, '1 1 1')
            (root / 'shaders/probe.vert').write_text('''
uniform mat4 g_ModelViewProjectionMatrix;
attribute vec3 a_Position;
void main() { gl_Position = mul(vec4(a_Position, 1), g_ModelViewProjectionMatrix); }
''')
            (root / 'shaders/probe.frag').write_text('''
uniform float uAmount; // {"material":"amount","default":0}
uniform vec3 uColor; // {"material":"color","default":"1 1 1"}
void main() { gl_FragColor = vec4(uColor * uAmount, 1); }
''')
            passes = []
            for stop, color in ((2, '1 0 0'), (7, '0 1 0')):
                script = '''
export function init(value) {
    if (thisObject === thisLayer) throw Error('Material animation scope missing');
    const clock = thisObject.getAnimation('fade');
    if (!clock || clock.isPlaying()) throw Error('Material startpaused ignored');
    clock.play();
    return value;
}
export function animationEvent(event, value) {
    if (event.name === 'finished') {
        if (event.frame !== STOP) throw Error('Received another material event');
        const clock = thisObject.getAnimation('fade');
        clock.setFrame(event.frame);
        clock.pause();
        console.log('MATERIAL_STOP_OK');
    }
    return value;
}
'''.replace('STOP', str(stop))
                passes.append({'shader': 'probe', 'blending': 'additive',
                               'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull',
                               'constantshadervalues': {'color': color, 'amount': {
                                   'value': 0, 'script': script,
                                   'animation': animation(0, 1, name='fade', startpaused=True,
                                       events=[{'frame': stop, 'name': 'finished'}])}}})
            (root / 'materials/probe.json').write_text(json.dumps({'passes': passes}))
            frame, output = render_scene(self, root, {
                'camera': {'eye': '0 0 100', 'center': '0 0 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': None, 'fov': 50,
                            'clearcolor': '0 0 0', 'camerafade': False},
                'objects': [{'id': 1, 'name': 'Material clocks', 'model': 'models/probe.mdl'}]}, frames=20)
            self.assertEqual(output.count('MATERIAL_2_OK'), 1)
            self.assertEqual(output.count('MATERIAL_7_OK'), 1)
            red, green, blue = frame.getpixel((160, 90))
            self.assertAlmostEqual(red, 51, delta=2)
            self.assertAlmostEqual(green, 178, delta=2)
            self.assertEqual(blue, 0)


if __name__ == '__main__':
    unittest.main()
