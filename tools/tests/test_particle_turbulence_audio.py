"""Turbulence audio response with independently captured spectrum and force time."""
import copy
import itertools
import json
import math
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_turbulence_time import write_time_probe, read_time_sample, force_coefficients


CASES = ('off', 'left_silent', 'right_silent', 'stereo_silent', 'half', 'quarter',
         'fractional', 'inverted', 'above', 'equal_zero', 'equal_positive',
         'equal_negative', 'negative_exponent', 'null_bounds', 'null_exponent',
         'null_mode', 'invalid_mode', 'range', 'negative_speed', 'flag_speed',
         'masked', 'blend', 'timed')
SIGNAL_CASES = ('left', 'right', 'stereo', 'first', 'last', 'reversed', 'unsigned',
                'default_bounds', 'default_exponent', 'fractional_signal', 'null_end',
                'left_band', 'right_band', 'default_bands')


def settings(case):
    audio = dict(audioprocessingmode=3)
    if case in ('off', 'left_silent', 'right_silent'):
        audio['audioprocessingmode'] = {'off':0, 'left_silent':1, 'right_silent':2}[case]
    if case in ('half','fractional','quarter','range','negative_speed','flag_speed','masked','blend','timed','invalid_mode'):
        audio['audioprocessingbounds'] = '-1 1'
    if case in ('half','inverted','invalid_mode'):
        audio['audioprocessingexponent'] = 1
    if case == 'fractional':audio['audioprocessingexponent'] = .5
    if case == 'inverted':audio['audioprocessingbounds'] = '1 -1'
    if case == 'above':audio['audioprocessingbounds'] = '-2 -1'
    if case.startswith('equal_'):
        value = {'equal_zero':0, 'equal_positive':1, 'equal_negative':-1}[case]
        audio['audioprocessingbounds'] = f'{value} {value}'
    if case == 'negative_exponent':audio['audioprocessingexponent'] = -1
    if case.startswith('null_') and case != 'null_end':
        audio['audioprocessing'+case.removeprefix('null_')] = None
    if case == 'invalid_mode':audio['audioprocessingmode'] = 9
    if case in SIGNAL_CASES:
        audio.update(audioprocessingbounds='0 1', audioprocessingexponent=1,
                     audioprocessingfrequencystart=0, audioprocessingfrequencyend=15)
        if case in ('left','right'):
            audio['audioprocessingmode'] = 1 if case=='left' else 2
        if case == 'first':audio['audioprocessingfrequencyend'] = 0
        if case == 'last':audio['audioprocessingfrequencystart'] = 15
        if case == 'reversed':
            audio.update(audioprocessingfrequencystart=12, audioprocessingfrequencyend=2)
        if case == 'unsigned':
            audio.update(audioprocessingfrequencystart=-1, audioprocessingfrequencyend=99)
        if case == 'default_bounds':
            audio.pop('audioprocessingbounds')
            audio.update(audioprocessingmode=2,audioprocessingfrequencystart=9,audioprocessingfrequencyend=9)
        if case in ('left_band','right_band'):
            audio.update(audioprocessingmode=1 if case=='left_band' else 2,
                         audioprocessingfrequencystart=2,audioprocessingfrequencyend=2)
        if case == 'default_bands':
            audio.pop('audioprocessingfrequencystart')
            audio.pop('audioprocessingfrequencyend')
        if case == 'default_exponent':audio.pop('audioprocessingexponent')
        if case == 'fractional_signal':audio['audioprocessingexponent'] = .5
        if case == 'null_end':audio['audioprocessingfrequencyend'] = None
    return dict(audio=audio, time_case='forward' if case=='timed' else 'zero',
                endpoints=(400,1200) if case=='range' else (800,800),
                speed=-1 if case in ('negative_speed','flag_speed') else 1,
                mask=(-.5,1,.25) if case=='masked' else (1,1,1),
                weight=.5 if case=='blend' else 1)


