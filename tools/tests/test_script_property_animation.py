"""Opt-in animated property reads and camera-follow entrance regressions."""
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


def animation(start, end, channel=0, **options):
    return {f'c{channel}': [{'frame': 0, 'value': start}, {'frame': 10, 'value': end}],
            'options': {'fps': 10, 'length': 10, 'mode': 'single', **options}}


def scene_with(objects):
    return {'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'camerafade': False}, 'objects': objects}


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ScriptPropertyAnimation(unittest.TestCase):
    def test_short_fan_rotation_uses_whole_frame_curve_samples(self):
        # Bunk 2134765860: authored easing between two frames becomes uniform
        # rotation after the native whole-frame samples are interpolated.
        script = '''
export function update(value) {
    const expected = (engine.runtime * 720) % 360;
    if (Math.abs(thisLayer.angles.z - expected) > .01)
        throw Error('fan angle ' + thisLayer.angles.z + ' != ' + expected);
    if (engine.runtime >= 1) console.log('FAN_ROTATION_OK');
    return value;
}
'''
        scene = scene_with([{
            'id': 238, 'name': 'Fan auto', 'image': 'models/util/solidlayer.json',
            'origin': '160 90 0', 'size': '40 10',
            'angles': {'value': '0 0 0', 'animation': {
                'c2': [{'frame': 0, 'value': 0,
                        'front': {'enabled': True, 'x': 1, 'y': 0}},
                       {'frame': 2, 'value': 6.2831855,
                        'back': {'enabled': True, 'x': -1, 'y': 0}}],
                'options': {'fps': 4, 'length': 2, 'mode': 'loop'}, 'relative': True}},
            'visible': {'value': True, 'script': script}}])
        with tempfile.TemporaryDirectory(prefix='lwe-fan-rotation-') as directory:
            _, output = render_scene(self, Path(directory), scene, frames=15)
        self.assertIn('FAN_ROTATION_OK', output)

    def test_saved_layer_vectors_do_not_follow_subsequent_assignments(self):
        script = '''
let origin, scale, depth;
const check = (a,b) => { if (Math.abs(a-b) > .001) throw Error(a+' != '+b); };
export function init() {
    origin = thisLayer.origin;
    scale = thisLayer.scale;
    depth = thisLayer.parallaxDepth;
}
export function update(value) {
    thisLayer.origin = origin.add(new Vec3(10, 0, 0));
    thisLayer.scale = scale.multiply(2);
    thisLayer.parallaxDepth = depth.add(new Vec2(.1, .2));
    check(origin.x, 160);
    check(scale.x, 1);
    check(depth.x, .5);
    check(thisLayer.origin.x, 170);
    check(thisLayer.scale.x, 2);
    check(thisLayer.parallaxDepth.x, .6);
    let edited = thisLayer.origin;
    edited.x = 180;
    check(thisLayer.origin.x, 170);
    thisLayer.origin = edited;
    check(thisLayer.origin.x, 180);
    if (engine.runtime >= 1) console.log('SAVED_VECTORS_OK');
    return value;
}
'''
        scene = scene_with([{
            'id': 1, 'name': 'offset driver', 'image': 'models/util/solidlayer.json',
            'origin': '160 90 0', 'scale': '1 1 1', 'size': '20 20', 'parallaxDepth': '.5 .5',
            'visible': {'value': True, 'script': script}}])
        with tempfile.TemporaryDirectory(prefix='lwe-saved-vectors-') as directory:
            _, output = render_scene(self, Path(directory), scene, frames=15)
        self.assertIn('SAVED_VECTORS_OK', output)

    def test_camera_round_trip_does_not_apply_the_editor_viewport_offset(self):
        frames = []
        for script in ('', 'export function update(v) { thisScene.setCameraTransforms(thisScene.getCameraTransforms()); return v; }'):
            scene = scene_with([{
                'id': 1, 'name': 'center marker', 'image': 'models/util/solidlayer.json',
                'origin': '160 90 0', 'size': '40 40', 'color': '1 0 0',
                'visible': {'value': True, 'script': script}}])
            scene['camera'] = {'eye': '49 -716 0', 'center': '49 -716 -1', 'up': '0 1 0'}
            with tempfile.TemporaryDirectory(prefix='lwe-camera-roundtrip-') as directory:
                frame, _ = render_scene(self, Path(directory), scene)
                frames.append(frame)
        self.assertEqual(frames[0].tobytes(), frames[1].tobytes())
        self.assertEqual(frames[1].getpixel((160, 90)), (255, 0, 0))

    def test_layer_reads_sample_timelines_without_changing_relative_bases(self):
        script = '''
let initial;
export function init() {
    initial = thisLayer.origin;
    if (!thisLayer.getAnimation('fade')) throw Error('missing alpha timeline');
}
export function update(value) {
    const t = Math.min(1, engine.runtime);
    const check = (a,b) => { if (Math.abs(a-b) > .001) throw Error(a+' != '+b); };
    check(thisLayer.origin.x, 160 + 100*t);
    check(thisLayer.origin.y, 90);
    check(initial.x, 160);
    check(thisLayer.angles.z, 90*t);
    check(thisLayer.alpha, 1 - .7*t);
    if (t === 1) console.log('ANIMATED_READS_OK');
    return value;
}
'''
        scene = scene_with([{
            'id': 1, 'name': 'animated driver', 'image': 'models/util/solidlayer.json', 'size': '20 20',
            'origin': {'value': '160 90 0', 'animation': {
                **animation(0, 100), 'relative': True}},
            'angles': {'value': '0 0 0', 'animation': animation(0, 1.57079632679, 2)},
            'alpha': {'value': 1, 'animation': animation(1, .3, name='fade')},
            'visible': {'value': True, 'script': script}}])
        with tempfile.TemporaryDirectory(prefix='lwe-animated-reads-') as directory:
            _, output = render_scene(self, Path(directory), scene, frames=15)
        self.assertIn('ANIMATED_READS_OK', output)

    def test_camera_follow_keeps_animated_zoom_and_explicit_zoom_controls(self):
        for animate_zoom in (True, False):
            with self.subTest(animate_zoom=animate_zoom):
                script = '''
export function update(value) {
    let cam = thisScene.getCameraTransforms();
    const expected = ANIMATED ? 2 - Math.min(1,engine.runtime) : 2;
    if (Math.abs(cam.zoom-expected) > .001) throw Error('pinned zoom '+cam.zoom+' != '+expected);
    cam.center = thisLayer.origin;
    cam.eye = thisLayer.origin.add(new Vec3(0,0,500));
    thisScene.setCameraTransforms(cam);
    if (engine.runtime >= 1) console.log('CAMERA_FOLLOW_OK');
    return value;
}
'''.replace('ANIMATED', str(animate_zoom).lower())
                scene = scene_with([{
                    'id': 1, 'name': 'animated driver', 'image': 'models/util/solidlayer.json', 'size': '20 20',
                    'origin': {'value': '160 90 0', 'animation': {
                        **animation(0, 100), 'relative': True}},
                    'visible': {'value': True, 'script': script}}])
                scene['general']['zoom'] = {'value': 2, 'animation': animation(2, 1)} if animate_zoom else 2
                with tempfile.TemporaryDirectory(prefix='lwe-camera-follow-') as directory:
                    _, output = render_scene(self, Path(directory), scene, frames=15)
                self.assertIn('CAMERA_FOLLOW_OK', output)


if __name__ == '__main__':
    unittest.main()
