"""Controlled-step angle readbacks for native angular-movement lifetime blending."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_motion_controlpoint import write_motion_probe


def write_angular_probe(root, case, candidate=False):
    scene = write_motion_probe(root, 'baseline', candidate)
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['initializer'][0].update(min=1, max=1)
    definition['initializer'][-1].update(name='angularvelocityrandom', min='0 0 8', max='0 0 8')
    operator = dict(name='angularmovement', force='0 0 0', drag=0)
    definition['operator'] = [operator]
    if case == 'baseline':
        definition['initializer'][-1].update(min='0 0 0', max='0 0 0')
    if case == 'inactive':
        operator.update(blendinstart=.2, blendinend=.5)
    if case in ('half', 'overlap', 'force_half', 'force_overlap', 'drag_half', 'drag_saturated_half', 'force_drag_half'):
        operator.update(blendinstart=-1, blendinend=1)
    if case in ('overlap', 'force_overlap'):
        operator.update(blendoutstart=-1, blendoutend=1)
    if case == 'ended':
        operator.update(blendoutstart=-.5, blendoutend=0)
    if case == 'sharp':
        operator.update(blendinstart=.2, blendinend=.2)
    if case == 'tiny':
        operator.update(blendinstart=.2, blendinend=.201, blendoutstart=.202, blendoutend=.203)
    if case.startswith('force') or case in ('speed', 'speed_disabled', 'speed_zero'):
        definition['initializer'][-1].update(min='0 0 0', max='0 0 0')
        operator['force'] = '0 0 -160' if case == 'force_negative' else '0 0 160'
    if case in ('speed', 'speed_disabled', 'speed_zero'):
        scene['objects'][0]['instanceoverride']['speed'] = 0 if case == 'speed_zero' else 2
        if case == 'speed_disabled':
            definition['flags'] = 16
    if case in ('xyz', 'xyz_force'):
        definition['initializer'][-1].update(min='4 -8 12', max='4 -8 12')
        if case == 'xyz_force':
            operator['force'] = '40 80 -120'
    if 'drag' in case:
        operator['drag'] = 100 if 'saturated' in case else -10 if case == 'negative_drag' else 10
        if case == 'drag_inactive':
            operator.update(blendinstart=.2, blendinend=.5)
        # A second, force-free integration exposes the velocity remaining after
        # the first operator's drag, without a test-only runtime accessor.
        definition['operator'].append(dict(name='angularmovement', force='0 0 0', drag=0))
    if case in ('initial_angles', 'unwrapped_angles'):
        definition['initializer'][-1].update(min='0 0 0', max='0 0 0')
        angles = '.2 -.3 .4' if case == 'initial_angles' else '7 -8 9'
        definition['initializer'].append(dict(name='rotationrandom', min=angles, max=angles))
    if case.startswith('clock_'):
        definition['initializer'][-1].update(min='8 0 0', max='8 0 0')
        operator.update(force='0 0 160', drag=0)
        if case != 'clock_force':
            operator.update(blendinstart=-1, blendinend=1)
        if case == 'clock_force_overlap':
            operator.update(blendoutstart=-1, blendoutend=1)
        if case == 'clock_drag_saturated_half':
            operator['drag'] = 100
    steps = 3 if case in ('aged', 'fadein', 'fadeout', 'long_lifetime') else 1
    if case == 'fadein':
        operator.update(blendinstart=0, blendinend=.2)
    if case in ('fadeout', 'long_lifetime'):
        operator.update(blendoutstart=0, blendoutend=.2)
    if case == 'long_lifetime':
        definition['initializer'][0].update(min=2,max=2)
    layer = scene['objects'][0]
    layer['visible']['script'] = layer['visible']['script'].replace('steps<1',f'steps<{steps}').replace('ticks>=4','ticks>=10').replace('engine.frametime<.08','engine.frametime<.024')
    shader = root / 'shaders/genericparticle.vert'
    source = shader.read_text().replace('attribute vec3 a_Position;', 'attribute vec3 a_Position;\nattribute vec2 a_TexCoordC2;')
    start = source.index('    v_Color=')
    end = source.index('#if GS_ENABLED', start)
    xsign = '-1.0' if candidate else '1.0'
    source = source[:start] + '''#if GS_ENABLED
    vec3 rotation=a_TexCoordVec4.xyz;
#else
    vec3 rotation=vec3(a_TexCoordC2.xy,a_TexCoordVec4.z);
#endif
    v_Color=vec4(vec3(.5,.5,.5)+rotation*vec3(XSIGN,1.0,1.0)/4.0,1.0);
'''.replace('XSIGN',xsign) + source[end:]
    if case == 'unwrapped_angles':
        source = source.replace('/4.0','/32.0')
    scene['objects'][-1]['color']['script'] = scene['objects'][-1]['color']['script'].replace('==1',f'=={steps}')
    shader.write_text(source)
    path.write_text(json.dumps(definition))
    (root/'scene.json').write_text(json.dumps(scene))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleAngularMovement(unittest.TestCase):
    def check_rotation(self, case, expected):
        with tempfile.TemporaryDirectory(prefix='lwe-angular-' + case + '-') as directory:
            root = Path(directory)
            scene = write_angular_probe(root, case, candidate=True)
            frame, _ = render_scene(self, root, scene, frames=30, fps=60)
            self.assertIn((0,255,0), (frame.getpixel((300,10)),frame.getpixel((300,170))))
            for actual, angle in zip(frame.getpixel((160,90)),expected):
                self.assertAlmostEqual(actual,(.5+angle/(32 if case == 'unwrapped_angles' else 4))*255,delta=1)

    def test_inactive_envelope_stops_rotation(self):
        self.check_rotation('baseline',(0,0,0))
        self.check_rotation('inactive',(0,0,0))

    def test_envelope_weights_existing_angular_velocity(self):
        for case, angle in [('constant',.4),('half',.2),('overlap',.1),('ended',0),('sharp',0),('tiny',.4)]:
            self.check_rotation(case,(0,0,angle))

    def test_force_precedes_rotation_and_receives_weight_twice(self):
        for case, angle in [('force',.4),('force_half',.1),('force_overlap',.025),('force_negative',-.4)]:
            self.check_rotation(case,(0,0,angle))

    def test_drag_follows_rotation_and_caps_before_weight(self):
        for case, angle in [('drag',.6),('drag_half',.5),('drag_inactive',.4),
                            ('drag_saturated',.4),('drag_saturated_half',.4),('negative_drag',1),
                            ('force_drag',.6),('force_drag_half',.25)]:
            self.check_rotation(case,(0,0,angle))

    def test_force_respects_instance_speed_and_disable_flag(self):
        self.check_rotation('speed',(0,0,.8))
        self.check_rotation('speed_disabled',(0,0,.4))
        self.check_rotation('speed_zero',(0,0,0))

    def test_xyz_rotation_uses_authored_radians(self):
        self.check_rotation('xyz',(.2,-.4,.6))
        self.check_rotation('xyz_force',(.3,-.2,.3))
        self.check_rotation('initial_angles',(.2,-.3,.4))

    def test_angles_are_not_wrapped_by_the_operator(self):
        self.check_rotation('unwrapped_angles',(7,-8,9))

    def test_envelope_uses_normalized_lifetime_across_steps(self):
        for case, angle in [('aged',1.2),('fadein',.3),('fadeout',.9),('long_lifetime',1.05)]:
            self.check_rotation(case,(0,0,angle))

    def test_force_and_saturated_drag_have_clock_independent_angle_relations(self):
        for case, expected in [('clock_force',(.4,0,.4)),('clock_force_half',(.2,0,.1)),
                               ('clock_force_overlap',(.1,0,.025)),('clock_drag_saturated_half',(.4,0,.2))]:
            self.check_rotation(case,expected)
