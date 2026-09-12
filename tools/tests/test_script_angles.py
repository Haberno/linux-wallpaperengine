"""Opt-in angle hook side-effect regression from Sukuna's rotating slices."""
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_script_property_animation import scene_with


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ScriptAngles(unittest.TestCase):
    def test_void_rotation_and_argument_mutation_survive_angle_hooks(self):
        objects = []
        for identifier, action in enumerate((
                'return thisLayer.rotateObjectSpace(new Vec3(0, 0, 10));',
                'value.z += 10;',
                'thisLayer.angles = new Vec3(0, 0, value.z + 10);'), 1):
            script = '''
let previous;
export function update(value) {
    if (previous !== undefined && Math.abs(value.z - previous - 10) > .001)
        throw Error('angle hook lost mutation: '+value.z+' previous '+previous);
    previous = value.z;
    if (engine.runtime >= .3) console.log('ANGLE_MUTATION_OK');
    ACTION
}
'''.replace('ACTION', action)
            objects.append({'id': identifier, 'name': 'angle probe '+str(identifier),
                            'image': 'models/util/solidlayer.json', 'size': '20 20',
                            'origin': '160 90 0', 'angles': {'value': '0 0 0', 'script': script}})
        with tempfile.TemporaryDirectory(prefix='lwe-angle-mutation-') as directory:
            _, output = render_scene(self, Path(directory), scene_with(objects))
        self.assertGreaterEqual(output.count('ANGLE_MUTATION_OK'), 3)


if __name__ == '__main__':
    unittest.main()
