"""Effect-instance conditions select resources without shifting authored overrides."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets


def fixture(root, kind='image'):
    write_copy_assets(root)
    vertex = (root / 'shaders/copy.vert').read_text()
    for name, expression in (('blue', 'vec3(0, 0, 1)'), ('red', 'vec3(1, 0, 0)'),
                             ('green', 'vec3(0, 1, 0)'), ('tint', 'uColor')):
        (root / f'shaders/{name}.vert').write_text(vertex)
        (root / f'shaders/{name}.frag').write_text('''
uniform vec3 uColor; // {"material":"tint","default":"1 0 0"}
void main() { gl_FragColor = vec4(EXPRESSION, 1); }
'''.replace('EXPRESSION', expression))
        (root / f'materials/{name}.json').write_text(json.dumps({'passes': [{
            'shader': name, 'blending': 'normal', 'depthtest': 'disabled',
            'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
    (root / 'models/probe.json').write_text(json.dumps({'material': 'materials/blue.json'}))
    layer = {'id': 1, 'name': 'Conditional effect', 'origin': '160 90 0'}
    if kind == 'image':
        layer.update(image='models/probe.json', size='100 100')
    else:
        layer.update(text='MMMM', pointsize=20, color='0 0 1')
    layer['effects'] = [{'id': 2, 'file': 'effects/probe.json', 'combos': {'MODE': 1}}]
    scene = base_scene([layer])
    scene['general']['camerafade'] = False
    return scene, layer['effects'][0]


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class EffectConditions(unittest.TestCase):
    def assert_green(self, root, scene):
        frame, output = render_scene(self, root, scene)
        self.assertNotIn('Failed loading effect', output)
        self.assertNotIn('Pass without material', output)
        self.assertEqual(frame.getextrema()[0][1], 0, 'An incorrect override or binding produced red')
        self.assertEqual(frame.getextrema()[2][1], 0, 'The effect was discarded, leaving the blue base')
        self.assertGreater(frame.getextrema()[1][1], 250)

    def test_disabled_pass_does_not_load_its_material(self):
        with tempfile.TemporaryDirectory(prefix='lwe-effect-disabled-') as directory:
            root = Path(directory)
            scene, effect = fixture(root)
            (root / 'effects/probe.json').write_text(json.dumps({'passes': [
                {'material': 'materials/not-installed.json', 'conditions': [{'MODE': 0}]},
                {'material': 'materials/green.json', 'conditions': [{'MODE': {'op': 'ge', 'value': 1}}]},
            ]}))
            self.assert_green(root, scene)

    def test_disabled_and_command_passes_keep_original_override_indices(self):
        with tempfile.TemporaryDirectory(prefix='lwe-effect-indices-') as directory:
            for kind in ('image', 'text'):
                with self.subTest(kind=kind):
                    root = Path(directory) / kind
                    scene, effect = fixture(root, kind)
                    effect['passes'] = [{}, {}, {'constantshadervalues': {'tint': '0 1 0'}}]
                    (root / 'effects/probe.json').write_text(json.dumps({
                        'fbos': [{'name': '_rt_scratch', 'scale': 1, 'format': 'rgba8888'}], 'passes': [
                            {'material': 'materials/red.json', 'conditions': [{'MODE': 0}]},
                            {'command': 'copy', 'source': 'util/white', 'target': '_rt_scratch'},
                            {'material': 'materials/tint.json'},
                        ]}))
                    self.assert_green(root, scene)

    def test_empty_visible_effect_suppresses_its_layer_like_native(self):
        with tempfile.TemporaryDirectory(prefix='lwe-effect-inactive-') as directory:
            for kind in ('image', 'text'):
                for visible in (False, True):
                    for following in (False, True):
                        with self.subTest(kind=kind, visible=visible, following=following):
                            root = Path(directory) / f'{kind}-{visible}-{following}'
                            scene, effect = fixture(root, kind)
                            scene['general']['clearcolor'] = '0.2 0.2 0.2'
                            effect['visible'] = visible
                            (root / 'effects/probe.json').write_text(json.dumps({'passes': [
                                {'material': 'materials/red.json', 'conditions': [{'MODE': 0}]},
                            ]}))
                            if following:
                                (root / 'effects/green.json').write_text(json.dumps({'passes': [
                                    {'material': 'materials/green.json'},
                                ]}))
                                scene['objects'][0]['effects'].append({'id': 3, 'file': 'effects/green.json'})
                            frame, output = render_scene(self, root, scene)
                            self.assertNotIn('Pass without material', output)
                            if visible:
                                self.assertEqual(frame.getextrema(), ((51, 51), (51, 51), (51, 51)))
                            else:
                                # An invisible effect is skipped; the original or next effect renders.
                                expected = [51, 255, 51] if following else [51, 51, 255]
                                self.assertEqual([bounds[1] for bounds in frame.getextrema()], expected)

    def test_conditional_bind_uses_instance_combos(self):
        with tempfile.TemporaryDirectory(prefix='lwe-effect-bind-') as directory:
            root = Path(directory)
            scene, effect = fixture(root)
            # A disabled bind must leave the authored white sampler unchanged.
            # If incorrectly enabled, previous supplies the blue base instead.
            # Per-material MODE must not replace the effect-instance MODE.
            effect['passes'] = [{'combos': {'MODE': 0}}]
            (root / 'shaders/binding.vert').write_text((root / 'shaders/copy.vert').read_text())
            (root / 'shaders/binding.frag').write_text('''
uniform sampler2D g_Texture1;
varying vec2 v_TexCoord;
void main() {
    float value = texSample2D(g_Texture1, v_TexCoord).r;
    gl_FragColor = vec4(1 - value, value, 0, 1);
}
''')
            (root / 'materials/binding.json').write_text(json.dumps({'passes': [{
                'shader': 'binding', 'blending': 'normal', 'depthtest': 'disabled',
                'depthwrite': 'disabled', 'cullmode': 'nocull',
                'textures': ['util/white', 'util/white']}]}))
            (root / 'effects/probe.json').write_text(json.dumps({'passes': [{
                'material': 'materials/binding.json', 'bind': [
                    {'index': 1, 'name': 'previous', 'conditions': [{'MODE': 0}]},
                ]}]}))
            self.assert_green(root, scene)


if __name__ == '__main__':
    unittest.main()
