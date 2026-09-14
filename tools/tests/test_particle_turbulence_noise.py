"""Explicit static spatial probes for the native turbulence simplex field."""
import copy
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_turbulence_blending import write_turbulence_probe


POSITIONS = {
    'origin': (0,0,0),
    'central_x': (.05,0,0),
    'xyz': (.3,.2,.1), 'xzy': (.3,.1,.2),
    'yxz': (.2,.3,.1), 'yzx': (.1,.3,.2),
    'zxy': (.2,.1,.3), 'zyx': (.1,.2,.3),
    'tie_yz': (.1,.3,.3), 'tie_xy': (.3,.3,.1),
    'tie_xz': (.3,.1,.3), 'tie_all': (.2,.2,.2),
    'negative': (-.3,-.2,-.1), 'mixed': (-.7,.25,1.1),
    'integer_negative': (-1,-2,-3), 'far': (12.125,-7.25,3.75),
    'hash_corner': (1.3,2.1,3.2), 'wrapped': (256.1,.2,-.3),
    'fourth_corner': (.4,.4,.4), 'rank_tie': (.1,.4,.4),
    'scaled': (3,2,1), 'negative_scale': (-.3,-.2,-.1),
    'masked': (.3,.2,.1), 'negative_speed': (.3,.2,.1), 'zero_scale': (.3,.2,.1),
}

# Recovered native scalar-kernel values, independently evaluated before the
# implementation. Cyclic samples are XYZ, ZXY, YZX in native Y-up space.
EXPECTED = {
    'origin': (0,0,0), 'central_x': (.203925595,.203925595,0),
    'xyz': (.747236490,.512414038,.459884644),
    'xzy': (.632986784,.686017990,.399469018),
    'yxz': (.686017990,.399469018,.632986784),
    'yzx': (.512414038,.459884644,.747236490),
    'zxy': (.459884644,.747236490,.512414038),
    'zyx': (.399469018,.632986784,.686017990),
    'tie_yz': (.255982637,.470094383,.542550445),
    'tie_xy': (.542550445,.255982637,.470094383),
    'tie_xz': (.470094383,.542550445,.255982637),
    'tie_all': (.679477215,.679477215,.679477215),
    'negative': (-.496379316,-.004621550,-.038599566),
    'mixed': (-.915374756,.344547123,-.392219871),
    'integer_negative': (0,0,0),
    'far': (.520894349,-.162408710,-.494662315),
    'hash_corner': (.628044486,-.635468841,.366968602),
    'wrapped': (.237908944,-.199134052,-.489899576),
    'fourth_corner': (.005308409,.005308409,.005308409),
    'rank_tie': (-.213692456,.412096322,.136049703),
    'scaled': (.747236490,.512414038,.459884644),
    'negative_scale': (.747236490,.512414038,.459884644),
    'masked': (.373618245,-.512414038,0),
    'negative_speed': (-.747236490,-.512414038,-.459884644),
    'zero_scale': (0,0,0),
}


def write_noise_probe(root, case, candidate=False):
    scene = write_turbulence_probe(root, 'constant', candidate)
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['emitter'][0]['origin'] = ' '.join(map(str, POSITIONS[case]))
    definition['operator'][0]['scale'] = 1
    operator = definition['operator'][0]
    if case in ('scaled', 'negative_scale', 'zero_scale'):
        operator['scale'] = {'scaled': .1, 'negative_scale': -1, 'zero_scale': 0}[case]
    if case == 'masked':
        operator['mask'] = '.5 -1 0'
    if case == 'negative_speed':
        operator.update(speedmin=-800,speedmax=-800)
    path.write_text(json.dumps(definition))
    # A known V2 vortex tangent records the force clock in this same frame.
    # This separates native timing variation from the spatial noise field.
    calibration = copy.deepcopy(definition)
    calibration['emitter'][0]['origin'] = '40 0 0'
    calibration['operator'] = [dict(name='vortex_v2',controlpoint=0,axis='0 1 0',flags=0,
                                    distanceinner=100,distanceouter=200,speedinner=1600,
                                    speedouter=1600,centerforce=0)]
    (root/'particles/clock.json').write_text(json.dumps(calibration))
    layer = copy.deepcopy(scene['objects'][0])
    layer.update(id=3,name='Force clock',particle='particles/clock.json',origin='80 90 0')
    for target, key in ((scene['objects'][0], 'noise'), (layer, 'clock')):
        target['visible']['script'] = target['visible']['script'].replace(
            'shared.motionSteps=++steps;',
            f'shared.{key}Steps=++steps;shared.{key}StepTick=ticks;')
    scene['objects'][-1]['color']['script'] = scene['objects'][-1]['color']['script'].replace(
        'shared.motionSteps==1',
        'shared.noiseSteps==1&&shared.clockSteps==1&&shared.noiseStepTick==shared.clockStepTick')
    scene['objects'].insert(1,layer)
    (root/'scene.json').write_text(json.dumps(scene))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleTurbulenceNoise(unittest.TestCase):
    def sample(self, case):
        with tempfile.TemporaryDirectory(prefix='lwe-noise-'+case+'-') as directory:
            root=Path(directory)
            scene=write_noise_probe(root,case,candidate=True)
            frame,_=render_scene(self,root,scene,frames=30,fps=60)
            self.assertIn((0,255,0),(frame.getpixel((300,10)),frame.getpixel((300,170))))
            clock=frame.getpixel((80,90))
            for value,want in zip(clock,(127.5,127.5,229.5)):
                self.assertAlmostEqual(value,want,delta=1)
            color=frame.getpixel((160,90))
            for value,noise in zip(color,EXPECTED[case]):
                self.assertAlmostEqual(value,127.5+.5*noise*(clock[2]-127.5),
                                       delta=.5+.25*abs(noise)+.1)
            return color

    def check_cases(self, cases):
        for case in cases:
            with self.subTest(case=case):
                for value, noise in zip(self.sample(case), EXPECTED[case]):
                    # One .05s step, speed800, velocity encoded over200 units.
                    self.assertAlmostEqual(value,(.5+noise*.2)*255,delta=1)

    def test_origin_has_no_turbulence_force(self):
        self.check_cases(['origin','zero_scale'])

    def test_cyclic_samples_preserve_field_magnitude(self):
        self.check_cases(['central_x','xyz','xzy','yxz','yzx','zxy','zyx'])

    def test_native_fourth_corner_hash_is_preserved(self):
        self.check_cases(['fourth_corner','hash_corner'])

    def test_native_coordinate_ties_are_preserved(self):
        self.check_cases(['rank_tie','tie_yz','tie_xy','tie_xz','tie_all'])

    def test_negative_and_wrapped_cells_use_native_gradients(self):
        self.check_cases(['negative','mixed','integer_negative','far','wrapped'])

    def test_authored_scale_has_no_extra_multiplier(self):
        self.check_cases(['scaled','negative_scale'])

    def test_masks_and_negative_speed_preserve_signed_force(self):
        self.check_cases(['masked','negative_speed'])
