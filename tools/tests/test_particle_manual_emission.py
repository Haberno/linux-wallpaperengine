"""Opt-in scripted bursts while paused/stopped, with normal expiry afterward."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_text_effect_targets import base_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleManualEmission(unittest.TestCase):
    def test_scripted_bursts_bypass_timers_without_resuming_the_system(self):
        with tempfile.TemporaryDirectory(prefix='lwe-manual-burst-') as directory:
            for emitter in ('boxrandom', 'sphererandom'):
                for state, lifetime, count, expected in (
                        ('pause', 2, 20, True), ('stop', 2, 20, True),
                        ('stop', .2, 20, False), ('pause', 2, 0, False)):
                    with self.subTest(emitter=emitter, state=state, lifetime=lifetime, count=count):
                        root = Path(directory) / f'{emitter}-{state}-{lifetime}-{count}'
                        (root / 'particles').mkdir(parents=True)
                        (root / 'materials').mkdir()
                        (root / 'particles/probe.json').write_text(json.dumps({
                            'maxcount': 20, 'material': 'materials/probe.json',
                            'emitter': [{'name': emitter, 'rate': 100, 'delay': 100,
                                         'duration': .01, 'flags': 2,
                                         'distancemin': 0, 'distancemax': 0}],
                            'initializer': [{'name': 'lifetimerandom', 'min': lifetime, 'max': lifetime},
                                            {'name': 'sizerandom', 'min': 20, 'max': 20}]}))
                        (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
                            'shader': 'genericparticle', 'textures': ['util/white'],
                            'blending': 'normal', 'cullmode': 'nocull',
                            'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
                        script = f'''export function init(value) {{
                            thisLayer.{state}(); thisLayer.emitParticles({count}); return value;
                        }}'''
                        frame, output = render_scene(self, root, base_scene([{
                            'id': 1, 'name': 'Manual burst', 'particle': 'particles/probe.json',
                            'origin': {'value': '160 90 0', 'script': script}}]), frames=6)
                        self.assertEqual(frame.getbbox() is not None, expected, output)


if __name__ == '__main__':
    unittest.main()
