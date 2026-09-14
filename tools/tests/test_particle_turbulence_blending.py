"""Fixed-position turbulence probes isolate lifetime envelopes from noise clocks."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_motion_controlpoint import write_motion_probe


def write_turbulence_probe(root, case, candidate=False):
    scene = write_motion_probe(root, 'baseline', candidate)
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['initializer'][0].update(min=1, max=1)
    definition['initializer'][-1].update(min='0 0 0', max='0 0 0')
    operator = dict(name='turbulence', scale=.005, speedmin=800, speedmax=800,
                    phasemin=0, phasemax=0, timescale=0, mask='1 1 1', audioprocessingmode=0)
    definition['operator'] = [operator]
    if case == 'baseline':
        operator.update(speedmin=0, speedmax=0)
    if case == 'inactive':
        operator.update(blendinstart=.2, blendinend=.5)
    if case == 'explicit':
        operator.update(blendinstart=0, blendinend=0, blendoutstart=1, blendoutend=1)
    if case in ('half', 'overlap', 'added_half', 'masked_half', 'shifted_half', 'half_plus_full'):
        operator.update(blendinstart=-1, blendinend=1)
    if case == 'overlap':
        operator.update(blendoutstart=-1, blendoutend=1)
    if case == 'ended':
        operator.update(blendoutstart=-.5, blendoutend=0)
    if case == 'sharp':
        operator.update(blendinstart=.2, blendinend=.2)
    if case == 'tiny':
        operator.update(blendinstart=.2, blendinend=.201, blendoutstart=.202, blendoutend=.203)
    steps = 3 if case in ('aged', 'fadein', 'fadeout', 'long_lifetime') else 1
    if steps == 3:
        # Keep accumulated velocity inside the diagnostic's [-100,100] range.
        operator.update(speedmin=400, speedmax=400)
    if case == 'fadein':
        operator.update(blendinstart=0, blendinend=.2)
    if case in ('fadeout', 'long_lifetime'):
        operator.update(blendoutstart=0, blendoutend=.2)
    if case == 'long_lifetime':
        definition['initializer'][0].update(min=2, max=2)
    if case.startswith('added_'):
        definition['initializer'][-1].update(min='20 -10 5', max='20 -10 5')
        if case == 'added_baseline':
            operator.update(speedmin=0, speedmax=0)
        if case == 'added_inactive':
            operator.update(blendinstart=.2, blendinend=.5)
    if case.startswith('masked'):
        operator['mask'] = '1 -2 0'
    if case.startswith('shifted'):
        definition['emitter'][0]['origin'] = '25 30 10'
        operator.update(phasemin=.37, phasemax=.37)
    if case in ('double', 'half_plus_full'):
        definition['operator'].append(dict(operator, blendinstart=0, blendinend=0,
                                           blendoutstart=1, blendoutend=1))
    layer = scene['objects'][0]
    layer['visible']['script'] = layer['visible']['script'].replace('ticks>=4','ticks>=10').replace('engine.frametime<.08','engine.frametime<.024').replace('steps<1', f'steps<{steps}')
    scene['objects'][-1]['color']['script'] = scene['objects'][-1]['color']['script'].replace('==1', f'=={steps}')
    path.write_text(json.dumps(definition))
    (root / 'scene.json').write_text(json.dumps(scene))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleTurbulenceBlending(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.samples = {}

    def sample(self, case):
        if case in self.samples:
            return self.samples[case]
        with tempfile.TemporaryDirectory(prefix='lwe-turb-' + case + '-') as directory:
            root = Path(directory)
            scene = write_turbulence_probe(root, case, candidate=True)
            frame, _ = render_scene(self, root, scene, frames=30, fps=60)
            self.assertIn((0,255,0), (frame.getpixel((300,10)), frame.getpixel((300,170))))
            color = frame.getpixel((160,90))
            self.assertTrue(all(0 < value < 255 for value in color), 'Readback must not clip')
            self.samples[case] = color
            return color

    def check_ratio(self, case, baseline, ratio, initial=(0,0,0)):
        full = self.sample(baseline)
        neutral = [(0.5 + value / 200) * 255 for value in initial]
        self.assertGreater(max(abs(a-b) for a,b in zip(full,neutral)), 5,
                           'Nonzero turbulence control is required')
        for actual, reference, zero in zip(self.sample(case), full, neutral):
            # Each readback has at most half an RGB8 code of quantization.
            self.assertAlmostEqual(actual, zero + ratio * (reference-zero),
                                   delta=.5 + abs(ratio)*.5 + .1)

    def test_inactive_envelope_prevents_turbulence(self):
        for case in ('baseline', 'inactive', 'ended', 'sharp'):
            with self.subTest(case=case):
                for component in self.sample(case):
                    self.assertAlmostEqual(component, 127.5, delta=1)

    def test_envelope_scales_strength_once_without_changing_noise(self):
        for case, ratio in [('explicit',1), ('half',.5), ('overlap',.25), ('tiny',1)]:
            self.check_ratio(case, 'constant', ratio)

    def test_envelope_uses_normalized_age_across_steps(self):
        # Three .05s steps sample ages 0,.05,.1. Fade-in weights are
        # 0,.25,.5; fade-out 1,.75,.5; lifetime2 yields 1,.875,.75.
        for case, ratio in [('fadein',.25), ('fadeout',.75), ('long_lifetime',.875)]:
            self.check_ratio(case, 'aged', ratio)

    def test_fade_preserves_preexisting_velocity(self):
        for case in ('added_baseline', 'added_inactive'):
            for actual, value in zip(self.sample(case), (20,-10,5)):
                self.assertAlmostEqual(actual, (.5+value/200)*255, delta=1)
        full = self.sample('constant')
        for actual, reference, value in zip(self.sample('added_half'), full, (20,-10,5)):
            self.assertAlmostEqual(actual, (.5+value/200)*255 + .5*(reference-127.5), delta=.85)

    def test_envelope_preserves_signed_axis_masks(self):
        self.check_ratio('masked_half', 'masked', .5)
        for actual, reference, mask in zip(self.sample('masked'), self.sample('constant'), (1,-2,0)):
            self.assertAlmostEqual(actual, 127.5+mask*(reference-127.5), delta=.5+abs(mask)*.5+.1)

    def test_position_and_phase_do_not_change_the_fade_ratio(self):
        self.check_ratio('shifted_half', 'shifted', .5)

    def test_each_turbulence_operator_applies_its_own_envelope(self):
        self.check_ratio('double', 'constant', 2)
        self.check_ratio('half_plus_full', 'constant', 1.5)
