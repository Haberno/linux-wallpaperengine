"""Opt-in animated bone lookup/transform regressions for Arona's parenting script."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_model_animation_scripts import write_model
from test_text_effect_targets import base_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class BoneTransforms(unittest.TestCase):
    def check_bones(self, image):
        script = '''
let checked = false;
const check = (a,b) => { if (Math.abs(a-b) > .01) throw Error(a+' != '+b); };
export function init(value) {
    check(thisLayer.getBoneIndex('root'), 0);
    check(thisLayer.getBoneIndex('missing'), -1);
    check(thisLayer.getBoneIndex(''), -1);
    const byName = thisLayer.getBoneTransform('root').translation();
    const byIndex = thisLayer.getBoneTransform(0).translation();
    check(byName.x, byIndex.x);
    check(byName.y, byIndex.y);
    check(byName.y, 90);
    return value;
}
export function update(value) {
    if (!checked && engine.runtime >= .6) {
        const position = thisLayer.getBoneTransform('root').translation();
        if (position.x <= 166 || position.x >= 177)
            throw Error('Bone did not include layer origin, scale and animated pose: '+position.x);
        check(position.y, 90);
        checked = true;
        console.log('ANIMATED_BONE_WORLD_OK');
    }
    return value;
}
'''
        with tempfile.TemporaryDirectory(prefix='lwe-bone-world-') as directory:
            root = Path(directory)
            write_model(root)
            layer = {'id': 1, 'name': 'bone probe', 'origin': '160 90 0', 'scale': '2 2 2',
                     'visible': {'value': True, 'script': script}}
            if image:
                (root / 'models/image.json').write_text(json.dumps({
                    'material': 'materials/probe.json', 'puppet': 'models/probe.mdl',
                    'width': 10, 'height': 10}))
                layer.update(image='models/image.json', size='10 10')
            else:
                layer['model'] = 'models/probe.mdl'
            _, output = render_scene(self, root, base_scene([layer]), frames=10)
        self.assertIn('ANIMATED_BONE_WORLD_OK', output)

    def test_model_bone_transform_includes_layer_transform_and_animation(self):
        self.check_bones(False)

    def test_image_bone_transform_includes_layer_transform_and_animation(self):
        self.check_bones(True)


if __name__ == '__main__':
    unittest.main()
