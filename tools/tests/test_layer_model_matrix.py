"""Original-layer transforms from controlled Wallpaper Engine 2.8.42 probes.

The diagnostic quads use UVs for placement so each sampled color measures the
uniform itself, independently of the renderer's existing geometry transforms.
"""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets

CASES = [
    ('identity', {}),
    ('translation', {'origin': '185 115 7'}),
    ('scale', {'scale': '1.5 0.75 2'}),
    ('rotation', {'angles': '0.2 0.3 0.5'}),
    ('aligned', {'alignment': 'topleft', 'scale': '1.5 0.75 1'}),
    ('aligned_rotated', {'alignment': 'topleft', 'scale': '1.5 0.75 1', 'angles': '0.2 0.3 0.5'}),
    ('parent', {'parent': 900, 'origin': '20 10 4', 'angles': '0.1 0.2 0.3', 'scale': '0.8 1.1 1.2'}),
]
STAGES = ['intermediate', 'final']


def matrix_fixture(root, animated=False):
    write_copy_assets(root)
    fragment = '''
uniform mat4 g_LayerModelMatrix;
varying vec2 v_TexCoord;
void main() {
    vec4 t = mul(vec4(0,0,0,1), g_LayerModelMatrix);
    vec4 x = mul(vec4(1,0,0,0), g_LayerModelMatrix);
    vec4 y = mul(vec4(0,1,0,0), g_LayerModelMatrix);
    vec4 z = mul(vec4(0,0,1,0), g_LayerModelMatrix);
    vec3 value;
    if (v_TexCoord.x < 0.2) value = t.xyz / 512.0 + 0.5;
    else if (v_TexCoord.x < 0.4) value = x.xyz / 8.0 + 0.5;
    else if (v_TexCoord.x < 0.6) value = y.xyz / 8.0 + 0.5;
    else if (v_TexCoord.x < 0.8) value = z.xyz / 8.0 + 0.5;
    else value = vec3(t.w, x.w, y.w) / 2.0;
    gl_FragColor = vec4(value, 1);
}
'''
    def shader(name, frag, cell=None):
        offset = (0, 0) if cell is None else (cell[0] / len(CASES), cell[1] / len(STAGES))
        size = (1, 1) if cell is None else (1 / len(CASES), 1 / len(STAGES))
        (root / f'shaders/{name}.vert').write_text('''
attribute vec3 a_Position;
attribute vec2 a_TexCoord;
varying vec2 v_TexCoord;
void main() {
    v_TexCoord = a_TexCoord;
    gl_Position = vec4(a_TexCoord * vec2(SX,SY) + vec2(OX,OY), 0, 1);
}
'''.replace('SX', str(2 * size[0])).replace('SY', str(-2 * size[1]))
   .replace('OX', str(2 * offset[0] - 1)).replace('OY', str(1 - 2 * offset[1])))
        (root / f'shaders/{name}.frag').write_text(frag)
        (root / f'materials/{name}.json').write_text(json.dumps({'passes': [{
            'shader': name, 'blending': 'normal', 'depthtest': 'disabled',
            'depthwrite': 'disabled', 'cullmode': 'nocull', 'textures': ['util/white']}]}))

    shader('encode', fragment)
    shader('blue', 'void main() { gl_FragColor = vec4(0,0,1,1); }')
    copy = '''
uniform sampler2D g_Texture0;
varying vec2 v_TexCoord;
void main() { gl_FragColor = texSample2D(g_Texture0, v_TexCoord); }
'''
    objects = [{'id': 900, 'name': 'Parent', 'origin': '150 75 3',
                'angles': '0 0 0.4', 'scale': '1.2 0.8 1'}]
    for row, stage in enumerate(STAGES):
        for col, (case, settings) in enumerate(CASES):
            name = f'{stage}_{case}'
            shader(name, fragment if stage == 'final' else copy, (col, row))
            (root / f'models/{name}.json').write_text(json.dumps({'material': 'materials/blue.json'}))
            layer = {'id': 10 + row * len(CASES) + col, 'name': name, 'origin': '160 90 0',
                     'size': '100 60', 'image': f'models/{name}.json', **settings}
            output = {'material': f'materials/{name}.json'}
            passes = []
            if stage == 'intermediate':
                passes.append({'material': 'materials/encode.json', 'target': '_rt_probe'})
                output['bind'] = [{'index': 0, 'name': '_rt_probe'}]
            passes.append(output)
            (root / f'effects/{name}.json').write_text(json.dumps({
                'fbos': [{'name': '_rt_probe', 'format': 'rgba8888', 'scale': 1, 'unique': True}],
                'passes': passes}))
            layer['effects'] = [{'id': 100 + layer['id'], 'file': f'effects/{name}.json'}]
            if animated and case == 'translation':
                layer['origin'] = {'value': '160 90 0', 'script':
                    'let frame=0; export function update(value){return ++frame<4?value:new Vec3(185,115,7);}'}
            objects.append(layer)
    scene = base_scene(objects)
    scene['general'].update(camerafade=False, cameraparallax=animated,
                            cameraparallaxamount=1.0, cameraparallaxmouseinfluence=0.0)
    return scene


