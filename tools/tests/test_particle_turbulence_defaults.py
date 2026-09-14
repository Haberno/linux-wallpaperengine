"""Projection defaults measured independently through velocity, u and clocks."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_turbulence_time import write_time_probe, read_time_sample


FIELDS = ('scale', 'timescale', 'speedmin', 'speedmax', 'mask')
KINDS = (*FIELDS, 'all', 'explicit', 'zero_scale', 'zero_speed', 'zero_mask', 'flags4',
         *('null_'+field for field in FIELDS))
CASES = tuple(projection+'_'+kind for projection in ('ortho', 'perspective') for kind in KINDS)


def settings(case):
    projection, kind = case.split('_', 1)
    perspective = projection == 'perspective'
    defaults = dict(scale=.5 if perspective else .01, timescale=1 if perspective else 20,
                    speedmin=1 if perspective else 500, speedmax=5 if perspective else 1000,
                    mask='1 1 1' if perspective else '1 1 0')
    clock_speed = 4 if perspective else 800
    explicit = dict(scale=defaults['scale'], timescale=0, speedmin=clock_speed,
                    speedmax=clock_speed, mask='1 1 1')
    omitted = FIELDS if kind in ('all', 'flags4') else (kind,) if kind in FIELDS else ()
    if kind == 'explicit':
        explicit = dict(defaults)
    if kind == 'zero_scale':
        explicit['scale'] = 0
    if kind == 'zero_speed':
        explicit.update(speedmin=0, speedmax=0)
    if kind == 'zero_mask':
        explicit['mask'] = '0 0 0'
    authored = {k:v for k,v in explicit.items() if k not in omitted}
    if kind.startswith('null_'):
        authored[kind.removeprefix('null_')] = None
    effective = dict(defaults, **authored)
    for field, value in effective.items():
        if value is None:
            effective[field] = '0 0 0' if field=='mask' else 0
    return dict(perspective=perspective, kind=kind, authored=authored,
                effective=effective, base=.05/defaults['scale'],
                clock_speed=clock_speed, rate=.1)


def write_defaults_probe(root, case, candidate=False):
    config = settings(case)
    scene = write_time_probe(root, 'zero', candidate)
    path = root/'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['flags'] = 4 if config['kind']=='flags4' else 0
    definition['emitter'][0]['origin'] = ' '.join([str(config['base'])]*3)
    definition['operator'][0] = dict(name='turbulence', phasemin=0, phasemax=0,
                                     audioprocessingmode=0, **config['authored'])
    path.write_text(json.dumps(definition))
    path = root/'particles/clock.json'
    clock = json.loads(path.read_text())
    clock['operator'][0].update(speedinner=config['clock_speed'], speedouter=config['clock_speed'])
    path.write_text(json.dumps(clock))
    for layer in scene['objects'][:2]:
        layer['instanceoverride']['rate'] = config['rate']
    if config['perspective']:
        scene['general'].update(orthogonalprojection=None, fov=50, nearz=.1, farz=1000)
        # Match the orthographic view's extent at z=0 while using an actual
        # perspective camera; local particle coordinates remain unchanged.
        scene['camera'] = dict(eye='160 90 193.00562285', center='160 90 0', up='0 1 0')
    shader = root/'shaders/genericparticle.vert'
    source = shader.read_text().replace('/200.0,', '/'+str(config['clock_speed']/4)+',')
    if case == 'ortho_flags4':
        # Flag 4 gives this particle a separate perspective render camera at
        # z=1000. Compensate only the diagnostic geometry and layer placement;
        # the simulated local coordinates and scene projection stay unchanged.
        factor = 1000/193.00562285
        scene['camera']['fov'] = 50
        scene['objects'][0]['origin'] = f'160 {90-60*factor} 0'
        source = source.replace('20.0', f'(a_Position.x<20.0?{20*factor}:20.0)')
    shader.write_text(source)
    (root/'scene.json').write_text(json.dumps(scene))
    return scene


def verify_defaults_sample(case, sample, requested_clock=True):
    config = settings(case)
    op = config['effective']
    u = sample['particles'][0]['random_code']/65535
    q = (config['base'] + op['timescale']*config['rate']*sample['time'])*op['scale']
    assert 0<=q<=.18, ('Outside independent diagonal field domain', q, sample)
    noise = 64*q*(.6-3*q*q)**4
    speed = op['speedmin']+u*(op['speedmax']-op['speedmin'])
    masks = list(map(float, op['mask'].split()))
    coefficients = [noise*speed/config['clock_speed']*m for m in masks]
    # Bound the independent RGB24 time and size16 u measurements. The native
    # diagonal field derivative is bounded by 8.2944 over this cell.
    q_error = abs(op['scale']*op['timescale']*config['rate'])*1e-5
    speed_error = abs(op['speedmax']-op['speedmin'])/65535
    bounds = [(8.2944*q_error*(abs(speed)+speed_error)+abs(noise)*speed_error)
              /config['clock_speed']*abs(m) for m in masks]
    clock = sample['force_clock'][2]-127.5
    expected = [127.5+c*clock for c in coefficients]
    errors = [abs(a-b) for a,b in zip(sample['particles'][0]['rgb'], expected)]
    tolerances = [.6+.5*abs(c)+(abs(clock)+.5)*b for c,b in zip(coefficients, bounds)]
    sample.update(expected=expected, errors=errors, tolerances=tolerances, q=q,
                  requested_clock_error=abs(clock-51))
    if requested_clock:
        assert sample['requested_clock_error']<=1, sample
    assert all(0<x<255 for x in sample['particles'][0]['rgb']), ('Clipped force', sample)
    assert all(e<=t for e,t in zip(errors, tolerances)), (case, sample)
    return max(errors)


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleTurbulenceDefaults(unittest.TestCase):
    def check_cases(self, kinds):
        for projection in ('ortho', 'perspective'):
            for kind in kinds:
                case = projection+'_'+kind
                with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='lwe-turb-defaults-'+case+'-') as directory:
                    root = Path(directory)
                    scene = write_defaults_probe(root, case, candidate=True)
                    frame, _ = render_scene(self, root, scene, frames=60, fps=60)
                    self.assertIn((0,255,0), (frame.getpixel((300,10)),frame.getpixel((300,170))))
                    verify_defaults_sample(case, read_time_sample(frame, case))

    def test_omitted_scale_uses_scene_projection(self):
        self.check_cases(['scale'])

    def test_omitted_timescale_uses_scene_projection(self):
        self.check_cases(['timescale'])

    def test_each_omitted_speed_endpoint_uses_scene_projection(self):
        self.check_cases(['speedmin', 'speedmax'])

    def test_omitted_mask_uses_scene_projection(self):
        self.check_cases(['mask'])

    def test_all_defaults_match_explicit_native_values(self):
        self.check_cases(['explicit', 'all'])

    def test_explicit_zero_values_do_not_select_defaults(self):
        self.check_cases(['zero_scale', 'zero_speed', 'zero_mask'])

    def test_particle_orientation_flag_does_not_choose_projection_defaults(self):
        self.check_cases(['flags4'])

    def test_explicit_null_values_are_zero_instead_of_omitted(self):
        self.check_cases(['null_'+field for field in FIELDS])
