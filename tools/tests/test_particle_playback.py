"""Opt-in authored bubble-burst timeline regression."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticlePlayback(unittest.TestCase):
    def test_timeline_controls_emission_without_freezing_released_particles(self):
        with tempfile.TemporaryDirectory(prefix='lwe-particle-playback-') as directory:
            for name, lifetime, play, expected in (
                    ('starts-paused', 2, False, False),
                    ('released-bubbles-survive-pause', 2, True, True),
                    ('burst-ends-and-bubbles-expire', .25, True, False)):
                with self.subTest(name=name):
                    root = Path(directory) / name
                    (root / 'particles').mkdir(parents=True)
                    (root / 'materials').mkdir()
                    (root / 'particles/probe.json').write_text(json.dumps({
                        'maxcount': 100, 'material': 'materials/probe.json',
                        'emitter': [{'name': 'sphererandom', 'rate': 100,
                                     'distancemin': 0, 'distancemax': 0}],
                        'initializer': [{'name': 'lifetimerandom', 'min': lifetime, 'max': lifetime},
                                        {'name': 'sizerandom', 'min': 20, 'max': 20},
                                        {'name': 'velocityrandom', 'min': '0 0 0', 'max': '0 0 0'}],
                        'renderer': [{'name': 'sprite'}]}))
                    (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
                        'shader': 'genericparticle', 'textures': ['util/white'],
                        'blending': 'normal', 'cullmode': 'nocull',
                        'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
                    script = '''
                        export function init() {
                            thisLayer.pause();
                            const animation = thisLayer.getAnimation('bubbles');
                            if (animation.isPlaying()) throw Error('must start paused');
                            animation.rate = 1;
                            animation.setFrame(0);
                            PLAY
                        }
                        export function animationEvent(event, value) {
                            console.log('BUBBLE_EVENT ' + event.name);
                            if (event.name === 'on') thisLayer.play();
                            if (event.name === 'off') thisLayer.pause();
                            return value;
                        }
                    '''.replace('PLAY', 'animation.play();' if play else '')
                    frame, log = render_scene(self, root, {
                        'camera': {'eye': '0 0 100', 'center': '0 0 0', 'up': '0 1 0'},
                        'general': {'orthogonalprojection': None, 'fov': 50,
                                    'clearcolor': '0 0 0', 'bloom': False},
                        'objects': [{'id': 1, 'name': 'Burst', 'particle': 'particles/probe.json', 'scale': {
                            'value': '1 1 1', 'script': script, 'animation': {
                                'relative': True, 'c0': [{'frame': 0, 'value': 0}],
                                'options': {'name': 'bubbles', 'fps': 10, 'length': 10,
                                            'mode': 'single', 'startpaused': True,
                                            'events': [{'frame': 0, 'name': 'on'},
                                                       {'frame': 2, 'name': 'off'}]}}}}]}, frames=10)
                    self.assertEqual(frame.getbbox() is not None, expected, log)
                    self.assertEqual(log.count('BUBBLE_EVENT on'), int(play), log)
                    self.assertEqual(log.count('BUBBLE_EVENT off'), int(play), log)


if __name__ == '__main__':
    unittest.main()
