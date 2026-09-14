"""Measure the owner clock and current rate independently of turbulence output."""
import copy
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_turbulence_random import write_random_probe, read_samples


CASES = ('forward', 'zero', 'negative', 'half_time', 'slow_step', 'fast_step',
         'history_slow', 'history_fast', 'late_birth', 'phase', 'masked',
         'blend_half', 'blend_inactive', 'authored_half', 'authored_double',
         'refresh_alpha', 'refresh_size', 'refresh_count', 'refresh_speed',
         'refresh_lifetime', 'refresh_brightness', 'refresh_colorn',
         'refresh_controlpoint0', 'refresh_before_rate')
DELTAS = (.0125, .025, .00625, .0125)


def settings(case):
    return dict(authored_rate=.5 if case=='authored_half' else 2 if case=='authored_double' else 1,
                refresh=('alpha' if case=='refresh_before_rate' else case.removeprefix('refresh_')) if case.startswith('refresh_') else None,
                timescale=0 if case=='zero' else -.15 if case=='negative' else .075 if case=='half_time' else .15,
                delta=.025 if case=='slow_step' else .075 if case=='fast_step' else .05,
                history=.25 if case=='history_slow' else 2 if case=='history_fast' else 0,
                wait=1.05 if case=='late_birth' else .45,
                base=1.2 if case=='negative' else .3,
                phase=.2 if case=='phase' else 0,
                mask=(.5,-1,.25) if case=='masked' else (1,1,1),
                weight=.5 if case=='blend_half' else 0 if case=='blend_inactive' else 1)


def write_time_probe(root, case, candidate=False, temporal=False):
    scene = write_random_probe(root,'coupled',candidate)
    config = settings(case)
    path = root/'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['emitter'][0].update(instantaneous=0,rate=0,origin=' '.join([str(config['base'])]*3))
    op = definition['operator'][0]
    op.update(speedmin=800,speedmax=800,phasemin=0,phasemax=config['phase'],
              timescale=config['timescale'],mask=' '.join(map(str,config['mask'])))
    if case=='blend_half':op.update(blendinstart=-1,blendinend=1)
    if case=='blend_inactive':op.update(blendinstart=.2,blendinend=.5)
    path.write_text(json.dumps(definition))
    path=root/'particles/clock.json';clock=json.loads(path.read_text())
    clock['emitter'][0].update(instantaneous=0,rate=0)
    clock['operator'][0].update(speedinner=800,speedouter=800)
    path.write_text(json.dumps(clock))
    for layer,key in zip(scene['objects'][:2],('noise','clock')):
        layer['instanceoverride']['rate']=config['authored_rate']
        refresh=config['refresh']
        refresh_value='new Vec3(0,0,0)' if refresh=='controlpoint0' else 'new Vec3(1,1,1)' if refresh=='colorn' else '1'
        assignment=f'thisLayer.instance.{refresh}={refresh_value};' if refresh else ''
        before=assignment if case=='refresh_before_rate' else ''
        after=assignment if case!='refresh_before_rate' else ''
        layer['visible']['script']=f'''let ticks=0,steps=0;
export function init(v){{thisLayer.instance.rate={config['history']};return v;}}
export function update(v){{ticks++;thisLayer.instance.rate=steps==0?{config['history']}:0;
if(ticks>=10&&steps<{4 if temporal else 1}&&engine.runtime>={'.25+steps*.5' if temporal else config['wait']}&&engine.frametime>.01&&engine.frametime<.024){{
let rate={f'{list(DELTAS)}[steps]' if temporal else config['delta']}/engine.frametime;
{before}thisLayer.instance.rate=rate;{after}if(steps==0)thisLayer.emitParticles(1);
shared.{key}Time=engine.runtime;shared.{key}Rate=rate;
shared.{key}Steps=++steps;shared.{key}StepTick=ticks;}}return v;}}'''
    ready='shared.noiseSteps==shared.clockSteps&&shared.noiseStepTick==shared.clockStepTick&&shared.noiseTime==shared.clockTime&&shared.noiseRate==shared.clockRate'
    scene['objects'][-1]['color']['script']='export function update(v){return '+ready+(
        '?new Vec3(.2,1,(shared.noiseSteps||0)/10):new Vec3(1,0,0);}' if temporal else
        '&&shared.noiseSteps==1?new Vec3(0,1,0):new Vec3(1,0,0);}')
    for index,(key,x) in enumerate((('Time',220),('Rate',250))):
        name='marker_'+key.lower()
        source=(root/'shaders/marker.vert').read_text().replace('.875',str(x/160-1))
        (root/f'shaders/{name}.vert').write_text(source)
        (root/f'shaders/{name}.frag').write_text((root/'shaders/marker.frag').read_text())
        material=json.loads((root/'materials/marker.json').read_text());material['passes'][0]['shader']=name
        (root/f'materials/{name}.json').write_text(json.dumps(material))
        (root/f'models/{name}.json').write_text(json.dumps(dict(material=f'materials/{name}.json')))
        layer=copy.deepcopy(scene['objects'][-1])
        layer.update(id=4+index,name=key+' readback',origin=f'{x} 170 0',image=f'models/{name}.json')
        layer['color']['script']=f'''export function update(v){{let n=Math.round((shared.noise{key}||0)*100000);
return new Vec3(Math.floor(n/65536)/255,Math.floor(n/256)%256/255,n%256/255);}}'''
        scene['objects'].append(layer)
    (root/'scene.json').write_text(json.dumps(scene))
    return scene