def sample(image):
    result = {}
    for row, stage in enumerate(STAGES):
        for col, (case, _) in enumerate(CASES):
            result[stage + '_' + case] = [list(image.getpixel((
                round((col + (i + 0.5) / 5) * image.width / len(CASES)),
                round((row + 0.5) * image.height / len(STAGES))))) for i in range(5)]
    return result


def text_fixture(root, case):
    write_copy_assets(root)
    layer = {'id': 1, 'name': 'Text', 'text': 'MMMM', 'pointsize': 12,
             'font': 'fonts/NotoSans-Regular.ttf', 'origin': '160 90 0', 'color': '1 1 1',
             'effects': [{'id': 2, 'file': 'effects/probe.json'}]}
    if case == 'translated':
        layer.update(origin='180 100 7', scale='1.5 0.75 2')
    if case == 'right':
        layer.update(horizontalalign='right', verticalalign='top')
    (root / 'shaders/probe.vert').write_text((root / 'shaders/copy.vert').read_text())
    (root / 'shaders/probe.frag').write_text('''
uniform mat4 g_LayerModelMatrix;
void main() { gl_FragColor = vec4(mul(vec4(0,0,0,1), g_LayerModelMatrix).xyz / 512.0 + 0.5, 1); }
''')
    (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
        'shader': 'probe', 'blending': 'normal', 'depthtest': 'disabled',
        'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
    (root / 'effects/probe.json').write_text(json.dumps({'passes': [{'material': 'materials/probe.json'}]}))
    scene = base_scene([layer])
    scene['general'].update(camerafade=False, cameraparallax=False)
    return scene


# Native samples encode translation / 512 + 0.5, each basis / 8 + 0.5,
# then affine W components / 2. Allow two RGB8 levels for quantization/filtering.
EXPECTED = {
    'identity': [[207,172,127],[159,127,127],[127,159,127],[127,127,159],[127,0,0]],
    'translation': [[220,185,131],[159,127,127],[127,159,127],[127,127,159],[127,0,0]],
    'scale': [[207,172,127],[175,127,127],[127,151,127],[127,127,191],[127,0,0]],
    'rotation': [[207,172,127],[154,142,118],[114,156,134],[139,126,157],[127,0,0]],
    'aligned': [[245,161,127],[175,127,127],[127,151,127],[127,127,159],[127,0,0]],
    'aligned_rotated': [[243,179,114],[168,149,113],[117,149,132],[139,126,157],[127,0,0]],
    'parent': [[212,173,131],[152,144,122],[106,148,131],[137,130,165],[127,0,0]],
}


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class LayerModelMatrix(unittest.TestCase):
    def check_matrices(self, animated):
        with tempfile.TemporaryDirectory(prefix='lwe-layer-matrix-') as directory:
            root = Path(directory)
            scene = matrix_fixture(root, animated)
            frame, _ = render_scene(self, root, scene, frames=12, parallax=animated)
            samples = sample(frame)
            for stage in STAGES:
                for case, _ in CASES:
                    actual = samples[stage+'_'+case]
                    error = max(abs(a-b) for rgb, want in zip(actual, EXPECTED[case]) for a,b in zip(rgb,want))
                    self.assertLessEqual(error, 2, f'{stage}/{case}: {actual}')

    def test_original_image_matrix_survives_effect_stages(self):
        self.check_matrices(False)

    def test_live_translation_and_camera_parallax_keep_authored_coordinates(self):
        self.check_matrices(True)

    def check_text(self, case, expected, dynamic=False):
        with tempfile.TemporaryDirectory(prefix='lwe-text-layer-matrix-') as directory:
            root = Path(directory)
            scene = text_fixture(root, case)
            if dynamic:
                scene['objects'][0]['text'] = {'value':'MM', 'script':
                    'let frame=0; export function update(value){return ++frame<4?value:"MMMM";}'}
            frame, _ = render_scene(self, root, scene, frames=12)
            colors = [(count,rgb) for count,rgb in frame.getcolors(frame.width*frame.height) if rgb != (0,0,0)]
            self.assertTrue(colors, 'Text effect must render')
            count, actual = max(colors)
            self.assertGreater(count, 100)
            self.assertLessEqual(max(abs(a-b) for a,b in zip(actual,expected)), 2, str(actual))

    def test_text_matrix_includes_effect_surface_alignment(self):
        for case, expected in [('default',(207,169,127)), ('translated',(217,175,131)), ('right',(163,155,127))]:
            with self.subTest(case=case):
                self.check_text(case, expected)

    def test_text_rebuild_updates_the_layer_matrix(self):
        self.check_text('right', (163,155,127), dynamic=True)


if __name__ == '__main__':
    unittest.main()
