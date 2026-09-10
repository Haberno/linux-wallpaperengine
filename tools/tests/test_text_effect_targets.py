"""Opt-in text composites: named targets, hidden sources, and changing glyph sizes."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


def write_copy_assets(root):
    for directory in ('models', 'materials', 'shaders', 'effects'):
        (root / directory).mkdir(parents=True, exist_ok=True)
    vertex = '''
attribute vec3 a_Position;
attribute vec2 a_TexCoord;
uniform mat4 g_ModelViewProjectionMatrix;
varying vec2 v_TexCoord;
void main() {
    v_TexCoord = a_TexCoord;
    gl_Position = mul(vec4(a_Position, 1.0), g_ModelViewProjectionMatrix);
}
'''
    for name, expression in (('copy', 'texSample2D(g_Texture0, v_TexCoord)'), ('clear', 'vec4(0.0)')):
        (root / f'shaders/{name}.vert').write_text(vertex)
        (root / f'shaders/{name}.frag').write_text('''
uniform sampler2D g_Texture0;
varying vec2 v_TexCoord;
void main() { gl_FragColor = EXPRESSION; }
'''.replace('EXPRESSION', expression))
        (root / f'materials/{name}.json').write_text(json.dumps({'passes': [{
            'shader': name, 'blending': 'normal', 'depthtest': 'disabled',
            'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))


def base_scene(objects):
    return {'camera': {'eye': '160 90 100', 'center': '160 90 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'bloom': False}, 'objects': objects}


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class TextEffectTargets(unittest.TestCase):
    def test_previous_keeps_the_original_text_across_named_targets(self):
        from PIL import ImageChops

        with tempfile.TemporaryDirectory(prefix='lwe-text-prev-') as directory:
            images = []
            for targeted in (False, True):
                root = Path(directory) / ('targeted' if targeted else 'direct')
                write_copy_assets(root)
                passes = [{'material': 'materials/clear.json', 'target': '_rt_scratch'}] if targeted else []
                passes.append({'material': 'materials/copy.json', 'bind': [{'index': 0, 'name': 'previous'}]})
                (root / 'effects/probe.json').write_text(json.dumps({
                    'fbos': [{'name': '_rt_scratch', 'scale': 1, 'format': 'rgba8888'}], 'passes': passes}))
                image, _ = render_scene(self, root, base_scene([{
                    'id': 1, 'name': 'Text', 'text': 'Crisp original', 'pointsize': 24,
                    'origin': '160 90 0', 'effects': [{'id': 2, 'file': 'effects/probe.json'}]}]))
                images.append(image)
            self.assertIsNotNone(images[0].getbbox())
            self.assertIsNone(ImageChops.difference(*images).getbbox(),
                              'The scratch pass must not replace the previous/original text binding')


if __name__ == '__main__':
    unittest.main()
