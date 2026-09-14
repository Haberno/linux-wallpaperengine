"""Position-offset initializer readbacks from controlled, non-random births."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_inherited_velocity import write_probe, read_velocity_color
from test_text_effect_targets import write_copy_assets


def write_offset_probe(root, case, candidate=False):
    scene = write_probe(root, 'static', candidate)
    layer = scene['objects'][0]
    layer['origin'] = '160 90 0'
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['flags'] = 0
    emitter = definition['emitter'][0]
    emitter.update(origin='20 30 40', rate=0, delay=0, instantaneous=1)
    offset = {'name': 'positionoffsetrandom', 'timescale': 0,
              'directions': '1 1 1', 'scale': .01, 'distance': 64, 'octaves': 1}
    definition['initializer'][-1] = offset
    if case == 'baseline':
        definition['initializer'].pop()
    elif case == 'zero_distance':
        offset['distance'] = 0
    elif case == 'origin_zero':
        emitter['origin'] = '0 0 0'
    elif case == 'negative_position':
        emitter['origin'] = '-20 -30 -40'
    elif case == 'octaves_six':
        offset['octaves'] = 6
    elif case == 'positive_sign':
        offset['sign'] = '1 1 1'
    elif case == 'negative_sign':
        offset['sign'] = '-1 -1 -1'
    elif case == 'fractional_sign':
        offset['sign'] = '-0.5 0.5 0.25'
    elif case == 'directions':
        offset['directions'] = '0 -0.5 -0.75'
    elif case == 'defaults':
        offset.clear()
        offset.update(name='positionoffsetrandom', timescale=0)
    elif case == 'clamp_low':
        offset['octaves'] = 0
    elif case == 'clamp_high':
        offset['octaves'] = 100
    elif case == 'clamp_uint':
        offset['octaves'] = 2147483648
    elif case == 'octaves_eight':
        offset['octaves'] = 8
    elif case == 'negative_scale':
        offset['scale'] = -.01
    elif case == 'negative_distance':
        offset['distance'] = -64
    elif case == 'after_mapping':
        definition['controlpoint'] = [{'id': i, 'offset': '20 30 40'} for i in range(8)]
        definition['initializer'].insert(-1, {'name': 'mapsequencebetweencontrolpoints',
            'controlpointstart': 0, 'controlpointend': 1, 'flags': 4})
    elif case == 'local_scaled':
        layer['scale'] = '2 3 1'
    elif case == 'local_mirrored':
        layer['scale'] = '-2 3 -1'
    elif case in ('world_scaled', 'world_baseline', 'world_manual', 'world_double'):
        definition['flags'] = 1
        layer.update(origin='5 10 4', scale='2 3 1')
        emitter['origin'] = '0 0 0'
        offset.update(distance=16, scale=.1)
        if case == 'world_baseline':
            offset['distance'] = 0
        if case == 'world_manual':
            emitter.update(instantaneous=0, rate=0)
            layer['visible'] = {'value': True, 'script': '''let ticks=0;
export function update(v){if(++ticks==4){thisLayer.origin=new Vec3(30,20,4);thisLayer.emitParticles(1);}return v;}'''}
        if case == 'world_double':
            definition['initializer'].append(dict(offset))
    if case.startswith('perspective_'):
        scene['general'].update(orthogonalprojection=None, fov=50, nearz=.1, farz=1000)
        scene['camera'] = {'eye': '0 0 10', 'center': '0 0 0', 'up': '0 1 0'}
        layer['origin'] = '0 0 0'
        emitter['origin'] = '.2 .3 .4'
        offset.clear()
        offset.update(name='positionoffsetrandom', timescale=0)
        if case == 'perspective_explicit':
            offset.update(directions='1 1 1', scale=1, distance=.1, octaves=6, sign='0 0 0')
    if case.startswith('time_'):
        emitter.update(instantaneous=0, rate=0, origin='0 0 0')
        offset.update(timescale=.125, scale=0)
        layer['visible'] = {'value': True, 'script': '''let ticks=0;
export function update(v){if(++ticks==4){shared.offsetBirth=engine.runtime;thisLayer.emitParticles(1);}return v;}'''}
        if case == 'time_rate':
            layer['instanceoverride']['rate'] = 2
        write_copy_assets(root)
        (root / 'shaders/marker.vert').write_text((root / 'shaders/copy.vert').read_text())
        (root / 'shaders/marker.frag').write_text('uniform vec3 g_Color;\nvoid main(){gl_FragColor=vec4(g_Color,1.0);}')
        (root / 'materials/marker.json').write_text(json.dumps({'passes': [{'shader': 'marker',
            'blending': 'normal', 'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
        (root / 'models/marker.json').write_text(json.dumps({'material': 'materials/marker.json'}))
        scene['objects'].append({'id': 2, 'name': 'Birth clock', 'origin': '300 170 0',
            'size': '10 10', 'image': 'models/marker.json', 'color': {'value': '0 0 0', 'script': '''
export function update(v){let t=Math.round((shared.offsetBirth||0)*1000);
return new Vec3(Math.floor(t/65536)/255,Math.floor(t/256)%256/255,t%256/255);}'''}})
    path.write_text(json.dumps(definition))
    shader = root / 'shaders/genericparticle.vert'
    text = shader.read_text()
    old = 'a_TexCoordVec4C1.xyz*vec3(1.0,' + ('-1.0' if candidate else '1.0') + ',1.0)/200.0'
    text = text.replace(old, 'a_Position.xyz*vec3(1.0,' + ('-1.0' if candidate else '1.0') + ',1.0)/256.0')
    if case.startswith('perspective_'):
        text = text.replace('/256.0', '/1.0')
    # Read the center attribute, while keeping diagnostic geometry on screen.
    text = text.replace('gl_Position=vec4(a_Position,60.0);', 'gl_Position=vec4(0.0,0.0,0.0,60.0);')
    text = text.replace('vec3 position=a_Position+', 'vec3 position=')
    shader.write_text(text)
    (root / 'scene.json').write_text(json.dumps(scene))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticlePositionOffset(unittest.TestCase):
    def check_color(self, case, expected):
        with tempfile.TemporaryDirectory(prefix='lwe-position-offset-' + case + '-') as directory:
            root = Path(directory)
            scene = write_offset_probe(root, case, candidate=True)
            frame, _ = render_scene(self, root, scene, frames=5)
            actual = read_velocity_color(frame)
            self.assertIsNotNone(actual, 'Position readback particle must be visible')
            for value, component in zip(actual, expected):
                self.assertAlmostEqual(value, component, delta=1)

    def test_zero_distance_keeps_the_birth_position(self):
        self.check_color('baseline', (147, 157, 167))
        self.check_color('zero_distance', (147, 157, 167))
        self.check_color('origin_zero', (127, 127, 127))

    def test_fractal_noise_offsets_each_authored_axis(self):
        self.check_color('spatial', (200, 133, 222))

    def test_negative_positions_sample_the_negative_noise_domain(self):
        self.check_color('negative_position', (55, 122, 32))

    def test_octaves_blend_normalized_noise_layers(self):
        self.check_color('octaves_six', (192, 139, 202))

    def test_positive_sign_reflects_negative_offsets(self):
        self.check_color('positive_sign', (200, 182, 222))

    def test_negative_sign_reflects_positive_offsets(self):
        self.check_color('negative_sign', (95, 133, 113))

    def test_orthographic_defaults_use_pixel_distances_and_xy_directions(self):
        self.check_color('defaults', (173, 140, 167))

    def test_fractional_sign_blends_the_reflected_offset(self):
        self.check_color('fractional_sign', (147, 157, 222))

    def test_directions_scale_and_invert_axes_before_applying_sign(self):
        self.check_color('directions', (147, 170, 127))

    def test_octaves_are_bounded_to_one_through_eight(self):
        for case, expected in [('clamp_low', (200, 133, 222)),
                               ('clamp_high', (191, 139, 202)),
                               ('octaves_eight', (191, 139, 202)),
                               ('clamp_uint', (191, 139, 202))]:
            with self.subTest(case=case):
                self.check_color(case, expected)

    def test_negative_scale_reverses_the_sample_domain(self):
        self.check_color('negative_scale', (95, 182, 112))

    def test_negative_distance_reverses_the_offset(self):
        self.check_color('negative_distance', (95, 182, 113))

    def test_noise_uses_the_position_from_earlier_initializers(self):
        self.check_color('after_mapping', (240, 158, 205))

    def test_perspective_defaults_use_world_distances_and_three_directions(self):
        self.check_color('perspective_defaults', (196, 196, 243))
        self.check_color('perspective_explicit', (196, 196, 243))

    def test_local_noise_is_independent_of_layer_scale_and_mirroring(self):
        self.check_color('local_scaled', (200, 133, 222))
        self.check_color('local_mirrored', (200, 133, 222))

    def test_world_noise_uses_world_positions_and_distances(self):
        self.check_color('world_baseline', (132, 137, 131))
        self.check_color('world_scaled', (146, 125, 145))
        self.check_color('world_manual', (146, 125, 145))
        self.check_color('world_double', (146, 132, 145))

    def test_manual_births_sample_the_scene_clock_independently_of_simulation_rate(self):
        for case in ('time_manual', 'time_rate'):
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='lwe-position-offset-' + case + '-') as directory:
                root = Path(directory)
                scene = write_offset_probe(root, case, candidate=True)
                frame, _ = render_scene(self, root, scene, frames=6)
                r, g, b = frame.getpixel((300, 10))
                phase = (r * 65536 + g * 256 + b) / 1000 * .125
                self.assertGreater(phase, 0, 'Birth clock marker must be populated')
                self.assertLess(phase, .1, 'This probe must sample inside the central simplex triangle')
                # In this triangle only the origin corner contributes; its
                # three native axis gradients are -t, 2t and t respectively.
                amplitude = 45.23065 * phase * (.5 - phase * phase) ** 4 * 64
                color = read_velocity_color(frame)
                self.assertIsNotNone(color)
                for actual, position in zip(color, (-amplitude, 2 * amplitude, amplitude)):
                    self.assertAlmostEqual(actual, (.5 + position / 256) * 255, delta=1)
