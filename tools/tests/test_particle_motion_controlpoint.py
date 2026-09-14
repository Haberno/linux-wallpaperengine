"""Controlled native velocity readbacks for distance-dependent particle damping."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_inherited_velocity import write_probe, read_velocity_color
from test_text_effect_targets import write_copy_assets


def write_motion_probe(root, case, candidate=False):
    scene = write_probe(root, 'known', candidate)
    layer = scene['objects'][0]
    layer['origin'] = '160 90 0'
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['flags'] = 0
    definition['emitter'][0].update(origin='10 0 0', rate=0, delay=0, instantaneous=1)
    definition['controlpoint'] = [{'id': i, 'offset': '50 0 0' if i == 1 else '0 0 0'} for i in range(8)]
    operator = {'name': 'reducemovementnearcontrolpoint', 'controlpoint': 1,
                'distanceinner': 50, 'distanceouter': 150,
                'reductioninner': 10, 'reductionouter': 0}
    definition['operator'].append(operator)
    steps = 1
    delta = .05
    if case == 'baseline':
        definition['operator'].pop()
    elif case == 'stop':
        operator['reductioninner'] = 1000
    elif case == 'negative':
        operator['reductioninner'] = -10
    elif case == 'middle':
        definition['emitter'][0]['origin'] = '150 0 0'
    elif case == 'outside':
        definition['emitter'][0]['origin'] = '250 0 0'
    elif case == 'outer_nonzero':
        definition['emitter'][0]['origin'] = '250 0 0'
        operator['reductionouter'] = 5
    elif case == 'equal_reductions':
        operator.update(reductioninner=5, reductionouter=5)
        definition['emitter'][0]['origin'] = '250 0 0'
    elif case == 'equal_radii':
        operator['distanceouter'] = 50
        definition['emitter'][0]['origin'] = '101 0 0'
    elif case == 'center':
        definition['emitter'][0]['origin'] = '50 0 0'
    elif case == 'defaults':
        operator.clear()
        operator['name'] = 'reducemovementnearcontrolpoint'
    elif case == 'repeated':
        steps = 3
    elif case == 'smaller_delta':
        delta = .025
    elif case == 'speed':
        layer['instanceoverride']['speed'] = 2
    elif case == 'blend_start':
        operator.update(blendinstart=.2, blendinend=.5)
    elif case == 'blend_half_stop':
        operator.update(reductioninner=1000, blendinstart=-1, blendinend=1)
    elif case == 'blend_fadeout':
        operator.update(reductioninner=5, blendoutstart=0, blendoutend=1)
        definition['initializer'][0].update(min=.4, max=.4)
        steps = 3
    elif case in ('radial_y', 'radial_z'):
        definition['controlpoint'][1]['offset'] = '10 100 0' if case == 'radial_y' else '10 0 100'
    elif case == 'equal_zero':
        operator.update(reductioninner=0, reductionouter=0)
        definition['emitter'][0]['origin'] = '250 0 0'
    elif case == 'equal_negative':
        operator.update(reductioninner=-.5, reductionouter=-.5)
        definition['emitter'][0]['origin'] = '250 0 0'
    elif case == 'equal_radii_middle':
        operator['distanceouter'] = 50
        definition['emitter'][0]['origin'] = '100.5 0 0'
    elif case == 'inverted_radii':
        operator.update(distanceinner=150, distanceouter=50)
        definition['emitter'][0]['origin'] = '150 0 0'
    elif case == 'center_nonzero':
        operator['reductionouter'] = 5
        definition['emitter'][0]['origin'] = '50 0 0'
    elif case == 'point_zero':
        operator['controlpoint'] = 0
        definition['emitter'][0]['origin'] = '100 0 0'
    elif case in ('point_seven', 'point_negative', 'point_high'):
        operator['controlpoint'] = {'point_seven': 7, 'point_negative': -1, 'point_high': 8}[case]
        definition['controlpoint'][7]['offset'] = '250 0 0'
    elif case in ('local_scaled', 'world_scaled', 'world_point'):
        layer.update(origin='5 10 0', scale='2 3 1')
        if case != 'local_scaled':
            definition['flags'] = 1
            definition['emitter'][0]['origin'] = '0 0 0'
            definition['controlpoint'][1]['offset'] = '30 0 0'
        if case == 'world_point':
            definition['controlpoint'][1].update(flags=2, offset='30 10 0')
    elif case in ('perspective_defaults', 'perspective_explicit'):
        scene['general'].update(orthogonalprojection=None, fov=50, nearz=.1, farz=1000)
        scene['camera'] = {'eye': '0 0 10', 'center': '0 0 0', 'up': '0 1 0'}
        layer['origin'] = '0 0 0'
        definition['emitter'][0]['origin'] = '.98 0 0'
        operator.clear()
        operator.update(name='reducemovementnearcontrolpoint')
        if case == 'perspective_explicit':
            operator.update(controlpoint=0, distanceinner=.5, distanceouter=1,
                            reductioninner=100, reductionouter=0)
    elif case in ('before_movement', 'after_movement'):
        definition['emitter'][0]['origin'] = '105 0 0'
        definition['initializer'][-1].update(min='40 0 0', max='40 0 0')
        operator['distanceouter'] = 60
        movement = {'name': 'movement', 'gravity': '0 0 0', 'drag': 0}
        if case == 'before_movement':
            definition['operator'].append(movement)
        else:
            definition['operator'].insert(-1, movement)
    # Run a known number of fixed-duration simulation steps, then hold the
    # result. Neither native startup time nor random emitters drive the sample.
    layer['visible'] = {'value': True, 'script': f'''let ticks=0,steps=0;
export function init(v){{thisLayer.instance.rate=0;return v;}}
export function update(v){{++ticks;thisLayer.instance.rate=0;
if(ticks>=4&&steps<{steps}&&engine.frametime>0&&engine.frametime<.08){{
thisLayer.instance.rate={delta}/engine.frametime;shared.motionSteps=++steps;}}return v;}}'''}
    # Exclude delayed frames that would cross the fork's 100 ms simulation
    # cap. Both engines receive the same script; green proves all steps ran.
    write_copy_assets(root)
    (root / 'shaders/marker.vert').write_text('''attribute vec2 a_TexCoord;
void main(){gl_Position=vec4(vec2(.875,.8888889)+(a_TexCoord-.5)*vec2(.0625,-.1111111),0.0,1.0);}''')
    (root / 'shaders/marker.frag').write_text('uniform vec3 g_Color;\nvoid main(){gl_FragColor=vec4(g_Color,1.0);}')
    (root / 'materials/marker.json').write_text(json.dumps({'passes': [{'shader': 'marker',
        'blending': 'normal', 'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
    (root / 'models/marker.json').write_text(json.dumps({'material': 'materials/marker.json'}))
    scene['objects'].append({'id': 2, 'name': 'Step completion', 'origin': '300 170 0',
        'size': '10 10', 'image': 'models/marker.json', 'color': {'value': '1 0 0',
            'script': f'export function update(v){{return shared.motionSteps=={steps}?new Vec3(0,1,0):new Vec3(1,0,0);}}'}})
    if case.startswith('perspective_'):
        # Native culls by the authored image bounds before our diagnostic
        # shader positions it in screen space. Keep those bounds in view.
        scene['objects'][-1].update(origin='0 0 0', size='1 1')
    shader = root / 'shaders/genericparticle.vert'
    text = shader.read_text().replace('gl_Position=vec4(a_Position,60.0);',
                                     'gl_Position=vec4(0.0,0.0,0.0,60.0);')
    text = text.replace('vec3 position=a_Position+', 'vec3 position=')
    if case.startswith('perspective_'):
        # A 60-world-unit diagnostic covers the completion marker in native
        # perspective rendering. Keep this readback geometry near the center.
        text = text.replace('60.0', '1.0')
    shader.write_text(text)
    path.write_text(json.dumps(definition))
    (root / 'scene.json').write_text(json.dumps(scene))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleMotionControlPoint(unittest.TestCase):
    def check_velocity(self, case, expected):
        with tempfile.TemporaryDirectory(prefix='lwe-motion-point-' + case + '-') as directory:
            root = Path(directory)
            scene = write_motion_probe(root, case, candidate=True)
            frame, _ = render_scene(self, root, scene, frames=20, fps=30)
            # The orthographic output is vertically reflected relative to
            # perspective output; the diagnostic occupies the matching corner.
            self.assertIn((0, 255, 0), (frame.getpixel((300, 10)), frame.getpixel((300, 170))),
                          'All fixed simulation steps must finish')
            actual = read_velocity_color(frame)
            self.assertIsNotNone(actual, 'Velocity readback particle must be visible')
            for value, component in zip(actual, expected):
                self.assertAlmostEqual(value, component, delta=1)

    def test_strong_reduction_stops_particles_inside_the_inner_radius(self):
        self.check_velocity('stop', (127, 127, 127))

    def test_baseline_calibrates_the_velocity_readback(self):
        self.check_velocity('baseline', (178, 102, 140))

    def test_inner_distance_reduces_all_velocity_components(self):
        self.check_velocity('inner', (153, 115, 134))

    def test_distances_blend_between_inner_and_outer_reductions(self):
        self.check_velocity('middle', (166, 108, 137))
        self.check_velocity('outside', (178, 102, 140))
        self.check_velocity('outer_nonzero', (166, 108, 137))

    def test_native_negative_reduction_is_clamped_without_acceleration(self):
        self.check_velocity('negative', (178, 102, 140))

    def test_equal_reduction_endpoints_retain_the_native_one_unit_delta(self):
        self.check_velocity('equal_reductions', (163, 110, 136))
        self.check_velocity('equal_zero', (176, 103, 140))
        self.check_velocity('equal_negative', (177, 103, 140))

    def test_exact_center_selects_the_native_outer_endpoint(self):
        self.check_velocity('center', (178, 102, 140))
        self.check_velocity('center_nonzero', (166, 108, 137))

    def test_equal_radii_use_a_one_unit_transition(self):
        self.check_velocity('equal_radii', (178, 102, 140))
        self.check_velocity('equal_radii_middle', (166, 108, 137))

    def test_inverted_radii_preserve_the_signed_transition(self):
        self.check_velocity('inverted_radii', (165, 108, 137))

    def test_control_point_selection_and_unsigned_index_bounds(self):
        self.check_velocity('point_zero', (166, 108, 137))
        for case in ('point_seven', 'point_negative', 'point_high'):
            with self.subTest(case=case):
                self.check_velocity(case, (178, 102, 140))

    def test_local_and_world_distances_use_the_particle_simulation_space(self):
        self.check_velocity('local_scaled', (153, 115, 134))
        self.check_velocity('world_scaled', (183, 86, 134))
        self.check_velocity('world_point', (178, 90, 134))

    def test_distance_includes_the_y_and_z_axes(self):
        self.check_velocity('radial_y', (165, 108, 137))
        self.check_velocity('radial_z', (165, 108, 137))

    def test_perspective_distance_defaults_use_world_units(self):
        self.check_velocity('perspective_defaults', (168, 107, 138))
        self.check_velocity('perspective_explicit', (168, 107, 138))

    def test_omitted_settings_apply_native_orthographic_defaults(self):
        self.check_velocity('defaults', (127, 127, 127))

    def test_each_simulation_step_applies_damping(self):
        self.check_velocity('repeated', (134, 124, 129))
        self.check_velocity('smaller_delta', (166, 108, 137))

    def test_instance_speed_does_not_multiply_damping(self):
        self.check_velocity('speed', (178, 102, 140))

    def test_lifetime_blending_can_delay_the_operator(self):
        self.check_velocity('blend_start', (178, 102, 140))

    def test_lifetime_weight_applies_after_damping_is_clamped(self):
        self.check_velocity('blend_half_stop', (153, 115, 134))

    def test_lifetime_fadeout_reduces_damping_as_particles_age(self):
        self.check_velocity('blend_fadeout', (152, 115, 133))

    def test_authored_operator_order_controls_the_sampled_position(self):
        self.check_velocity('before_movement', (166, 127, 127))
        self.check_velocity('after_movement', (171, 127, 127))
