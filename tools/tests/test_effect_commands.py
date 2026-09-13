"""Native effect command ordering, feedback, and physical-buffer clear semantics."""
import copy
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_effect_conditions import fixture


def write_shader(root, name, expression):
    (root / f'shaders/{name}.vert').write_text((root / 'shaders/copy.vert').read_text())
    (root / f'shaders/{name}.frag').write_text('''
uniform sampler2D g_Texture0;
uniform sampler2D g_Texture1;
uniform sampler2D g_Texture2;
varying vec2 v_TexCoord;
void main() { gl_FragColor = EXPRESSION; }
'''.replace('EXPRESSION', expression))
    (root / f'materials/{name}.json').write_text(json.dumps({'passes': [{
        'shader': name, 'blending': 'normal', 'depthtest': 'disabled',
        'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))


def command_fixture(root, case, kind='image'):
    scene, instance = fixture(root, kind)
    instance['name'] = 'probe'
    scene['general']['clearcolor'] = '0.2 0.2 0.2'
    fbos = [{'name': '_rt_' + name, 'format': 'rgba8888', 'scale': 1,
             'unique': True, 'clear': color}
            for name, color in (('A', '1 0 0 1'), ('B', '0 1 0 1'), ('C', '0 0 1 1'))]
    def swap(a, b):
        return {'command': 'swap', 'source': '_rt_' + a, 'target': '_rt_' + b}
    def sample(name, expression, binds, target=None):
        write_shader(root, name, expression)
        result = {'material': f'materials/{name}.json', 'bind': [
            {'index': i, 'name': '_rt_' + value} for i, value in enumerate(binds)]}
        if target:
            result['target'] = '_rt_' + target
            vertex = (root / f'shaders/{name}.vert').read_text()
            (root / f'shaders/{name}.vert').write_text(vertex.replace(
                'mul(vec4(a_Position, 1.0), g_ModelViewProjectionMatrix)', 'vec4(a_Position, 1.0)'))
        return result
    if case == 'swap':
        # Fixed endpoint transpositions AB then BC turn [A,B,C] into [C,A,B].
        # Check each resolved buffer in one RGB output; storage swaps yield red.
        passes = [swap('A', 'B'), swap('B', 'C'), sample('check',
            'vec4(0, step(0.9, texSample2D(g_Texture0, v_TexCoord).b) * '
            'step(0.9, texSample2D(g_Texture1, v_TexCoord).r) * '
            'step(0.9, texSample2D(g_Texture2, v_TexCoord).g), 0, 1)', ['A', 'B', 'C']),
            swap('B', 'C'), swap('A', 'B')]
    elif case == 'feedback':
        for fbo in fbos:
            fbo['clear'] = '0 0 0 0'
        passes = [sample('increment', 'vec4(texSample2D(g_Texture0, v_TexCoord).r + 0.125, 0, 0, 1)', ['A'], 'B'),
                  sample('output', 'vec4(texSample2D(g_Texture0, v_TexCoord).rrr, 1)', ['B']), swap('A', 'B')]
    elif case == 'clear':
        # Native 2.8.42 uses the number of resolved names, then clears the first
        # N physical buffers. Distinguish this from clearing the requested B.
        scene['objects'][0]['origin'] = {'value': '160 90 0', 'script': """
let frame = 0;
export function update(value) {
    if (++frame > 5) thisLayer.getEffect('probe').executeMaterialFunction('onlyB');
    return value;
}
"""}
        passes = [sample('fillA', 'vec4(0, 0, 1, 1)', [], 'A'),
                  sample('fillB', 'vec4(1, 1, 1, 1)', [], 'B'),
                  sample('output', 'vec4(texSample2D(g_Texture0, v_TexCoord).r, 0, '
                         '1 - texSample2D(g_Texture1, v_TexCoord).r, 1)', ['A', 'B'])]
        for name in ('fillA', 'fillB'):
            fragment = root / f'shaders/{name}.frag'
            fragment.write_text('uniform float uFill; // {"material":"fill","default":1}\n' + fragment.read_text().replace(
                'void main() {', 'void main() { if (uFill < 0.5) discard;'))
        instance['passes'] = [{'constantshadervalues': {'fill': {'value': 1, 'script':
            'let frame=0; export function update() { return ++frame <= 3 ? 1 : 0; }'}}} for _ in range(2)]
    elif case in ('initclear', 'updateclear'):
        fbos[0]['clear'] = '1 0 0'
        scene['objects'][0]['origin'] = {'value': '160 90 0', 'script': """
export function init(value) {
    const effect = thisLayer.getEffect('probe');
    if (effect.executeMaterialFunction('onlyB') !== undefined) throw new Error('clear must return void');
    effect.executeMaterialFunction('unknown');
    return value;
}
"""}
        if case == 'updateclear':
            scene['objects'][0]['origin']['script'] = scene['objects'][0]['origin']['script'].replace('export function init', 'export function update')
        passes = [sample('output', 'vec4(texSample2D(g_Texture0, v_TexCoord).rgb, 1)', ['A'])]
    elif case == 'copytail':
        passes = [sample('output', 'vec4(0, 1, 0, 1)', []),
                  {'command': 'copy', 'source': '_rt_A', 'target': '_rt_B'}]
    elif case == 'copysource':
        passes = [swap('A', 'B'), {'command': 'copy', 'source': '_rt_A', 'target': '_rt_C'},
                  sample('output', 'vec4(texSample2D(g_Texture0, v_TexCoord).rgb, 1)', ['C']), swap('A', 'B')]
    elif case == 'materialtexture':
        output = sample('output', 'vec4(texSample2D(g_Texture1, v_TexCoord).rgb, 1)', [])
        material = root / 'materials/output.json'
        definition = json.loads(material.read_text())
        definition['passes'][0]['textures'] = ['util/white', '_rt_A']
        material.write_text(json.dumps(definition))
        passes = [swap('A', 'B'), output, swap('A', 'B')]
    elif case == 'target':
        # Restore the permutation at the tail so every frame tests the same order.
        passes = [swap('A', 'B'), sample('write', 'vec4(0, 0, 1, 1)', [], 'A'),
                  sample('output', 'vec4(texSample2D(g_Texture0, v_TexCoord).rgb, 1)', ['A']), swap('A', 'B')]
    elif case in ('r16f', 'rg1616f'):
        fbos[0]['format'] = case
        passes = [sample('signed', 'vec4(-2, 3, 4, 0.5)', [], 'A')]
        check = ('abs(v.r + 2) < 0.01 && abs(v.g - 3) < 0.01' if case == 'rg1616f'
                 else 'abs(v.r + 2) < 0.01 && v.g == 0')
        write_shader(root, 'format', 'vec4(0)')
        (root / 'shaders/format.frag').write_text('''
uniform sampler2D g_Texture0;
varying vec2 v_TexCoord;
void main() {
    vec4 v = texSample2D(g_Texture0, v_TexCoord);
    bool valid = CHECK && v.b == 0 && v.a == 1;
    gl_FragColor = valid ? vec4(0, 1, 0, 1) : vec4(1, 0, 0, 1);
}
'''.replace('CHECK', check))
        passes.append({'material': 'materials/format.json', 'bind': [{'index': 0, 'name': '_rt_A'}]})
    else:
        raise ValueError(case)
    (root / 'effects/probe.json').write_text(json.dumps({'fbos': fbos, 'passes': passes,
        'functions': {'onlyB': {'action': 'clear', 'fbos': ['_rt_B']}}}))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class EffectCommands(unittest.TestCase):
    def check_case(self, case, expected, kind='image'):
        with tempfile.TemporaryDirectory(prefix=f'lwe-command-{case}-{kind}-') as directory:
            root = Path(directory)
            scene = command_fixture(root, case, kind)
            frame, output = render_scene(self, root, scene, frames=12)
            self.assertNotIn('Only copy command', output)
            self.assertEqual(frame.getpixel((160, 90)), expected, output[-2000:])

    def test_overlapping_swaps_rewrite_pass_references_in_authored_order(self):
        self.check_case('swap', (0, 255, 0))

    def test_swaps_persist_between_frames(self):
        self.check_case('feedback', (255, 255, 255))

    def test_swap_changes_explicit_write_target(self):
        self.check_case('target', (0, 0, 255))

    def test_clear_function_uses_native_physical_buffer_order(self):
        self.check_case('clear', (255, 0, 0))

    def test_simulation_targets_preserve_signed_values_and_channel_defaults(self):
        for case in ('r16f', 'rg1616f'):
            with self.subTest(format=case):
                self.check_case(case, (0, 255, 0))

    def test_init_clear_survives_lazy_framebuffer_creation(self):
        for kind in ('image', 'text'):
            with self.subTest(kind=kind):
                self.check_case('initclear', (255, 0, 0), kind)

    def test_clear_uses_the_call_receiver(self):
        operations = {
            'direct': ("if (!left.hasOwnProperty('executeMaterialFunction') || typeof left.toString() !== 'string') "
                       "throw new Error('Effect object prototype lost'); left.executeMaterialFunction('onlyB');", True, False),
            'detached': ("const method = left.executeMaterialFunction; method('onlyB');", False, False),
            'borrowed': ("left.executeMaterialFunction.call(right, 'onlyB');", False, True),
            'plain_object': ("left.executeMaterialFunction.call({}, 'onlyB');", False, False),
        }
        for case, (operation, left_cleared, right_cleared) in operations.items():
            with self.subTest(case=case):
                with tempfile.TemporaryDirectory(prefix='lwe-clear-receiver-') as directory:
                    root = Path(directory)
                    scene = command_fixture(root, 'clear')
                    left = scene['objects'][0]
                    left['name'] = 'left'
                    right = copy.deepcopy(left)
                    right.update(id=3, name='right', origin='240 90 0')
                    right['effects'][0]['id'] = 4
                    scene['objects'].append(right)
                    left['origin'] = {'value': '80 90 0', 'script': (
                        "let frame=0; export function update(value) { if (++frame === 8) { "
                        "const left=thisScene.getLayer('left').getEffect('probe');"
                        "const right=thisScene.getLayer('right').getEffect('probe');"
                        + operation + " } return value; }")}
                    frame, _ = render_scene(self, root, scene, frames=12)
                    for point, cleared in (((80, 90), left_cleared), ((240, 90), right_cleared)):
                        self.assertEqual(frame.getpixel(point), (255, 0, 0) if cleared else (0, 0, 0))

    def test_copy_command_remaps_source_without_replacing_effect_output(self):
        for case in ('copytail', 'copysource'):
            for kind in ('image', 'text'):
                with self.subTest(case=case, kind=kind):
                    self.check_case(case, (0, 255, 0), kind)

    def test_material_texture_slots_are_not_swapped(self):
        # Preserve the existing fork lookup path. Native swap analysis excludes
        # raw material slots, but native local-FBO lookup through these slots
        # is not established by this fixture.
        self.check_case('materialtexture', (255, 0, 0))

    def test_hidden_effects_resume_without_resetting_feedback(self):
        for kind in ('image', 'text'):
            for initially_hidden in (False, True):
                with self.subTest(kind=kind, initially_hidden=initially_hidden):
                    with tempfile.TemporaryDirectory(prefix=f'lwe-command-visibility-{kind}-') as directory:
                        root = Path(directory)
                        scene = command_fixture(root, 'feedback', kind)
                        effect = scene['objects'][0]['effects'][0]
                        effect['visible'] = {'value': not initially_hidden, 'script': (
                            'let frame=0; export function update() { ++frame; return frame > 3; }'
                            if initially_hidden else
                            'let frame=0; export function update() { ++frame; return frame <= 3 || frame > 10; }')}
                        frame, _ = render_scene(self, root, scene, frames=15)
                        self.assertEqual(frame.getpixel((160, 90)), (255, 255, 255))

    def test_text_feedback_retains_previous_frame(self):
        self.check_case('feedback', (255, 255, 255), 'text')


if __name__ == '__main__':
    unittest.main()
