"""Fixed-step opacity readbacks for native alpha oscillator blending."""
import json
import math
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_motion_controlpoint import write_motion_probe


def write_alpha_probe(root, case, candidate=False):
    scene = write_motion_probe(root, 'baseline', candidate)
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['initializer'][0].update(min=1, max=1)
    operator = {'name': 'oscillatealpha', 'frequencymin': 0, 'frequencymax': 0,
                'scalemin': .2, 'scalemax': .2, 'phasemin': 0, 'phasemax': 0}
    definition['operator'].append(operator)
    if case == 'blend_start':
        operator.update(blendinstart=.2, blendinend=.5)
    elif case == 'blend_half':
        operator.update(blendinstart=-1, blendinend=1)
    elif case == 'overlap':
        operator.update(blendinstart=-1, blendinend=1, blendoutstart=-1, blendoutend=1)
    elif case == 'blend_end':
        operator.update(blendoutstart=-.5, blendoutend=0)
    elif case == 'sharp_start':
        operator.update(blendinstart=.2, blendinend=.2)
    elif case == 'tiny_envelope':
        operator.update(blendinstart=.2, blendinend=.201, blendoutstart=.202, blendoutend=.203)
    elif case == 'initial_alpha':
        definition['initializer'].append({'name': 'alpharandom', 'min': .4, 'max': .4})
    elif case == 'instance_alpha':
        scene['objects'][0]['instanceoverride']['alpha'] = .4
    elif case == 'twice':
        definition['operator'].append(dict(operator, scalemin=.5, scalemax=.5))
    elif case in ('fade_before', 'fade_after', 'change_before', 'change_after'):
        change = ({'name': 'alphafade', 'fadeintime': .2, 'fadeouttime': .8} if case.startswith('fade')
                  else {'name': 'alphachange', 'starttime': 0, 'endtime': 1, 'startvalue': .4, 'endvalue': .4})
        definition['operator'].insert(-1 if case.endswith('before') else len(definition['operator']), change)
    elif case in ('trough_birth', 'trough_aged', 'trough_blended'):
        # At this trough the random amplitude cancels exactly. This verifies
        # sine, phase-before-frequency and particle age without RNG matching.
        age = 0 if case == 'trough_birth' else .1
        phase = 1.5 * math.pi / 2 - age
        operator.update(frequencymin=2, frequencymax=2, phasemin=phase, phasemax=phase, scalemax=1)
        if case == 'trough_blended':
            operator.update(blendinstart=0, blendinend=.2)
    elif case == 'inverted_scale_trough':
        operator.update(frequencymin=1, frequencymax=1, phasemin=1.5 * math.pi,
                        phasemax=1.5 * math.pi, scalemin=.7, scalemax=.2)
    if case in ('aged', 'fade_before', 'fade_after', 'trough_aged', 'trough_blended', 'fadeout_half'):
        layer = scene['objects'][0]
        layer['visible']['script'] = layer['visible']['script'].replace('steps<1', 'steps<3')
        scene['objects'][1]['color']['script'] = scene['objects'][1]['color']['script'].replace('==1', '==3')
    if case == 'fadeout_half':
        operator.update(blendoutstart=0, blendoutend=.2)
    path.write_text(json.dumps(definition))
    shader = root / 'shaders/genericparticle.vert'
    source = shader.read_text().replace('attribute vec3 a_Position;',
                                       'attribute vec3 a_Position;\nattribute vec4 a_Color;')
    start = source.index('    v_Color=')
    end = source.index('\n', start)
    # Encode opacity as opaque RGB, avoiding framebuffer alpha composition.
    source = source[:start] + '    v_Color=vec4(a_Color.a,a_Color.a,a_Color.a,1.0);' + source[end:]
    shader.write_text(source)
    (root / 'scene.json').write_text(json.dumps(scene))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleAlphaOscillation(unittest.TestCase):
    def check_alpha(self, case, expected):
        with tempfile.TemporaryDirectory(prefix='lwe-alpha-osc-' + case + '-') as directory:
            root = Path(directory)
            scene = write_alpha_probe(root, case, candidate=True)
            frame, _ = render_scene(self, root, scene, frames=20, fps=30)
            self.assertIn((0, 255, 0), (frame.getpixel((300, 10)), frame.getpixel((300, 170))))
            for value in frame.getpixel((160, 90)):
                self.assertAlmostEqual(value, expected * 255, delta=1)

    def test_envelope_leaves_alpha_unchanged_before_blend_in(self):
        self.check_alpha('blend_start', 1)

    def test_half_blend_interpolates_the_multiplier_toward_one(self):
        self.check_alpha('blend_half', .6)

    def test_equal_scale_endpoints_apply_a_constant_multiplier(self):
        self.check_alpha('constant', .2)

    def test_lifetime_fadeout_and_overlapping_envelopes(self):
        self.check_alpha('fadeout_half', .6)
        self.check_alpha('blend_end', 1)
        self.check_alpha('overlap', .8)

    def test_native_envelope_selection_handles_equal_and_tiny_intervals(self):
        self.check_alpha('sharp_start', 1)
        self.check_alpha('tiny_envelope', .2)

    def test_initial_and_instance_opacity_are_preserved(self):
        self.check_alpha('initial_alpha', .08)
        self.check_alpha('instance_alpha', .08)

    def test_oscillators_multiply_in_order_without_accumulating_each_tick(self):
        self.check_alpha('twice', .1)
        self.check_alpha('aged', .2)

    def test_alpha_fade_and_change_follow_authored_operator_order(self):
        self.check_alpha('fade_before', .1)
        self.check_alpha('fade_after', .1)
        self.check_alpha('change_before', .08)
        self.check_alpha('change_after', .08)

    def test_sine_trough_uses_particle_age_and_phase_before_frequency(self):
        self.check_alpha('trough_birth', .2)
        self.check_alpha('trough_aged', .2)
        self.check_alpha('trough_blended', .6)
        self.check_alpha('inverted_scale_trough', .7)