def write_audio_probe(root, case, candidate=False):
    config = settings(case)
    scene = write_time_probe(root, config['time_case'], candidate)
    path = root/'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['flags'] = 16 if case=='flag_speed' else 0
    op = definition['operator'][0]
    op.update(config['audio'], speedmin=config['endpoints'][0], speedmax=config['endpoints'][1],
              mask=' '.join(map(str,config['mask'])))
    if config['weight']==.5:op.update(blendinstart=-1, blendinend=1)
    path.write_text(json.dumps(definition))
    scene['objects'][0]['instanceoverride']['speed'] = config['speed']
    script = scene['objects'][0]['visible']['script']
    script = 'let audio=engine.registerAudioBuffers(16);\n'+script
    script = script.replace('shared.noiseTime=engine.runtime;', '''shared.audioLeft=[];shared.audioRight=[];
for(let i=0;i<16;i++){shared.audioLeft.push(audio.left[i]);shared.audioRight.push(audio.right[i]);}
shared.noiseTime=engine.runtime;''')
    scene['objects'][0]['visible']['script'] = script
    if case in SIGNAL_CASES or case=='timed':
        for layer in scene['objects'][:2]:
            layer['visible']['script'] = layer['visible']['script'].replace('engine.runtime>=0.45','engine.runtime>='+('4.0' if case in SIGNAL_CASES else '2.0'))
    for channel, y in (('Left',60), ('Right',120)):
        for band in range(16):
            x = 10+20*band
            name = f'audio_{channel.lower()}_{band}'
            (root/f'shaders/{name}.vert').write_text(f'''attribute vec2 a_TexCoord;
void main(){{gl_Position=vec4(vec2({x/160-1},{y/90-1})+(a_TexCoord-.5)*vec2(.025,.044444444),0.0,1.0);}}''')
            (root/f'shaders/{name}.frag').write_text((root/'shaders/marker.frag').read_text())
            material = json.loads((root/'materials/marker.json').read_text())
            material['passes'][0]['shader'] = name
            (root/f'materials/{name}.json').write_text(json.dumps(material))
            (root/f'models/{name}.json').write_text(json.dumps(dict(material=f'materials/{name}.json')))
            layer = copy.deepcopy(scene['objects'][-1])
            layer.update(id=10+(0 if channel=='Left' else 16)+band, name=name,
                         origin=f'{x} {y} 0', image=f'models/{name}.json', size='4 4')
            layer['color']['script'] = f'''export function update(v){{let n=Math.round((shared.audio{channel}?shared.audio{channel}[{band}]:0)*100000);
return new Vec3(Math.floor(n/65536)/255,Math.floor(n/256)%256/255,n%256/255);}}'''
            scene['objects'].append(layer)
    project = json.loads((root/'project.json').read_text())
    project['general'] = dict(supportsaudioprocessing=True,properties={})
    (root/'project.json').write_text(json.dumps(project))
    (root/'scene.json').write_text(json.dumps(scene))
    return scene


def read_audio_sample(frame, case):
    sample = read_time_sample(frame, case)
    native_y = frame.getpixel((300,10))==(0,255,0)
    for channel,y in (('left',60),('right',120)):
        colors = [frame.getpixel((10+20*band,180-y if native_y else y)) for band in range(16)]
        sample[channel] = [(r*65536+g*256+b)/100000 for r,g,b in colors]
        assert all(0<=v<16 for v in sample[channel]), (channel,sample)
    return sample


def audio_response(peak, bounds, exponent):
    numerator = peak-bounds[0]
    denominator = bounds[1]-bounds[0]
    mapped = numerator/denominator if denominator else math.copysign(math.inf,numerator) if numerator else math.nan
    value = 0 if mapped<0 else mapped if mapped<1 else 1
    smooth = value*value*(3-2*value)
    result = math.inf if smooth==0 and exponent<0 else smooth**exponent
    return 0 if result<0 else result if result<1 else 1


