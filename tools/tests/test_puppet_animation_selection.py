"""Puppets must only apply clips assigned to scene animation layers."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_model_animation_scripts import write_model
from test_text_effect_targets import base_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class PuppetAnimationSelection(unittest.TestCase):
    def test_unassigned_embedded_clips_preserve_the_puppet_bind_pose(self):
        # Seven Suns embeds a discarded clip that squeezes the sun, but neither
        # sun layer assigns it. Native leaves missing/empty layer lists at rest.
        for mode in ('missing', 'empty', 'assigned'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(prefix='lwe-puppet-selection-') as directory:
                root = Path(directory)
                write_model(root)
                (root / 'models/image.json').write_text(json.dumps({
                    'material': 'materials/probe.json', 'puppet': 'models/probe.mdl',
                    'width': 10, 'height': 10}))
                expected = 360 if mode == 'assigned' else 160
                script = '''
let checked = false;
export function update(value) {
    if (!checked && engine.runtime >= .6) {
        const x = thisLayer.getBoneTransform('root').translation().x;
        if (Math.abs(x - EXPECTED) > .01) throw Error('Unexpected puppet bone position: ' + x);
        if (thisLayer.isPlaying() !== PLAYING) throw Error('Unexpected puppet playback state');
        checked = true;
        console.log('PUPPET_SELECTION_OK');
    }
    return value;
}
'''.replace('EXPECTED', str(expected)).replace('PLAYING', str(mode == 'assigned').lower())
                layer = {'id': 1, 'name': mode, 'image': 'models/image.json',
                         'size': '10 10', 'origin': '160 90 0', 'scale': '2 2 2',
                         'visible': {'value': True, 'script': script}}
                if mode == 'empty':
                    layer['animationlayers'] = []
                elif mode == 'assigned':
                    layer['animationlayers'] = [{'id': 10, 'animation': 2, 'rate': 0,
                                                 'blend': 1, 'visible': True}]
                _, output = render_scene(self, root, base_scene([layer]), frames=10)
                self.assertIn('PUPPET_SELECTION_OK', output)


if __name__ == '__main__':
    unittest.main()
