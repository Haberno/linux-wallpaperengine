"""Native control-point velocity readback, independent of particle motion/shape."""
import json
import os
from collections import Counter
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene


def write_probe(root, case, candidate=False):
    for directory in ('particles', 'materials', 'shaders'):
        (root / directory).mkdir(parents=True, exist_ok=True)
    initializer = {'name': 'inheritcontrolpointvelocity', 'controlpoint': 1, 'min': 1, 'max': 1}
    definition = {'flags': 1, 'maxcount': 1, 'material': 'materials/probe.json',
                  'emitter': [{'name': 'sphererandom', 'rate': 20, 'delay': .25,
                               'distancemin': 0, 'distancemax': 0}],
                  'initializer': [{'name': 'lifetimerandom', 'min': 100, 'max': 100},
                                  {'name': 'sizerandom', 'min': 60, 'max': 60}, initializer],
                  # Integrate age without changing the velocity being measured.
                  'operator': [{'name': 'angularmovement', 'drag': 0, 'force': '0 0 0'}],
                  'renderer': [{'name': 'spritetrail'}]}
    script = '''let elapsed=0;
export function update(value) {
    elapsed+=engine.frametime;
    if(elapsed<1) value.x+=20*engine.frametime;
    return value;
}
'''
    layer = {'id': 1, 'name': 'Velocity probe', 'origin': {'value': '100 90 0', 'script': script},
             'particle': 'particles/probe.json', 'instanceoverride': {}}
    scene = base_scene([layer])
    if case == 'known':
        initializer.update(name='velocityrandom', min='40 -20 10', max='40 -20 10')
    if case == 'local':
        definition['flags'] = 0
    if case == 'static':
        layer['origin'] = '100 90 0'
    if case == 'scaled':
        layer['scale'] = '2 3 1'
    if case == 'mirrored':
        layer['scale'] = '-2 1 1'
    if case == 'rotated':
        layer['angles'] = '0 0 1.570796327'
    if case in ('worldpoint', 'localworldpoint'):
        definition['controlpoint'] = [{'id': i, 'flags': 2 if i == 1 else 0,
                                       'offset': '100 90 0' if i == 1 else '0 0 0'} for i in range(8)]
        if case == 'localworldpoint':
            definition['flags'] = 0
    if case in ('worldzero', 'worldzero_scaled'):
        definition['controlpoint'] = [{'id': i, 'flags': 2 if i == 0 else 0,
                                       'offset': '0 0 0'} for i in range(8)]
        initializer['controlpoint'] = 0
        if case == 'worldzero_scaled':
            layer['scale'] = '2 3 1'
    if case == 'zero':
        initializer['controlpoint'] = 0
    if case == 'missing':
        initializer.clear()
        initializer['name'] = 'inheritcontrolpointvelocity'
    if case == 'half':
        initializer.update(min=.5, max=.5)
    if case == 'negative':
        initializer.update(min=-.5, max=-.5)
    if case == 'add':
        definition['initializer'].insert(2, {'name': 'velocityrandom', 'min': '10 0 0', 'max': '10 0 0'})
    if case in ('speed', 'rate'):
        layer['instanceoverride'][case] = 2
    if case == 'parent':
        scene['objects'].insert(0, {'id': 2, 'name': 'Mover', 'origin': layer['origin'], 'scale': '2 3 1'})
        layer.update(origin='10 0 0', parent=2)
    if case == 'first':
        definition['emitter'][0].update(delay=0, instantaneous=1, rate=0)
        layer['origin'] = '100 90 0'
    if case == 'diagonal':
        layer['origin']['script'] = script.replace('value.x+=20*engine.frametime;',
            '{value.x+=20*engine.frametime;value.y+=10*engine.frametime;value.z+=5*engine.frametime;}')
    if case in ('manual', 'lastmove', 'afterstop'):
        definition['emitter'][0].update(delay=0, rate=0)
        motion = 'count<20' if case == 'manual' else 'count==10' if case == 'lastmove' else 'count<10'
        birth = 11 if case == 'afterstop' else 10
        layer['origin']['script'] = f'''let count=0;
export function init(value) {{thisLayer.pause();return value;}}
export function update(value) {{
    count++;
    if({motion}) value.x+=20*engine.frametime;
    if(count=={birth}) thisLayer.emitParticles(1);
    return value;
}}
'''
    # Both engines get the same authored scene. The fork's simulation Y axis
    # points down, so normalize only the diagnostic output to native Y-up.
    y_sign = '-1.0' if candidate else '1.0'
    (root / 'shaders/genericparticle.vert').write_text('''
attribute vec3 a_Position;
attribute vec4 a_TexCoordVec4;
attribute vec4 a_TexCoordVec4C1;
varying vec4 v_Color;
#if GS_ENABLED
varying vec3 v_Rotation;
varying vec4 v_VelocityLifetime;
#else
uniform mat4 g_ModelViewProjectionMatrix;
varying vec2 v_TexCoord;
#endif
void main() {
    v_Color=vec4(vec3(.5,.5,.5)+a_TexCoordVec4C1.xyz*vec3(1.0,Y_SIGN,1.0)/200.0,1.0);
#if GS_ENABLED
    gl_Position=vec4(a_Position,60.0);
    v_Rotation=vec3(0.0,0.0,0.0);
    // Keep zero velocity particles visible instead of degenerating the trail.
    v_VelocityLifetime=vec4(20.0,0.0,0.0,0.0);
#else
    vec3 position=a_Position+vec3((a_TexCoordVec4.xy-.5)*60.0,0.0);
    gl_Position=mul(vec4(position,1.0),g_ModelViewProjectionMatrix);
    v_TexCoord=a_TexCoordVec4.xy;
#endif
}
'''.replace('Y_SIGN', y_sign))
    (root / 'shaders/genericparticle.frag').write_text('''
varying vec4 v_Color;
void main() {
    gl_FragColor=v_Color;
}
''')
    material = {'passes': [{'shader': 'genericparticle', 'textures': ['util/white'],
                           'blending': 'normal', 'depthtest': 'disabled',
                           'depthwrite': 'disabled', 'cullmode': 'nocull'}]}
    for filename, data in [('particles/probe.json', definition), ('materials/probe.json', material),
                           ('scene.json', scene), ('project.json', {'type': 'scene', 'file': 'scene.json',
                                                                 'title': 'Velocity contract probe'})]:
        (root / filename).write_text(json.dumps(data))
    return scene


