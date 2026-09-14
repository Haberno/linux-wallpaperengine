"""Numeric size readbacks for native particle oscillator lifetime blending."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_alpha_oscillation import write_alpha_probe


def write_size_probe(root, case, candidate=False):
    # The shared fixed-step fixture supplies envelopes, phases and a completion
    # marker. Read the size attribute directly, independent of sprite geometry.
    scene = write_alpha_probe(root, case, candidate)
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    for operator in definition['operator']:
        if operator['name'] == 'oscillatealpha':
            operator['name'] = 'oscillatesize'
        elif operator['name'] == 'alphachange':
            operator['name'] = 'sizechange'
    if case == 'baseline':
        definition['operator'].pop()
    elif case == 'initial_size':
        definition['initializer'][1].update(min=24, max=24)
    elif case == 'instance_size':
        scene['objects'][0]['instanceoverride']['size'] = .4
    elif case == 'default_scale_trough':
        operator = definition['operator'][-1]
        operator.update(frequencymin=1, frequencymax=1, phasemin=4.71238898038469, phasemax=4.71238898038469)
        del operator['scalemin'], operator['scalemax']
    elif case == 'grow':
        definition['operator'][-1].update(scalemin=1.5, scalemax=1.5)
    elif case == 'change_twice':
        definition['operator'] = [{'name': 'sizechange', 'startvalue': value, 'endvalue': value}
                                  for value in (.4, .5)]
    path.write_text(json.dumps(definition))
    shader = root / 'shaders/genericparticle.vert'
    source = shader.read_text().replace('vec4(a_Color.a,a_Color.a,a_Color.a,1.0)',
        'vec4(a_TexCoordVec4.w/60.0,a_TexCoordVec4.w/60.0,a_TexCoordVec4.w/60.0,1.0)')
    shader.write_text(source)
    (root / 'scene.json').write_text(json.dumps(scene))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleSizeOscillation(unittest.TestCase):
    def check_size(self, case, multiplier):
        with tempfile.TemporaryDirectory(prefix='lwe-size-osc-' + case + '-') as directory:
            root = Path(directory)
            scene = write_size_probe(root, case, candidate=True)
            frame, _ = render_scene(self, root, scene, frames=20, fps=30)
            self.assertIn((0, 255, 0), (frame.getpixel((300, 10)), frame.getpixel((300, 170))))
            for value in frame.getpixel((160, 90)):
                self.assertAlmostEqual(value, multiplier * 127.5, delta=1)

    def test_baseline_calibrates_size_readback(self):
        self.check_size('baseline', 1)

    def test_envelope_preserves_size_before_activation(self):
        self.check_size('blend_start', 1)

    def test_partial_envelope_blends_toward_unchanged_size(self):
        self.check_size('blend_half', .6)

    def test_equal_scale_endpoints_apply_constant_size(self):
        self.check_size('constant', .2)

    def test_fadeout_and_overlapping_envelopes(self):
        self.check_size('fadeout_half', .6)
        self.check_size('blend_end', 1)
        self.check_size('overlap', .8)

    def test_native_selector_retains_equal_and_tiny_envelope_behavior(self):
        self.check_size('sharp_start', 1)
        self.check_size('tiny_envelope', .2)

    def test_birth_size_and_instance_size_are_preserved(self):
        self.check_size('initial_size', .08)
        self.check_size('instance_size', .08)

    def test_oscillators_multiply_without_frame_accumulation(self):
        self.check_size('twice', .1)
        self.check_size('aged', .2)

    def test_size_change_multiplies_in_both_operator_orders(self):
        self.check_size('change_before', .08)
        self.check_size('change_after', .08)
        self.check_size('change_twice', .2)

    def test_sine_phase_and_age_at_random_independent_troughs(self):
        self.check_size('trough_birth', .2)
        self.check_size('trough_aged', .2)
        self.check_size('trough_blended', .6)
        self.check_size('inverted_scale_trough', .7)

    def test_native_size_scale_defaults_and_growth(self):
        self.check_size('default_scale_trough', .8)
        self.check_size('grow', 1.5)