def read_time_sample(frame, case):
    sample=read_samples(frame,'coupled')
    for key,x in (('time',220),('rate',250)):
        colors=[frame.getpixel((x,y)) for y in (10,170) if frame.getpixel((x,y))!=(0,0,0)]
        assert len(colors)==1, ('Missing or ambiguous '+key,colors)
        r,g,b=colors[0];sample[key]=(r*65536+g*256+b)/100000
    assert 0<sample['time']<10 and 0<sample['rate']<8,sample
    return sample


def force_coefficients(case, sample):
    c=settings(case)
    random=sample['particles'][0]['random_code']/65535
    time=sample['time'];rate=sample['rate'] if c['refresh'] else c['authored_rate']
    rate_error=1e-5 if c['refresh'] else 0
    q=(c['base']+random*c['phase']+c['timescale']*rate*time)*.1
    assert .01<=q<=.18, ('Outside independently reduced diagonal domain',q,sample)
    # Native diagonal field in this cell; independently recovered before this
    # test, not evaluated through the production simplex helper.
    noise=64*q*(.6-3*q*q)**4
    coefficients=[noise*m*c['weight'] for m in c['mask']]
    # Time/rate RGB24 rounding and float conversion are bounded by1e-5 each.
    # The diagonal field derivative is bounded by8.2944 over this domain.
    q_error=.1*(abs(c['timescale'])*(abs(rate)*1e-5+abs(time)*rate_error+1e-5*rate_error)+abs(c['phase'])/65535)
    coefficient_errors=[8.2944*q_error*abs(m*c['weight']) for m in c['mask']]
    return coefficients,coefficient_errors


def verify_time_sample(case, sample, requested_clock=True):
    clock=sample['force_clock'][2]
    sample['requested_clock_error']=abs(clock-(127.5+1020*settings(case)['delta']))
    if requested_clock:assert sample['requested_clock_error']<=1,sample
    coefficients,quantization=force_coefficients(case,sample)
    particle=sample['particles'][0]
    expected=[127.5+c*(clock-127.5) for c in coefficients]
    tolerances=[.5+.5*abs(c)+.1+(abs(clock-127.5)+.5)*q for c,q in zip(coefficients,quantization)]
    errors=[abs(a-b) for a,b in zip(particle['rgb'],expected)]
    sample.update(expected=expected,errors=errors,tolerances=tolerances,coefficient_quantization=quantization)
    assert all(0<x<255 for x in particle['rgb']), ('Clipped force',sample)
    assert all(e<=t for e,t in zip(errors,tolerances)), (case,sample)
    return max(errors)


def verify_time_sequence(case, samples, requested_clock=True):
    """Predict each held stage using its independently recorded time and rate."""
    initial = samples[0]['particles'][0]['random_code']
    coefficients, clocks = [], [0.]
    expected = [127.5] * 3
    input_errors = [0.] * 3
    for index, sample in enumerate(samples):
        particle = sample['particles'][0]
        assert abs(particle['random_code'] - initial) <= 1, ('Random value changed', sample)
        measured = dict(sample, particles=[dict(particle, random_code=initial)])
        coefficient, bound = force_coefficients(case, measured)
        coefficients.append(coefficient)
        clocks.append(sample['force_clock'][2] - 127.5)
        increment = clocks[-1] - clocks[-2]
        assert increment > 0, ('Missing force step', sample)
        for axis in range(3):
            expected[axis] += coefficient[axis] * increment
            input_errors[axis] += bound[axis] * (increment + (0.5 if index == 0 else 1.))
        # Adjacent increments share a quantized clock sample. Bound those
        # correlated errors rather than treating each increment independently.
        tolerances = [
            .6 + .5 * (abs(coefficient[axis]) + sum(
                abs(a[axis] - b[axis]) for a, b in zip(coefficients, coefficients[1:])))
            + input_errors[axis] for axis in range(3)]
        errors = [abs(a-b) for a, b in zip(particle['rgb'], expected)]
        sample.update(expected=list(expected), errors=errors, tolerances=tolerances,
                      initial_random_code=initial, coefficient_quantization=bound,
                      requested_clock_error=abs(clocks[-1] - 1020 * sum(DELTAS[:index+1])))
        if requested_clock:
            assert sample['requested_clock_error'] <= 1, sample
        assert all(0 < x < 255 for x in particle['rgb']), ('Clipped force', sample)
        assert all(e <= t for e, t in zip(errors, tolerances)), (case, index+1, sample)


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleTurbulenceTime(unittest.TestCase):
    def check_cases(self, cases):
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='lwe-turb-time-'+case+'-') as directory:
                root=Path(directory);scene=write_time_probe(root,case,candidate=True)
                frame,_=render_scene(self,root,scene,frames=120,fps=60)
                self.assertIn((0,255,0),(frame.getpixel((300,10)),frame.getpixel((300,170))))
                verify_time_sample(case,read_time_sample(frame,case))

    def test_owner_time_offsets_all_three_noise_coordinates(self):
        self.check_cases(['forward','zero'])

    def test_signed_timescale_controls_phase_velocity(self):
        self.check_cases(['negative','half_time'])

    def test_scripted_rate_alone_does_not_refresh_the_time_multiplier(self):
        self.check_cases(['slow_step','fast_step'])

    def test_rate_history_and_late_birth_do_not_replace_owner_time(self):
        self.check_cases(['history_slow','history_fast','late_birth'])

    def test_random_phase_is_added_to_the_same_time_offset(self):
        self.check_cases(['phase'])

    def test_masks_preserve_the_timed_force(self):
        self.check_cases(['masked'])

    def test_lifetime_weights_do_not_shift_the_time_sample(self):
        self.check_cases(['blend_half','blend_inactive'])

    def test_authored_rate_and_deferred_parameter_refresh_scale_time(self):
        self.check_cases(['authored_half','authored_double',*[case for case in CASES if case.startswith('refresh_')]])
