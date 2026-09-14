"""Turbulence correlations measured from each particle's stable random value."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_turbulence_noise import write_noise_probe


CASES = ('speed', 'phase', 'coupled', 'shifted_phase', 'equal_phase',
         'reverse_phase', 'reverse_speed', 'negative_speed', 'mask',
         'alpha_first', 'size_first', 'double', 'inactive', 'half',
         'repeated', 'multiple')


def settings(case):
    op = dict(name='turbulence', scale=.1, speedmin=600, speedmax=1000,
              phasemin=0, phasemax=.8, timescale=0, mask='1 1 1', audioprocessingmode=0)
    if case == 'speed':
        op['phasemax'] = 0
    if case in ('phase', 'equal_phase'):
        op.update(speedmin=800, speedmax=800)
    if case == 'shifted_phase':
        op.update(phasemin=2, phasemax=2.8)
    if case == 'equal_phase':
        op.update(phasemin=2, phasemax=2)
    if case == 'reverse_phase':
        op.update(phasemin=.8, phasemax=0)
    if case == 'reverse_speed':
        op.update(speedmin=1000, speedmax=600)
    if case == 'negative_speed':
        op.update(speedmin=-1000, speedmax=-600)
    if case == 'mask':
        op['mask'] = '.5 -1 .25'
    if case == 'inactive':
        op.update(blendinstart=.2, blendinend=.5)
    if case == 'half':
        op.update(blendinstart=-1, blendinend=1)
    operators = [op]
    if case == 'double':
        operators.append(dict(op, speedmin=300, speedmax=600, phasemax=-.2, mask='-.5 .25 1'))
    return operators


def write_random_probe(root, case, candidate=False, temporal=False):
    scene = write_noise_probe(root, 'xyz', candidate)
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['initializer'][0].update(min=100, max=100)
    base = 1.2 if case == 'reverse_phase' else .3
    emitter = dict(definition['emitter'][0], origin=f'{base} {base} {base}')
    count = 8 if case == 'multiple' else 1
    definition['maxcount'] = count
    definition['emitter'] = [dict(emitter, origin=' '.join([str(base+i*.1)]*3)) for i in range(count)]
    definition['operator'] = settings(case)
    scalar = dict(name='oscillatesize', frequencymin=0, frequencymax=0,
                  phasemin=0, phasemax=0, scalemin=0, scalemax=2)
    definition['operator'].insert(0 if case == 'size_first' else len(definition['operator']), scalar)
    if case == 'alpha_first':
        definition['operator'].insert(0, dict(scalar, name='oscillatealpha'))
    path.write_text(json.dumps(definition))
    scene['objects'][0]['origin'] = '160 30 0'
    if case == 'repeated' or temporal:
        for layer, key in zip(scene['objects'][:2], ('noise', 'clock')):
            layer['visible']['script'] = f'''let ticks=0,steps=0,hold=0;
export function init(v){{thisLayer.instance.rate=0;return v;}}
export function update(v){{ticks++;hold+=engine.frametime;thisLayer.instance.rate=0;
if(ticks>=10&&steps<4&&{'(steps==0||hold>1.5)&&' if temporal else ''}engine.frametime>0&&engine.frametime<.024){{
thisLayer.instance.rate=.0125/engine.frametime;hold=0;shared.{key}Steps=++steps;shared.{key}StepTick=ticks;}}return v;}}'''
        scene['objects'][-1]['color']['script'] = (
            'export function update(v){return shared.noiseSteps==shared.clockSteps&&shared.noiseStepTick==shared.clockStepTick?'
            'new Vec3(.2,1,(shared.noiseSteps||0)/10):new Vec3(1,0,0);}' if temporal else
            scene['objects'][-1]['color']['script'].replace('Steps==1', 'Steps==4'))
    shader = root / 'shaders/genericparticle.vert'
    source = shader.read_text().replace('/200.0,1.0);', '/200.0,a_TexCoordVec4.w/30.0);')
    # Geometry does not depend on simulated size, velocity or alpha. Native
    # size is a float attribute; alpha alone would quantize u to eight bits.
    source = source.replace('60.0', '20.0')
    if case == 'multiple':
        offset = '(a_Position.x-.3)*400.0-140.0'
        source = source.replace('gl_Position=vec4(0.0,0.0,0.0,20.0);',
                                f'gl_Position=vec4({offset},0.0,0.0,20.0);')
        source = source.replace('vec3 position=vec3(', f'vec3 position=vec3({offset},0.0,0.0)+vec3(')
        # The clock shares the shader but has a different authored position.
        source = source.replace(offset, f'(a_Position.x>20.0?0.0:{offset})')
    shader.write_text(source)
    (root / 'shaders/genericparticle.frag').write_text('''varying vec4 v_Color;
varying vec2 v_TexCoord;
void main(){
 if(v_TexCoord.x<.3){
  float code=floor(clamp(v_Color.a,0.0,1.0)*65535.0+.5);
  float high=floor(code/256.0);
  gl_FragColor=vec4(high/255.0,(code-high*256.0)/255.0,1.0,1.0);
 }else{gl_FragColor=vec4(v_Color.rgb,1.0);}
}''')
    (root / 'scene.json').write_text(json.dumps(scene))
    return scene


def read_samples(frame, case):
    clock = frame.getpixel((80,90))
    assert all(abs(c-127.5)<=1 for c in clock[:2]) and 128<clock[2]<255, ('Invalid force clock', clock)
    centers = [20+40*i for i in range(8)] if case == 'multiple' else [160]
    samples = []
    for x in centers:
        rows = [y for y in (30,150) if frame.getpixel((x,y)) != (0,0,0)]
        assert len(rows)==1, ('Missing or ambiguous particle', x, rows)
        y = rows[0]
        color = frame.getpixel((x,y))
        patches = {frame.getpixel((x+dx,y+dy)) for dx,dy in ((-7,0),(7,0),(0,-7),(0,7))
                   if frame.getpixel((x+dx,y+dy))[2]==255 and frame.getpixel((x+dx,y+dy))!=color}
        assert len(patches)==1, ('Missing or ambiguous random fraction', x, patches)
        patch = patches.pop()
        samples.append(dict(rgb=list(color), random_code=patch[0]*256+patch[1]))
    return dict(force_clock=list(clock), particles=samples)


def expected_force(case, random, index=0):
    base = 1.2 if case == 'reverse_phase' else .3+index*.1
    result = [0.,0.,0.]
    for op in settings(case):
        q = (base + random*(op['phasemax']-op['phasemin'])) * op['scale']
        # Independently reduced native diagonal field in cell zero: other
        # corners attenuate away or have a zero gradient dot product.
        noise = 64*q*(.6-3*q*q)**4
        speed = op['speedmin']+random*(op['speedmax']-op['speedmin'])
        weight = 0 if case=='inactive' else .5 if case=='half' else 1
        for axis,mask in enumerate(map(float,op['mask'].split())):
            result[axis] += noise*speed*weight*mask
    return result


def verify_sample(case, sample, requested_clock=True):
    clock = sample['force_clock']
    if requested_clock:
        assert abs(clock[2]-229.5)<=1, ('Requested force step', clock)
    errors = []
    for index,particle in enumerate(sample['particles']):
        random = particle['random_code']/65535
        force = expected_force(case,random,index)
        particle['expected_at_measured_clock'] = [127.5+f/1600*(clock[2]-127.5) for f in force]
        particle['errors'] = [abs(a-b) for a,b in zip(particle['rgb'],particle['expected_at_measured_clock'])]
        # Half-code output/clock uncertainty plus .1 for u's16-bit packing,
        # native tangent normalization and floating point arithmetic.
        tolerances = [.5+.5*abs(f/1600)+.1 for f in force]
        particle['tolerances'] = tolerances
        assert all(0<c<255 for c in particle['rgb']), ('Clipped field', particle)
        assert all(e<=t for e,t in zip(particle['errors'],tolerances)), (case,index,particle)
        errors.extend(particle['errors'])
    if case == 'multiple':
        assert len({p['random_code'] for p in sample['particles']})>1, 'Particles must have distinct random samples'
    return max(errors)


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleTurbulenceRandom(unittest.TestCase):
    def check_cases(self, cases):
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='lwe-turb-random-'+case+'-') as directory:
                root = Path(directory)
                scene = write_random_probe(root,case,candidate=True)
                frame,_ = render_scene(self,root,scene,frames=30,fps=60)
                self.assertIn((0,255,0),(frame.getpixel((300,10)),frame.getpixel((300,170))))
                verify_sample(case,read_samples(frame,case))

    def test_equal_nonzero_phase_endpoints_produce_zero_offset(self):
        self.check_cases(['equal_phase'])

    def test_speed_and_phase_share_the_particle_random_fraction(self):
        self.check_cases(['speed','phase','coupled'])

    def test_phase_span_omits_the_minimum_and_offsets_all_axes(self):
        self.check_cases(['shifted_phase','reverse_phase'])

    def test_signed_ranges_and_axis_masks_preserve_correlation(self):
        self.check_cases(['reverse_speed','negative_speed','mask'])

    def test_turbulence_and_oscillators_share_randomness_in_both_orders(self):
        self.check_cases(['alpha_first','size_first','double'])

    def test_lifetime_weight_preserves_sampled_force(self):
        self.check_cases(['inactive','half'])

    def test_randomness_is_stable_across_steps_and_independent_per_particle(self):
        self.check_cases(['repeated','multiple'])
