"""Effect timelines drive live uniforms, playback controls, and layer animation events."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene, write_copy_assets
from test_script_property_animation import animation


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class EffectAnimation(unittest.TestCase):
    def test_effect_timeline_can_trigger_events_and_resume_after_pause(self):
        script = '''
let resumed = false;
export function init(value) {
    const effect = thisLayer.getEffect('Test effect');
    if (!effect || thisLayer.getEffect(-1) !== undefined || effect.getMaterial(10) !== undefined)
        throw Error('Effect/material lookup bounds');
    const fade = effect.getMaterial(0).getAnimation('fade');
    if (!fade || fade.isPlaying()) throw Error('Effect startpaused ignored');
    fade.play();
    return value;
}
export function animationEvent(event, value) {
    if (event.animation === 'fade' && event.name === 'change_scene') {
        thisLayer.getEffect(0).getMaterial(0).getAnimation('fade').pause();
        console.log('EFFECT_EVENT_OK');
    }
    return value;
}
export function update(value) {
    if (!resumed && engine.runtime > .8) {
        resumed = true;
        thisLayer.getAnimation('fade').play();
    }
    return value;
}
'''
        with tempfile.TemporaryDirectory(prefix='lwe-effect-animation-') as directory:
            for kind in ('image', 'text'):
                with self.subTest(kind=kind):
                    root = Path(directory) / kind
                    write_copy_assets(root)
                    (root / 'models/probe.json').write_text(json.dumps({'material': 'materials/copy.json'}))
                    (root / 'shaders/probe.vert').write_text((root / 'shaders/copy.vert').read_text())
                    (root / 'shaders/probe.frag').write_text('''
uniform float uAmount; // {"material":"amount","default":0}
uniform vec3 uColor; // {"material":"tint","default":"1 0 0"}
void main() { gl_FragColor = vec4(uColor * uAmount, 1.0); }
''')
                    (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
                        'shader': 'probe', 'blending': 'normal', 'depthtest': 'disabled',
                        'depthwrite': 'disabled', 'cullmode': 'nocull',
                        'constantshadervalues': {'amount': {'value': .2, 'animation': animation(0, .2)}}}]}))
                    (root / 'effects/probe.json').write_text(json.dumps({
                        'passes': [{'material': 'materials/probe.json'}]}))
                    layer = {'id': 1, 'name': 'Fade event', 'origin': '160 90 0', 'effects': [{
                        'id': 2, 'name': 'Test effect', 'file': 'effects/probe.json', 'passes': [{
                            'constantshadervalues': {
                                'amount': {'value': 0, 'script': script, 'animation': animation(0, 1,
                                    name='fade', startpaused=True, events=[{'frame': 5, 'name': 'change_scene'}])},
                                'tint': {'value': '0 .2 0', 'animation': {
                                    **animation(0, .4, channel=1), 'relative': True}}}}]}]}
                    if kind == 'image':
                        layer.update(image='models/probe.json', size='100 100')
                    else:
                        layer.update(text='Timeline', pointsize=40)
                    scene = base_scene([layer])
                    scene['general']['camerafade'] = False
                    frame, output = render_scene(self, root, scene, frames=20)
                    self.assertEqual(output.count('EFFECT_EVENT_OK'), 1)
                    self.assertAlmostEqual(frame.getpixel((160, 90))[1], 153, delta=2)
                    self.assertEqual(frame.getpixel((160, 90))[0], 0)


if __name__ == '__main__':
    unittest.main()
