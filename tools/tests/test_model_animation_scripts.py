"""Opt-in model clip controls, timed events and one-shot retirement through SceneScript."""
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene


def write_model(root, action_mode='loop'):
    """A skinned triangle with a tip attachment, an idle loop and an action clip."""
    data = bytearray()
    def put(fmt, *values):
        data.extend(struct.pack('<' + fmt, *values))
    def string(value):
        data.extend(value.encode() + b'\0')
    def begin(marker):
        string(marker)
        offset = len(data)
        put('I', 0)
        return offset
    def end(offset):
        struct.pack_into('<I', data, offset, len(data))
    identity = [1 if row == column else 0 for column in range(4) for row in range(4)]

    string('MDLV0023')
    put('III', 0x0180000f, 1, 1)
    string('materials/probe.json')
    put('I6fII', 0, -5, -5, 0, 5, 5, 0, 0x0180000f, 3 * 80)
    for x, y in ((-5, -5), (5, -5), (0, 5)):
        put('10f4I6f', x, y, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0)
    put('I3HBBI', 6, 0, 1, 2, 0, 0, 0)
    offset = begin('MDLS0002')
    put('I', 1)
    string('root')
    put('III', 0, 0xffffffff, 64)
    put('16f', *identity)
    string('')
    end(offset)
    offset = begin('MDAT0001')
    put('HH', 1, 0)
    string('tip')
    put('16f', *identity)
    end(offset)
    offset = begin('MDLA0006')
    put('I', 2)
    for clip_id, name, base, events in (
            (1, 'Idle', 0, [(9, 'end')]), (2, 'Action', 100, [(3, 'cry'), (9, 'reset')])):
        put('II', clip_id, 0)
        string(name)
        string(action_mode if name == 'Action' else 'loop')
        put('fIII', 10, 10, 0, 1)
        put('II', 0, 11 * 36)
        for frame in range(11):
            put('9f', base + frame, 0, 0, 0, 0, 0, 1, 1, 1)
        put('IBB', 0, 0, 0)  # blend, scalar and constraint tracks
        put('6f', *([0] * 6))
        put('BI', 0, len(events))
        for frame, event in events:
            put('f', frame / 10)
            string(json.dumps({'frame': frame, 'name': event}))
    end(offset)
    (root / 'models').mkdir(parents=True)
    (root / 'models/probe.mdl').write_bytes(data)
    (root / 'materials').mkdir()
    (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
        'shader': 'generic4', 'textures': ['util/white'], 'blending': 'normal',
        'cullmode': 'nocull', 'depthtest': 'enabled', 'depthwrite': 'enabled'}]}))


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ModelAnimationScripts(unittest.TestCase):
    def test_one_shot_blend_out_respects_the_authored_clip_mode(self):
        for mode in ('', 'loop', 'single'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix='lwe-model-blend-') as directory:
                root = Path(directory)
                write_model(root, action_mode=mode)
                expected = 53.75 if mode == 'single' else 107.5
                script = '''
export function init(value) {
    const idle = thisLayer.getAnimationLayer('Idle');
    idle.pause();
    idle.setFrame(0);
    const action = thisLayer.playSingleAnimation('Action', {blendin: false, blendout: true});
    action.pause();
    action.setFrame(7.5);
    const actual = thisLayer.getAttachmentOrigin('tip').x - 160;
    if (Math.abs(actual - EXPECTED) > .01) throw Error('Late action pose '+actual+' != '+EXPECTED);
    console.log('MODEL_BLEND_MODE_OK');
    return value;
}
'''.replace('EXPECTED', str(expected))
                _, output = render_scene(self, root, base_scene([{
                    'id': 1, 'name': 'model probe', 'model': 'models/probe.mdl', 'origin': '160 90 0',
                    'animationlayers': [{'id': 10, 'name': 'Idle', 'animation': 1,
                                         'blend': {'value': 1, 'script': script}}]}]))
                self.assertIn('MODEL_BLEND_MODE_OK', output)

    def test_property_animations_are_scoped_to_the_selected_model_layer(self):
        from test_script_property_animation import animation

        script = '''
let first, second;
const check = (a,b) => { if (Math.abs(a-b) > .001) throw Error(a+' != '+b); };
export function init(value) {
    first = thisLayer.getAnimationLayer('Idle').getAnimation('fade');
    second = thisLayer.getAnimationLayer('Action').getAnimation('fade');
    if (!first || !second) throw Error('Missing nested property controller');
    if (thisLayer.getAnimationLayer('Idle').getAnimation('missing') !== undefined)
        throw Error('Unknown animation must not select the model clip');
    first.setFrame(2);
    second.setFrame(6);
    check(first.getFrame(), 2);
    check(second.getFrame(), 6);
    first.stop();
    check(second.getFrame(), 6);
    if (!thisLayer.getAnimationLayer('Idle').isPlaying()) throw Error('Blend stop paused the clip');
    first.setFrame(0);
    second.setFrame(0);
    first.play();
    second.play();
    return value;
}
export function animationEvent(event, value) {
    if (event.animation === 'fade') console.log('NESTED_EVENT '+event.name);
    return value;
}
export function update(value) {
    if (engine.runtime > 1.1) {
        check(first.getFrame(), 10);
        check(second.getFrame(), 10);
        console.log('NESTED_CONTROLS_OK');
    }
    return value;
}
'''
        with tempfile.TemporaryDirectory(prefix='lwe-model-nested-') as directory:
            root = Path(directory)
            write_model(root)
            layers = []
            for index, name in enumerate(('Idle', 'Action')):
                layers.append({'id': 10 + index, 'name': name, 'animation': index + 1,
                               'blend': {'value': 0, 'animation': animation(0, .5, name='fade',
                                   startpaused=True, events=[{'frame': 5, 'name': name}])}})
            _, output = render_scene(self, root, base_scene([{
                'id': 1, 'name': 'model probe', 'model': 'models/probe.mdl', 'origin': '160 90 0',
                'visible': {'value': True, 'script': script}, 'animationlayers': layers}]), frames=15)
            self.assertIn('NESTED_CONTROLS_OK', output)
            for name in ('Idle', 'Action'):
                self.assertEqual(output.count('NESTED_EVENT ' + name), 1)

    def test_clip_controls_events_and_return_to_idle(self):
        script = '''
let idle, action, triggered = false, reset = false, checked = false;
function check(value, expected, label) {
    if (Math.abs(value - expected) > .01) throw new Error(label + ': ' + value + ' != ' + expected);
}
export function init(value) {
    idle = thisLayer.getAnimationLayer('Idle');
    check(thisLayer.getAnimationLayerCount(), 1, 'initial layers');
    check(idle.fps, 10, 'fps');
    check(idle.frameCount, 10, 'frames');
    check(idle.duration, 1, 'duration');
    idle.pause();
    idle.setFrame(4);
    check(thisLayer.getAttachmentOrigin('tip').x, 164, 'paused pose');
    thisLayer.getAnimationLayer(0).setFrame(5);
    check(thisLayer.getAttachmentOrigin('tip').x, 165, 'same-frame seek');
    idle.blend = .5;
    check(thisLayer.getAttachmentOrigin('tip').x, 162.5, 'blend');
    idle.visible = false;
    check(thisLayer.getAttachmentOrigin('tip').x, 160, 'visibility');
    idle.visible = true;
    idle.blend = 1;
    idle.rate = .5;
    idle.play();
    console.log('MODEL_INIT_OK');
    return value;
}
export function animationEvent(event, value) {
    console.log('MODEL_EVENT ' + event.animation + ':' + event.name);
    if (event.name == 'end' && !triggered) {
        triggered = true;
        idle.pause();
        action = thisLayer.playSingleAnimation('Action', {blendin: false, blendout: false});
        action.pause();
        action.setFrame(4);
        check(thisLayer.getAttachmentOrigin('tip').x, 264, 'action pose');
        action.setFrame(0);
        action.play();
        check(thisLayer.getAnimationLayerCount(), 2, 'action layers');
        console.log('MODEL_ACTION_OK');
    }
    if (event.name == 'reset') {
        reset = true;
        idle.setFrame(0);
        idle.play();
    }
    return value;
}
export function update(value) {
    if (reset && !checked && thisLayer.getAnimationLayerCount() == 1) {
        checked = true;
        if (!idle.isPlaying()) throw new Error('Idle did not resume');
        if (action.isPlaying() !== undefined) throw new Error('Expired controller still resolves');
        if (thisLayer.getAttachmentOrigin('tip').x >= 180) throw new Error('Action pose survived retirement');
        console.log('MODEL_RESUMED_OK');
    }
    return value;
}
'''
        with tempfile.TemporaryDirectory(prefix='lwe-model-scripts-') as directory:
            root = Path(directory)
            write_model(root)
            _, output = render_scene(self, root, base_scene([{
                'id': 1, 'name': 'model probe', 'model': 'models/probe.mdl', 'origin': '160 90 0',
                'animationlayers': [{'id': 10, 'name': 'Idle', 'animation': 1,
                                     'visible': True, 'blend': {'value': 1, 'script': script}}]}]), frames=35)
            for marker in ('MODEL_INIT_OK', 'MODEL_ACTION_OK', 'MODEL_RESUMED_OK',
                           'MODEL_EVENT Idle:end', 'MODEL_EVENT Action:cry', 'MODEL_EVENT Action:reset'):
                self.assertIn(marker, output)
            self.assertEqual(output.count('MODEL_EVENT Action:cry'), 1)
            self.assertEqual(output.count('MODEL_EVENT Action:reset'), 1)


if __name__ == '__main__':
    unittest.main()
