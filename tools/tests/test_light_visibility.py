"""Light uniforms must follow the same inherited visibility as their shadows."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class LightVisibility(unittest.TestCase):
    def test_linked_light_color_follows_its_paused_angle_animation(self):
        from test_script_property_animation import animation

        script = '''
let resumed = false;
export function init(value) {
    thisLayer.getAnimation('change').setFrame(5);
    return value;
}
export function update(value) {
    const clock = thisLayer.getAnimation('change');
    if (!resumed && engine.runtime > .5) { clock.play(); resumed = true; }
    if (Math.abs(thisLayer.color.y - clock.getFrame()/10) > .001)
        throw Error('Light color left its parent clock');
    return value;
}
'''
        with tempfile.TemporaryDirectory(prefix='lwe-light-clock-') as directory:
            root = Path(directory)
            write_copy_assets(root)
            (root / 'models/probe.json').write_text(json.dumps({'material': 'materials/copy.json'}))
            (root / 'shaders/copy.frag').write_text('''
uniform vec4 g_LDirectional_Color[1];
void main() { gl_FragColor = vec4(g_LDirectional_Color[0].rgb, 1.0); }
''')
            color = animation(1, 0, parent={'key': 'angles'})
            color['c1'] = [{'frame': 0, 'value': 0}, {'frame': 10, 'value': 1}]
            scene = base_scene([{
                'id': 1, 'name': 'Sun', 'light': 'ldirectional', 'castshadow': False,
                'color': {'value': '1 0 0', 'animation': color},
                'angles': {'value': '0 0 0', 'animation': animation(0, 0, name='change', startpaused=True)},
                'intensity': {'value': 1, 'script': script}}, {
                'id': 2, 'name': 'Uniform probe', 'image': 'models/probe.json',
                'origin': '160 90 0', 'size': '100 100'}])
            scene['general'].update(orthogonalprojection=None, nearz=.1, farz=1000, fov=90, camerafade=False)
            frame, _ = render_scene(self, root, scene, frames=20)
            self.assertEqual(frame.getpixel((160, 90)), (0, 255, 0))

    def test_hidden_parent_disables_all_light_types_and_can_show_them_again(self):
        with tempfile.TemporaryDirectory(prefix='lwe-light-visibility-') as directory:
            root = Path(directory)
            write_copy_assets(root)
            (root / 'models/probe.json').write_text(json.dumps({'material': 'materials/copy.json'}))
            (root / 'shaders/copy.frag').write_text('''
uniform vec4 g_LPoint_Color[1];
uniform vec4 g_LSpot_Color[1];
uniform vec4 g_LDirectional_Color[1];
uniform vec4 g_LTube_Color[1];
void main() {
    gl_FragColor = vec4((g_LPoint_Color[0].rgb + g_LSpot_Color[0].rgb
                       + g_LDirectional_Color[0].rgb + g_LTube_Color[0].rgb) / 4.0, 1.0);
}
''')
            lights = [{'id': 10 + index, 'name': name, 'light': name, 'parent': 1,
                       'origin': '0 0 0', 'color': '1 0 0', 'intensity': 1, 'castshadow': False}
                      for index, name in enumerate(('lpoint', 'lspot', 'ldirectional', 'ltube'))]
            for visible, expected in [(False, (0, 0, 0)), (True, (255, 0, 0))]:
                with self.subTest(visible=visible):
                    objects = [{'id': 1, 'name': 'Light group', 'visible': {'value': not visible, 'script':
                                'export function init(v) { return ' + str(visible).lower() + '; }'}},
                               *lights, {'id': 2, 'name': 'Uniform probe', 'image': 'models/probe.json',
                                         'origin': '160 90 0', 'size': '100 100'}]
                    (root / 'frame.png').unlink(missing_ok=True)
                    scene = base_scene(objects)
                    scene['general'].update(orthogonalprojection=None, nearz=.1, farz=1000, fov=90,
                                            camerafade=False)
                    frame, output = render_scene(self, root, scene)
                    self.assertNotIn('Cannot compile', output)
                    self.assertEqual(frame.getpixel((160, 90)), expected)


if __name__ == '__main__':
    unittest.main()
