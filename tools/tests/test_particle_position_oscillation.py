"""Fixed-step position readbacks for native oscillator lifetime blending."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_motion_controlpoint import write_motion_probe


def write_position_probe(root, case, candidate=False):
    scene = write_motion_probe(root, 'baseline', candidate)
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['emitter'][0]['origin'] = '0 0 0'
    definition['initializer'][0].update(min=1, max=1)
    definition['initializer'][-1].update(min='0 0 0', max='0 0 0')
    operator = {'name': 'oscillateposition', 'frequencymin': 10, 'frequencymax': 10,
                'scalemin': 40, 'scalemax': 40, 'phasemin': 0, 'phasemax': 0,
                'mask': '1 0 1'}
    if case != 'baseline':
        definition['operator'].append(operator)
    if case == 'blend_start':
        operator.update(blendinstart=.2, blendinend=.5)
    steps, delta = 1, .05
    layer = scene['objects'][0]
    if case == 'blend_half':
        operator.update(blendinstart=-1, blendinend=1)
    elif case == 'overlap':
        operator.update(blendinstart=-1, blendinend=1, blendoutstart=-1, blendoutend=1)
    elif case == 'blend_end':
        operator.update(blendoutstart=-.5, blendoutend=0)
    elif case == 'sharp_start':
        operator.update(blendinstart=.2, blendinend=.2)
    elif case == 'tiny_envelope':
        operator.update(blendinstart=.2, blendinend=.201, blendoutstart=.202, blendoutend=.203)
    elif case in ('aged', 'fadeout', 'long_lifetime'):
        steps = 3
        if case != 'aged':
            operator.update(blendoutstart=0, blendoutend=.2)
        if case == 'long_lifetime':
            definition['initializer'][0].update(min=2, max=2)
    elif case == 'phase':
        operator.update(phasemin=.2, phasemax=.2)
    elif case == 'negative_frequency':
        operator.update(frequencymin=-10, frequencymax=-10)
    elif case == 'negative_scale':
        operator.update(scalemin=-40, scalemax=-40)
    elif case == 'zero_frequency':
        operator.update(frequencymin=0, frequencymax=0, mask='1 1 1')
    elif case == 'mask':
        operator['mask'] = '-.5 0 .25'
    elif case in ('small_delta', 'large_delta'):
        delta = .025 if case == 'small_delta' else .075
    elif case in ('speed', 'zero_speed', 'negative_speed', 'speed_disabled'):
        layer['instanceoverride']['speed'] = {'speed': 2, 'zero_speed': 0, 'negative_speed': -1, 'speed_disabled': 2}[case]
        if case == 'speed_disabled':
            definition['flags'] = 16
    elif case == 'twice':
        definition['operator'].append(dict(operator, scalemin=-10, scalemax=-10))
    elif case in ('movement_before', 'movement_after'):
        steps = 3
        definition['initializer'][-1].update(min='10 0 5', max='10 0 5')
        movement = {'name': 'movement', 'gravity': '0 0 0', 'drag': 0}
        definition['operator'].insert(-1 if case.endswith('before') else len(definition['operator']), movement)
    elif case == 'initial_position':
        definition['emitter'][0]['origin'] = '20 0 10'
    elif case == 'default_frequency':
        del operator['frequencymin']
        operator.update(frequencymax=1, scalemin=200, scalemax=200)
    elif case == 'default_scale':
        operator['scalemin'] = 10
        del operator['scalemax']
    elif case == 'perspective_default_scale':
        scene['general'].update(orthogonalprojection=None, fov=50, nearz=.1, farz=1000)
        scene['camera'] = {'eye': '0 0 10', 'center': '0 0 0', 'up': '0 1 0'}
        layer['origin'] = '0 0 0'
        scene['objects'][-1].update(origin='0 0 0', size='1 1')
        operator.update(frequencymin=100, frequencymax=100, scalemin=.5)
        del operator['scalemax']
    layer['visible']['script'] = layer['visible']['script'].replace('steps<1', f'steps<{steps}').replace('=0.05/', f'={delta}/')
    scene['objects'][-1]['color']['script'] = scene['objects'][-1]['color']['script'].replace('==1', f'=={steps}')
    shader = root / 'shaders/genericparticle.vert'
    source = shader.read_text().replace('a_TexCoordVec4C1.xyz*', 'a_Position*')
    if case == 'perspective_default_scale':
        source = source.replace('/200.0', '/2.0').replace('60.0', '1.0')
    shader.write_text(source)
    path.write_text(json.dumps(definition))
    (root / 'scene.json').write_text(json.dumps(scene))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticlePositionOscillation(unittest.TestCase):
    def check_position(self, case, expected, encoding_range=200):
        with tempfile.TemporaryDirectory(prefix='lwe-position-osc-' + case + '-') as directory:
            root = Path(directory)
            scene = write_position_probe(root, case, candidate=True)
            frame, _ = render_scene(self, root, scene, frames=20, fps=30)
            self.assertIn((0, 255, 0), (frame.getpixel((300, 10)), frame.getpixel((300, 170))))
            for actual, position in zip(frame.getpixel((160, 90)), expected):
                self.assertAlmostEqual(actual, (0.5 + position / encoding_range) * 255, delta=1)

    def test_baseline_keeps_the_birth_position(self):
        self.check_position('baseline', (0, 0, 0))

    def test_lifetime_envelope_prevents_movement_before_activation(self):
        self.check_position('blend_start', (0, 0, 0))

    def test_finite_sine_difference_accumulates_over_fixed_steps(self):
        self.check_position('constant', (19.17702154, 0, 19.17702154))
        self.check_position('aged', (52.83586094, 0, 52.83586094))

    def test_half_and_overlapping_envelopes_scale_displacement(self):
        self.check_position('blend_half', (9.58851077, 0, 9.58851077))
        self.check_position('overlap', (4.79425539, 0, 4.79425539))
        self.check_position('blend_end', (0, 0, 0))

    def test_equal_and_tiny_intervals_follow_the_native_selector(self):
        self.check_position('sharp_start', (0, 0, 0))
        self.check_position('tiny_envelope', (19.17702154, 0, 19.17702154))

    def test_fadeout_uses_normalized_lifetime(self):
        self.check_position('fadeout', (40.80069663, 0, 40.80069663))
        self.check_position('long_lifetime', (46.81827879, 0, 46.81827879))

    def test_phase_offsets_time_before_frequency(self):
        self.check_position('phase', (-3.52790239, 0, -3.52790239))

    def test_signed_frequency_and_amplitude_reverse_displacement(self):
        self.check_position('negative_frequency', (-19.17702154, 0, -19.17702154))
        self.check_position('negative_scale', (-19.17702154, 0, -19.17702154))
        self.check_position('zero_frequency', (0, 0, 0))

    def test_mask_scales_and_reverses_authored_axes(self):
        self.check_position('mask', (-9.58851077, 0, 4.79425539))

    def test_simulation_delta_selects_the_previous_sine_sample(self):
        self.check_position('small_delta', (9.89615837, 0, 9.89615837))
        self.check_position('large_delta', (27.26555040, 0, 27.26555040))

    def test_instance_speed_scales_frequency_and_respects_disable_flag(self):
        self.check_position('speed', (33.65883939, 0, 33.65883939))
        self.check_position('zero_speed', (0, 0, 0))
        self.check_position('negative_speed', (-19.17702154, 0, -19.17702154))
        self.check_position('speed_disabled', (19.17702154, 0, 19.17702154))

    def test_multiple_oscillators_add_to_the_birth_position(self):
        self.check_position('twice', (14.38276616, 0, 14.38276616))
        self.check_position('initial_position', (39.17702154, 0, 29.17702154))

    def test_position_oscillation_preserves_motion_in_both_orders(self):
        self.check_position('movement_before', (54.33586094, 0, 53.58586094))
        self.check_position('movement_after', (54.33586094, 0, 53.58586094))

    def test_native_frequency_and_projection_dependent_size_defaults(self):
        self.check_position('default_frequency', (9.99583385, 0, 9.99583385))
        self.check_position('default_scale', (4.79425539, 0, 4.79425539))
        self.check_position('perspective_default_scale', (-.47946214, 0, -.47946214), encoding_range=2)
