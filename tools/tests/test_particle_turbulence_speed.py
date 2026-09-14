"""Native turbulence speed flags with independently measured force time and u."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_turbulence_time import (
    DELTAS, force_coefficients as time_coefficients, read_time_sample, write_time_probe,
)


CASES = ('unit', 'double', 'zero', 'negative', 'flag_double', 'flag_zero',
         'flag_negative', 'range', 'flag_range', 'negative_range',
         'flag_negative_range', 'half', 'flag_half', 'mask', 'flag_mask',
         'scripted', 'flag_scripted', 'timed', 'flag_timed', 'flag_timed_scripted',
         'fractional', 'flag_fractional', 'flag_timed_scripted_equal')
SPEEDS = (0, 2, -1, .5)


def settings(case):
    return dict(flag=case.startswith('flag_'),
                speed=0 if 'zero' in case else -1 if 'negative' in case else .5 if 'fractional' in case else 1 if case=='unit' or case.endswith('_equal') else 2,
                endpoints=(400,1200) if 'range' in case else (800,800),
                mask=(.5,-1,.25) if 'mask' in case else (1,1,1),
                weight=.5 if 'half' in case else 1,
                time_case='refresh_speed' if 'timed_scripted' in case else 'authored_double' if 'timed' in case else 'zero')


def write_speed_probe(root, case, candidate=False, temporal=False):
    config = settings(case)
    scene = write_time_probe(root, config['time_case'], candidate, temporal)
    path = root/'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['flags'] = 16 if config['flag'] else 0
    op = definition['operator'][0]
    op.update(speedmin=config['endpoints'][0], speedmax=config['endpoints'][1],
              mask=' '.join(map(str,config['mask'])))
    if config['weight']==.5:
        op.update(blendinstart=-1,blendinend=1)
    path.write_text(json.dumps(definition))
    layer = scene['objects'][0]
    layer['instanceoverride']['speed'] = 1 if 'scripted' in case else config['speed']
    if 'scripted' in case:
        expression = str(list(SPEEDS))+'[steps]' if temporal else str(config['speed'])
        script = layer['visible']['script'].replace('thisLayer.instance.speed=1;', '')
        layer['visible']['script'] = script.replace('thisLayer.instance.rate=rate;',
            'thisLayer.instance.rate=rate;thisLayer.instance.speed='+expression+';')
    (root/'scene.json').write_text(json.dumps(scene))
    return scene


def force_coefficients(case, sample, stage=None):
    config = settings(case)
    noise, clock_errors = time_coefficients(config['time_case'], sample)
    override = SPEEDS[stage] if stage is not None and 'scripted' in case else config['speed']
    multiplier = 1 if config['flag'] else override
    minimum, maximum = config['endpoints']
    u = sample['particles'][0]['random_code']/65535
    minimum *= multiplier
    maximum *= multiplier
    speed = (minimum + u*(maximum-minimum))/800
    coefficients = [n*speed*m*config['weight'] for n,m in zip(noise,config['mask'])]
    # The oracle uses the independently reduced diagonal noise polynomial.
    # Bound time/rate packing and the separate 16-bit speed-fraction readback.
    errors = [(q*abs(speed) + abs(n*(maximum-minimum)/800)/65535)
              *abs(m*config['weight']) for n,q,m in zip(noise,clock_errors,config['mask'])]
    return coefficients,errors


def verify_speed_sample(case, sample, requested_clock=True):
    clock = sample['force_clock'][2]-127.5
    coefficients, bounds = force_coefficients(case,sample)
    expected = [127.5+c*clock for c in coefficients]
    errors = [abs(a-b) for a,b in zip(sample['particles'][0]['rgb'],expected)]
    tolerances = [.6+.5*abs(c)+(abs(clock)+.5)*q for c,q in zip(coefficients,bounds)]
    sample.update(expected=expected,errors=errors,tolerances=tolerances,
                  coefficient_quantization=bounds,requested_clock_error=abs(clock-51))
    if requested_clock:
        assert sample['requested_clock_error']<=1,sample
    assert all(0<x<255 for x in sample['particles'][0]['rgb']), ('Clipped force',sample)
    assert all(e<=t for e,t in zip(errors,tolerances)), (case,sample)
    return max(errors)


def verify_speed_sequence(case, samples, requested_clock=True):
    initial = samples[0]['particles'][0]['random_code']
    coefficients, clocks = [], [0.]
    expected, input_errors = [127.5]*3, [0.]*3
    for index,sample in enumerate(samples):
        particle = sample['particles'][0]
        assert abs(particle['random_code']-initial)<=1, ('Random value changed',sample)
        measured = dict(sample,particles=[dict(particle,random_code=initial)])
        coefficient,bounds = force_coefficients(case,measured,index)
        coefficients.append(coefficient)
        clocks.append(sample['force_clock'][2]-127.5)
        increment = clocks[-1]-clocks[-2]
        assert increment>0, ('Missing force step',sample)
        for axis in range(3):
            expected[axis] += coefficient[axis]*increment
            input_errors[axis] += bounds[axis]*(increment+(.5 if index==0 else 1))
        # Adjacent increments share quantized clock samples.
        tolerances = [.6+.5*(abs(coefficient[axis])+sum(abs(a[axis]-b[axis])
            for a,b in zip(coefficients,coefficients[1:])))+input_errors[axis] for axis in range(3)]
        errors = [abs(a-b) for a,b in zip(particle['rgb'],expected)]
        sample.update(expected=list(expected),errors=errors,tolerances=tolerances,
                      initial_random_code=initial,coefficient_quantization=bounds,
                      requested_clock_error=abs(clocks[-1]-1020*sum(DELTAS[:index+1])))
        if requested_clock:
            assert sample['requested_clock_error']<=1,sample
        assert all(0<x<255 for x in particle['rgb']), ('Clipped force',sample)
        assert all(e<=t for e,t in zip(errors,tolerances)), (case,index+1,sample)


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleTurbulenceSpeed(unittest.TestCase):
    def check_cases(self, cases):
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='lwe-turb-speed-'+case+'-') as directory:
                root=Path(directory)
                scene=write_speed_probe(root,case,candidate=True)
                frame,_=render_scene(self,root,scene,frames=60,fps=60)
                self.assertIn((0,255,0),(frame.getpixel((300,10)),frame.getpixel((300,170))))
                verify_speed_sample(case,read_time_sample(frame,case))

    def test_flag_bypasses_positive_zero_and_negative_instance_speed(self):
        self.check_cases(['zero','flag_zero','unit','double','flag_double','negative','flag_negative',
                          'fractional','flag_fractional'])

    def test_random_ranges_preserve_signed_instance_scaling(self):
        self.check_cases(['range','flag_range','negative_range','flag_negative_range'])

    def test_lifetime_envelope_still_weights_flagged_turbulence(self):
        self.check_cases(['half','flag_half'])

    def test_signed_axis_masks_still_apply_with_the_flag(self):
        self.check_cases(['mask','flag_mask'])

    def test_scripted_speed_changes_respect_the_flag(self):
        self.check_cases(['scripted','flag_scripted'])

    def test_flag_keeps_rate_timescale_and_speed_triggered_refresh(self):
        self.check_cases(['timed','flag_timed','flag_timed_scripted','flag_timed_scripted_equal'])