def read_velocity_color(frame):
    colors = Counter(frame.getdata())
    colors.pop((0, 0, 0), None)
    return colors.most_common(1)[0][0] if colors else None


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleInheritedVelocity(unittest.TestCase):
    def check_velocity(self, case, velocity):
        with tempfile.TemporaryDirectory(prefix='lwe-inherited-' + case + '-') as directory:
            root = Path(directory)
            scene = write_probe(root, case, candidate=True)
            frame, _ = render_scene(self, root, scene, frames=12)
            color = read_velocity_color(frame)
            self.assertIsNotNone(color, 'Velocity readback particle must be visible')
            for actual, component in zip(color, velocity):
                self.assertAlmostEqual(actual, (.5 + component / 200) * 255, delta=2)

    def test_known_velocity_calibrates_readback(self):
        self.check_velocity('known', (40, -20, 10))

    def test_moving_layer_supplies_world_velocity(self):
        self.check_velocity('moving', (20, 0, 0))

    def test_stationary_point_has_no_velocity(self):
        self.check_velocity('static', (0, 0, 0))

    def test_local_particles_do_not_inherit_layer_motion_twice(self):
        self.check_velocity('local', (0, 0, 0))

    def test_layer_scale_does_not_multiply_inherited_motion(self):
        self.check_velocity('scaled', (20, 0, 0))

    def test_mirrored_layer_preserves_motion_direction(self):
        self.check_velocity('mirrored', (20, 0, 0))

    def test_rotated_layer_preserves_motion_direction(self):
        self.check_velocity('rotated', (20, 0, 0))

    def test_parent_motion_uses_the_full_transform(self):
        self.check_velocity('parent', (20, 0, 0))

    def test_particle_speed_multiplier_applies(self):
        self.check_velocity('speed', (40, 0, 0))

    def test_simulation_rate_does_not_change_measured_velocity(self):
        self.check_velocity('rate', (20, 0, 0))

    def test_control_point_zero_is_supported(self):
        self.check_velocity('zero', (20, 0, 0))

    def test_half_inheritance(self):
        self.check_velocity('half', (10, 0, 0))

    def test_negative_inheritance_reverses_motion(self):
        self.check_velocity('negative', (-10, 0, 0))

    def test_inheritance_adds_to_existing_velocity(self):
        self.check_velocity('add', (30, 0, 0))

    def test_world_point_does_not_move_with_the_layer(self):
        self.check_velocity('worldpoint', (0, 0, 0))

    def test_world_point_moves_backwards_in_local_simulation(self):
        self.check_velocity('localworldpoint', (-20, 0, 0))

    def test_three_axis_motion_retains_its_direction(self):
        self.check_velocity('diagonal', (20, 10, 5))

    def test_scripted_bursts_use_native_between_tick_history(self):
        for case in ('manual', 'lastmove', 'afterstop'):
            with self.subTest(case=case):
                self.check_velocity(case, (0, 0, 0))

    def test_world_control_point_zero_still_follows_the_emitter(self):
        self.check_velocity('worldzero', (20, 0, 0))

    def test_world_control_point_zero_retains_native_birth_scaling(self):
        self.check_velocity('worldzero_scaled', (40, 0, 0))


if __name__ == '__main__':
    unittest.main()
