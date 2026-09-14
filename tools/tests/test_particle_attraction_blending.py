"""Fixed-step velocity readbacks for control-point attraction lifetime blending."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_motion_controlpoint import write_motion_probe


def write_attraction_probe(root, case, candidate=False):
    scene = write_motion_probe(root, 'baseline', candidate)
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['initializer'][0].update(min=1, max=1)
    definition['initializer'][-1].update(min='0 0 0', max='0 0 0')
    operator = dict(name='controlpointattract', controlpoint=1, scale=5000, threshold=100)
    if case != 'baseline':
        definition['operator'].append(operator)
    steps = 1
    if case == 'inactive':
        operator.update(blendinstart=.2, blendinend=.5)
    elif case == 'half':
        operator.update(blendinstart=-1, blendinend=1)
    elif case == 'overlap':
        operator.update(blendinstart=-1, blendinend=1, blendoutstart=-1, blendoutend=1)
    elif case == 'ended':
        operator.update(blendoutstart=-.5, blendoutend=0)
    elif case in ('aged', 'fadein', 'fadeout', 'long_lifetime'):
        steps = 3
        if case == 'fadein':
            operator.update(blendinstart=0, blendinend=.2)
        if case in ('fadeout', 'long_lifetime'):
            operator.update(blendoutstart=0, blendoutend=.2)
        if case == 'long_lifetime':
            definition['initializer'][0].update(min=2, max=2)
    elif case == 'sharp':
        operator.update(blendinstart=.2, blendinend=.2)
    elif case == 'tiny':
        operator.update(blendinstart=.2, blendinend=.201, blendoutstart=.202, blendoutend=.203)
    elif case in ('speed_zero', 'speed_disabled', 'speed_capped'):
        scene['objects'][0]['instanceoverride']['speed'] = 2 if case == 'speed_capped' else 0
        if case == 'speed_disabled':
            definition['flags'] = 16
    elif case in ('point_seven', 'point_negative', 'point_high'):
        operator['controlpoint'] = {'point_seven': 7, 'point_negative': -1, 'point_high': 8}[case]
        definition['controlpoint'][7]['offset'] = '70 0 0'
    elif case in ('center', 'radius', 'outside', 'zero_radius', 'negative_radius'):
        definition['controlpoint'][1]['offset'] = {'center': '10 0 0', 'radius': '110 0 0',
            'outside': '120 0 0', 'zero_radius': '50 0 0', 'negative_radius': '50 0 0'}[case]
        if case in ('zero_radius', 'negative_radius'):
            operator['threshold'] = 0 if case == 'zero_radius' else -100
    elif case in ('ratio_half', 'ratio_repulsion', 'ratio_speed', 'ratio_speed_disabled'):
        definition['emitter'][0]['origin'] = '0 0 0'
        definition['controlpoint'][2]['offset'] = '0 50 0'
        operator.update(scale=4000, flags=0)
        paired = dict(operator, controlpoint=2, blendinstart=-1, blendinend=1)
        if case == 'ratio_repulsion':
            paired['scale'] = -4000
        definition['operator'].append(paired)
        if case in ('ratio_speed', 'ratio_speed_disabled'):
            scene['objects'][0]['instanceoverride']['speed'] = .5
            if case == 'ratio_speed_disabled':
                definition['flags'] = 16
    elif case == 'point_zero':
        operator['controlpoint'] = 0
    elif case in ('offset', 'origin'):
        operator[case] = '20 0 0'
    layer = scene['objects'][0]
    layer['visible']['script'] = layer['visible']['script'].replace('steps<1', f'steps<{steps}').replace('ticks>=4','ticks>=10').replace('engine.frametime<.08','engine.frametime<.024')
    scene['objects'][-1]['color']['script'] = scene['objects'][-1]['color']['script'].replace('==1', f'=={steps}')
    shader = root / 'shaders/genericparticle.vert'
    shader.write_text(shader.read_text().replace('/200.0', '/400.0'))
    path.write_text(json.dumps(definition))
    (root / 'scene.json').write_text(json.dumps(scene))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleAttractionBlending(unittest.TestCase):
    def check_velocity(self, case, expected):
        with tempfile.TemporaryDirectory(prefix='lwe-attraction-' + case + '-') as directory:
            root = Path(directory)
            scene = write_attraction_probe(root, case, candidate=True)
            frame, _ = render_scene(self, root, scene, frames=30, fps=60)
            self.assertIn((0, 255, 0), (frame.getpixel((300, 10)), frame.getpixel((300, 170))))
            for actual, velocity in zip(frame.getpixel((160, 90)), expected):
                self.assertAlmostEqual(actual, (0.5 + velocity / 400) * 255, delta=1)

    def test_lifetime_blending_prevents_attraction_before_activation(self):
        self.check_velocity('baseline', (0, 0, 0))
        self.check_velocity('inactive', (0, 0, 0))


    def test_native_cap_applies_before_lifetime_blending(self):
        self.check_velocity('constant', (40, 0, 0))
        self.check_velocity('half', (20, 0, 0))
        self.check_velocity('overlap', (10, 0, 0))
        self.check_velocity('ended', (0, 0, 0))

    def test_envelope_uses_normalized_age_across_frames(self):
        self.check_velocity('aged', (120, 0, 0))
        self.check_velocity('fadein', (30, 0, 0))
        self.check_velocity('fadeout', (90, 0, 0))
        self.check_velocity('long_lifetime', (105, 0, 0))

    def test_equal_and_tiny_blends_follow_native_selection(self):
        self.check_velocity('sharp', (0, 0, 0))
        self.check_velocity('tiny', (40, 0, 0))

    def test_origin_and_offset_do_not_move_the_attraction_center(self):
        self.check_velocity('origin', (40, 0, 0))
        self.check_velocity('offset', (40, 0, 0))

    def test_system_flag_disables_speed_override_before_the_cap(self):
        self.check_velocity('speed_zero', (0, 0, 0))
        self.check_velocity('speed_disabled', (40, 0, 0))
        self.check_velocity('speed_capped', (40, 0, 0))

    def test_control_point_indices_use_native_unsigned_clamping(self):
        for case in ('point_seven', 'point_negative', 'point_high'):
            self.check_velocity(case, (60, 0, 0))
        self.check_velocity('point_zero', (-10, 0, 0))

    def test_attraction_excludes_the_center_and_radius_boundary(self):
        for case in ('center', 'radius', 'outside', 'zero_radius', 'negative_radius'):
            self.check_velocity(case, (0, 0, 0))

    def test_uncapped_forces_keep_signed_lifetime_weight(self):
        self.check_velocity('ratio_half', (100, 50, 0))
        self.check_velocity('ratio_repulsion', (100, -50, 0))
        self.check_velocity('ratio_speed', (50, 25, 0))
        self.check_velocity('ratio_speed_disabled', (100, 50, 0))
