"""Controlled velocity readbacks for native vortex_v2 lifetime blending."""
import json
import math
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_motion_controlpoint import write_motion_probe


def write_vortex_probe(root, case, candidate=False):
    scene = write_motion_probe(root, 'baseline', candidate)
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['emitter'][0]['origin'] = '40 0 0'
    definition['initializer'][0].update(min=1, max=1)
    definition['initializer'][-1].update(min='0 0 0', max='0 0 0')
    operator = dict(name='vortex_v2', controlpoint=0, flags=0, axis='0 0 1',
                    distanceinner=100, distanceouter=200, speedinner=1000,
                    speedouter=0, centerforce=0)
    if case != 'baseline':
        definition['operator'].append(operator)
    if case in ('inactive', 'legacy_inactive'):
        operator.update(blendinstart=.2, blendinend=.5)
    elif case == 'half':
        operator.update(blendinstart=-1, blendinend=1)
    elif case == 'overlap':
        operator.update(blendinstart=-1, blendinend=1, blendoutstart=-1, blendoutend=1)
    elif case == 'ended':
        operator.update(blendoutstart=-.5, blendoutend=0)
    elif case == 'sharp':
        operator.update(blendinstart=.2, blendinend=.2)
    elif case == 'tiny':
        operator.update(blendinstart=.2, blendinend=.201, blendoutstart=.202, blendoutend=.203)
    elif case == 'offset':
        operator['offset'] = '40 0 0'
    elif case in ('point_seven', 'point_negative', 'point_high'):
        operator['controlpoint'] = {'point_seven': 7, 'point_negative': -1, 'point_high': 8}[case]
        definition['controlpoint'][7]['offset'] = '80 0 0'
    elif case in ('sphere', 'cylinder'):
        definition['emitter'][0]['origin'] = '30 0 40'
        operator['flags'] = 1 if case == 'cylinder' else 0
    elif case in ('axis_small', 'axis_y'):
        operator['axis'] = '0 .01 0' if case == 'axis_small' else '0 1 0'
    elif case in ('equal_span', 'reversed_span'):
        operator.update(distanceinner=39.5, distanceouter=39.5)
        if case == 'reversed_span':
            operator.update(distanceinner=60, distanceouter=20)
    elif case.startswith('center_'):
        definition['initializer'][-1].update(min='20 10 5', max='20 10 5')
        operator.update(flags=3, speedinner=0, centerforce=1)
        if case == 'center_half':
            operator.update(blendinstart=-1, blendinend=1)
        if case == 'center_inactive':
            operator.update(blendinstart=.2, blendinend=.5)
    elif case.startswith('ring_'):
        operator.update(flags=4, ringradius=60, ringwidth=10, ringpulldistance=20, ringpullforce=2)
        distance = {'ring_deadband': 50, 'ring_inside': 40, 'ring_outside': 80,
                    'ring_cutoff': 90}.get(case, 40)
        definition['emitter'][0]['origin'] = f'{distance} 0 0'
        if case == 'ring_half':
            operator.update(blendinstart=-1, blendinend=1)
        if case in ('ring_speed_zero', 'ring_speed_disabled'):
            scene['objects'][0]['instanceoverride']['speed'] = 0
            if case == 'ring_speed_disabled':
                definition['flags'] = 16
    elif case in ('defaults', 'explicit_defaults'):
        operator.clear()
        operator['name'] = 'vortex_v2'
        if case == 'explicit_defaults':
            operator.update(distanceinner=500, distanceouter=650, speedinner=2500, speedouter=0)
    if case.startswith('ratio_'):
        paired = dict(operator, axis='0 1 0', blendinstart=-1, blendinend=1)
        if case == 'ratio_overlap':
            paired.update(blendoutstart=-1, blendoutend=1)
        if case == 'ratio_negative':
            paired['speedinner'] = -1000
        definition['operator'].append(paired)
    if case.startswith('perspective_'):
        scene['general'].update(orthogonalprojection=None, fov=50, nearz=.1, farz=1000)
        scene['camera'] = {'eye': '0 0 10', 'center': '0 0 0', 'up': '0 1 0'}
        scene['objects'][0]['origin'] = '0 0 0'
        definition['emitter'][0]['origin'] = '.5 0 0'
        scene['objects'][-1].update(origin='0 0 0', size='1 1')
        operator.clear()
        operator['name'] = 'vortex_v2'
        if case == 'perspective_explicit':
            operator.update(distanceinner=1, distanceouter=2, speedinner=1, speedouter=0)
        shader = root / 'shaders/genericparticle.vert'
        shader.write_text(shader.read_text().replace('60.0', '1.0').replace('/200.0', '/.4'))
    steps = 3 if case in ('aged', 'fadein', 'fadeout', 'long_lifetime') else 1
    if case == 'fadein':
        operator.update(blendinstart=0, blendinend=.2)
    if case in ('fadeout', 'long_lifetime'):
        operator.update(blendoutstart=0, blendoutend=.2)
    if case == 'long_lifetime':
        definition['initializer'][0].update(min=2, max=2)
    if case.startswith('legacy_'):
        operator['name'] = 'vortex'
    layer = scene['objects'][0]
    layer['visible']['script'] = layer['visible']['script'].replace('steps<1', f'steps<{steps}').replace('ticks>=4', 'ticks>=10').replace('engine.frametime<.08', 'engine.frametime<.024')
    scene['objects'][-1]['color']['script'] = scene['objects'][-1]['color']['script'].replace('==1', f'=={steps}')
    if case in ('aged', 'fadeout', 'long_lifetime', 'defaults', 'explicit_defaults'):
        shader = root / 'shaders/genericparticle.vert'
        shader.write_text(shader.read_text().replace('/200.0', '/400.0'))
    path.write_text(json.dumps(definition))
    (root / 'scene.json').write_text(json.dumps(scene))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleVortexV2(unittest.TestCase):
    def check_velocity(self, case, expected):
        with tempfile.TemporaryDirectory(prefix='lwe-vortex-v2-' + case + '-') as directory:
            root = Path(directory)
            scene = write_vortex_probe(root, case, candidate=True)
            frame, _ = render_scene(self, root, scene, frames=30, fps=60)
            self.assertIn((0, 255, 0), (frame.getpixel((300, 10)), frame.getpixel((300, 170))))
            scale = .4 if case.startswith('perspective_') else 400 if case in (
                'aged', 'fadeout', 'long_lifetime', 'defaults', 'explicit_defaults') else 200
            for actual, velocity in zip(frame.getpixel((160, 90)), expected):
                self.assertAlmostEqual(actual, (0.5 + velocity / scale) * 255, delta=1)

    def test_lifetime_envelope_prevents_force_before_activation(self):
        self.check_velocity('baseline', (0, 0, 0))
        self.check_velocity('inactive', (0, 0, 0))

    def test_lifetime_weight_scales_spin(self):
        for case, speed in [('constant', -50), ('half', -25), ('overlap', -12.5),
                            ('ended', 0), ('sharp', 0), ('tiny', -50)]:
            self.check_velocity(case, (0, speed, 0))

    def test_lifetime_uses_age_and_lifetime_across_frames(self):
        for case, speed in [('aged', -150), ('fadein', -37.5), ('fadeout', -112.5), ('long_lifetime', -131.25)]:
            self.check_velocity(case, (0, speed, 0))

    def test_legacy_ignores_v2_envelope(self):
        self.check_velocity('legacy_constant', (0, -50, 0))
        self.check_velocity('legacy_inactive', (0, -50, 0))

    def test_v2_ignores_legacy_offset_and_clamps_point_index(self):
        self.check_velocity('offset', (0, -50, 0))
        for case in ('point_seven', 'point_negative', 'point_high'):
            self.check_velocity(case, (0, 50, 0))

    def test_sphere_tangent_attenuates_near_axis(self):
        self.check_velocity('sphere', (0, -30, 0))
        self.check_velocity('cylinder', (0, -50, 0))
        self.check_velocity('axis_small', (0, -50, 0))
        self.check_velocity('axis_y', (0, 0, 50))

    def test_equal_and_reversed_spans(self):
        self.check_velocity('equal_span', (0, -25, 0))
        self.check_velocity('reversed_span', (0, -25, 0))

    def test_radius_correction_uses_original_velocity_and_weight(self):
        scale = 40 / math.sqrt(41 * 41 + .5 * .5 + .25 * .25) - 1
        correction = (41 * scale / .05, .5 * scale / .05, .25 * scale / .05)
        for case, weight in [('center_constant', 1), ('center_half', .5), ('center_inactive', 0)]:
            self.check_velocity(case, tuple(v + weight * d for v, d in zip((20, 10, 5), correction)))

    def test_ring_deadband_pull_and_lifetime_weight(self):
        for case, expected in [('ring_deadband', (0, -50, 0)), ('ring_inside', (2, -25, 0)),
                               ('ring_outside', (-4, -25, 0)), ('ring_cutoff', (0, 0, 0)),
                               ('ring_half', (1, -12.5, 0)), ('ring_speed_zero', (2, 0, 0)),
                               ('ring_speed_disabled', (2, -25, 0))]:
            self.check_velocity(case, expected)

    def test_projection_defaults_are_resolved(self):
        self.check_velocity('defaults', (0, -125, 0))
        self.check_velocity('explicit_defaults', (0, -125, 0))
        self.check_velocity('perspective_defaults', (0, -.05, 0))
        self.check_velocity('perspective_explicit', (0, -.05, 0))

    def test_paired_axes_share_force_clock_and_signed_envelope(self):
        self.check_velocity('ratio_half', (0, -50, 25))
        self.check_velocity('ratio_overlap', (0, -50, 12.5))
        self.check_velocity('ratio_negative', (0, -50, -25))
