"""Native-matched tangent-space light directions for lit 2D image routes."""
import json
import os
import struct
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_model_animation_scripts import write_model
from test_text_effect_targets import base_scene, write_copy_assets


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ImageLightNormals(unittest.TestCase):
    def test_light_direction_matches_native_across_image_routes(self):
        # A static native genericimage2 probe with the light above the quad
        # reports positive tangent Y. The fork previously reflected light Y
        # without reflecting the image's normal/tangent coordinate system.
        for shader in ('genericimage2', 'genericimage4'):
            for route in ('direct', 'effect', 'puppet', 'puppet-effect', 'perspective', 'perspective-effect'):
                with self.subTest(shader=shader, route=route):
                    with tempfile.TemporaryDirectory(prefix=f'lwe-normal-{shader}-{route}-') as directory:
                        root = Path(directory)
                        puppet = route.startswith('puppet')
                        perspective = route.startswith('perspective')
                        if puppet:
                            write_model(root)
                            model = root / 'models/probe.mdl'
                            data = bytearray(model.read_bytes().split(b'MDLA0006\0')[0])
                            start = data.index(b'materials/probe.json\0') + len(b'materials/probe.json\0') + 36
                            for index, uv in enumerate(((0, 1), (1, 1), (.5, 0))):
                                struct.pack_into('<2f', data, start + index * 80 + 72, *uv)
                            model.write_bytes(data)
                        write_copy_assets(root)
                        if shader == 'genericimage2':
                            fragment = '''varying vec4 v_Light0DirectionL3X;
void main() { gl_FragColor = vec4(normalize(v_Light0DirectionL3X.xyz) * 0.5 + vec3(0.5), 1.0); }
'''
                        else:
                            fragment = '''varying vec3 v_WorldPos;
varying vec3 v_Tangent;
varying vec3 v_Bitangent;
varying vec3 v_Normal;
uniform vec4 g_LPoint_Origin[1];
void main() {
    vec3 light = normalize(g_LPoint_Origin[0].xyz - v_WorldPos);
    vec3 direction = vec3(dot(v_Tangent, light), dot(v_Bitangent, light), dot(v_Normal, light));
    gl_FragColor = vec4(direction * 0.5 + vec3(0.5), 1.0);
}
'''
                        # Use the installed stock vertex shader, so these assertions
                        # exercise its tangent construction and the renderer bindings.
                        (root / f'shaders/{shader}.frag').write_text(fragment)
                        (root / 'materials/lit.json').write_text(json.dumps({'passes': [{
                            'shader': shader, 'textures': ['util/white'],
                            'combos': {'LIGHTING': 1, 'NORMALMAP': 1, 'REFLECTION': 0},
                            'blending': 'normal', 'depthtest': 'disabled',
                            'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
                        model = {'material': 'materials/lit.json', 'width': 64, 'height': 64}
                        plane = {'id': 1, 'name': 'lit probe', 'image': 'models/lit.json',
                                 'origin': '80 90 0', 'size': '64 64'}
                        if puppet:
                            model['puppet'] = 'models/probe.mdl'
                            model.update(width=10, height=10)
                            plane.update(size='10 10', scale='10 10 1')
                        (root / 'models/lit.json').write_text(json.dumps(model))
                        if route.endswith('effect'):
                            (root / 'effects/copy.json').write_text(json.dumps({'passes': [
                                {'material': 'materials/copy.json'}]}))
                            plane['effects'] = [{'id': 3, 'file': 'effects/copy.json'}]
                        light = {'id': 2, 'name': 'point', 'light': 'point',
                                 'origin': '80 190 100', 'radius': 300, 'color': '1 1 1'}
                        scene = base_scene([plane, light])
                        scene['general'].update(hdr=False, camerafade=False)
                        if perspective:
                            plane.update(origin='0 0 0', scale='0.025 0.025 0.025')
                            light['origin'] = '0 1 1'
                            scene['camera'] = {'eye': '0 0 5', 'center': '0 0 0', 'up': '0 1 0'}
                            scene['general'].update(orthogonalprojection=None, nearz=.1, farz=100, fov=50)
                        frame, _ = render_scene(self, root, scene)
                        pixel = frame.getpixel((160 if perspective else 80, 90))
                        self.assertLessEqual(abs(pixel[0] - 127), 3, pixel)
                        self.assertLessEqual(abs(pixel[1] - 218), 4, pixel)
                        self.assertLessEqual(abs(pixel[2] - 218), 4, pixel)


if __name__ == '__main__':
    unittest.main()