def verify_audio_sample(case, sample, signal=False, requested_clock=True):
    config = settings(case)
    authored = config['audio']
    def value(field, default):
        result = authored.get('audioprocessing'+field, default)
        return 0 if result is None else result
    mode = value('mode',0)
    bounds = value('bounds','.8 1')
    bounds = (0,0) if bounds==0 else tuple(map(float,bounds.split()))
    exponent = value('exponent',2)
    first,last = sorted(min(int(value(field,default))&0xffffffff,15)
                        for field,default in (('frequencystart',0),('frequencyend',1)))
    peak = max([0]+[sample['left'][i] if mode==1 else sample['right'][i] if mode==2 else
               (sample['left'][i]+sample['right'][i])*.5 if mode==3 else 0 for i in range(first,last+1)])
    if signal:
        assert max(sample['left']+sample['right'])>.05, ('No actual audio input',sample)
    else:
        assert max(sample['left']+sample['right'])==0, ('Expected silence',sample)
    audio = 1 if mode==0 else audio_response(peak,bounds,exponent)
    interval = (1,1) if mode==0 else tuple(sorted(audio_response(p,bounds,exponent)
               for p in (max(0,peak-1e-5),peak+1e-5))) if signal else (audio,audio)
    noise, noise_errors = force_coefficients(config['time_case'],sample)
    u = sample['particles'][0]['random_code']/65535
    speed = 1 if case=='flag_speed' else config['speed']
    minimum,maximum = [v*speed for v in config['endpoints']]
    amplitude = minimum+u*(maximum-minimum)
    amp_error = abs(maximum-minimum)/65535
    clock = sample['force_clock'][2]-127.5
    coefficients,force_bounds = [],[]
    for n,e,m in zip(noise,noise_errors,config['mask']):
        coefficient = n*amplitude*audio*m*config['weight']/800
        corners = [a*b*c*m*config['weight']/800 for a,b,c in itertools.product(
            (n-e,n+e),(amplitude-amp_error,amplitude+amp_error),interval)]
        coefficients.append(coefficient)
        force_bounds.append(max(abs(x-coefficient) for x in corners))
    expected = [127.5+c*clock for c in coefficients]
    if case=='timed':
        wrong_noise,_ = force_coefficients(config['time_case'],dict(sample,time=sample['time']*audio))
        wrong_expected = [127.5+n*amplitude*audio*clock/800 for n in wrong_noise]
        sample['wrong_time_gap'] = max(abs(a-b) for a,b in zip(expected,wrong_expected))
    errors = [abs(a-b) for a,b in zip(sample['particles'][0]['rgb'],expected)]
    tolerances = [.6+.5*abs(c)+(abs(clock)+.5)*q for c,q in zip(coefficients,force_bounds)]
    if case=='timed':
        assert sample['wrong_time_gap']>2*max(tolerances), ('Time control is not discriminating',sample)
    if signal:
        alternate = None
        if case in ('left_band','right_band'):
            alternate = audio_response(sample['right' if mode==1 else 'left'][2],bounds,exponent)
        if case in ('default_bands','unsigned','null_end','first','last'):
            alternate = audio_response(max((a+b)*.5 for a,b in zip(sample['left'],sample['right'])),bounds,exponent)
        if case=='stereo':
            alternate = audio_response((max(sample['left'])+max(sample['right']))*.5,bounds,exponent)
        if case=='default_bounds':alternate = audio_response(peak,(0,1),exponent)
        if case=='default_exponent':alternate = audio_response(peak,bounds,1)
        if case=='fractional_signal':alternate = audio_response(peak,bounds,0)
        if case=='reversed':alternate = audio_response(0,bounds,exponent)
        if alternate is not None:
            gap = max(abs(n*amplitude*(audio-alternate)*clock/800) for n in noise)
            sample['wrong_path_gap'] = gap
            assert gap>2*max(tolerances), ('Audio control is not discriminating',case,sample)
    sample.update(expected=expected,errors=errors,tolerances=tolerances,audio_peak=peak,audio_response=audio,
                  selected_bands=[first,last],requested_clock_error=abs(clock-51))
    if requested_clock:assert sample['requested_clock_error']<=1,sample
    assert all(0<x<255 for x in sample['particles'][0]['rgb']), ('Clipped force',sample)
    assert all(e<=t for e,t in zip(errors,tolerances)), (case,sample)
    return max(errors)


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleTurbulenceAudio(unittest.TestCase):
    def check_cases(self,cases):
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='lwe-turb-audio-'+case+'-') as directory:
                root=Path(directory)
                scene=write_audio_probe(root,case,candidate=True)
                frame,_=render_scene(self,root,scene,frames=180 if case=='timed' else 60,fps=60)
                self.assertIn((0,255,0),(frame.getpixel((300,10)),frame.getpixel((300,170))))
                verify_audio_sample(case,read_audio_sample(frame,case))

    def test_active_modes_suppress_default_turbulence_in_silence(self):
        self.check_cases(['off','left_silent','right_silent','stereo_silent'])

    def test_remap_smoothstep_and_fractional_exponent_scale_force(self):
        self.check_cases(['half','quarter','fractional','inverted','above'])

    def test_equal_bounds_and_negative_exponent_follow_native_comparisons(self):
        self.check_cases(['equal_zero','equal_positive','equal_negative','negative_exponent'])

    def test_null_and_invalid_modes_keep_native_defaults_distinct(self):
        self.check_cases(['null_bounds','null_exponent','null_mode','invalid_mode'])

    def test_audio_preserves_speed_randomness_masks_and_lifetime_weight(self):
        self.check_cases(['range','negative_speed','flag_speed','masked','blend'])

    def test_audio_does_not_scale_owner_time_or_noise_coordinates(self):
        self.check_cases(['timed'])
