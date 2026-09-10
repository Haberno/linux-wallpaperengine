"""Opt-in regressions for authored minified SceneScript modules and shared state."""
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ScriptModuleSyntax(unittest.TestCase):
    def render_scripts(self, scripts):
        with tempfile.TemporaryDirectory(prefix='lwe-script-module-') as directory:
            scene = {
                'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                            'clearcolor': '0 0 0', 'camerafade': False},
                'objects': [{'id': i + 1, 'name': 'module ' + str(i), 'solid': True,
                             'visible': {'value': True, 'script': script}}
                            for i, script in enumerate(scripts)]}
            return render_scene(self, Path(directory), scene)[1]

    def test_namespace_aliases_inside_minified_lines(self):
        output = self.render_scripts([
            "let n=2;import*as math from'WEMath';import * as color from 'WEColor';"
            "export function update(value){"
            "if(math.mix(n,6,.5)!==4)throw Error('math alias');"
            "if(color.normalizeColor(new Vec3(255,0,0)).x!==1)throw Error('color alias');"
            "console.log('COMPACT_IMPORTS_OK');return value;}"
        ])
        self.assertIn('COMPACT_IMPORTS_OK', output)

    def test_multiline_imports_preserve_literals_and_adjacent_code(self):
        output = self.render_scripts([r'''
const literal = "export 'use strict'; import * as wrong from 'WEMath';";
const template = `export 'use strict';`;
const pattern = /export [/*] import/;
// import * as ignored from 'WEColor';
/* export function wrong() {} */
import * as
    math
    from 'WEMath'; const after = 42;
export function update(value) {
    if (!literal.startsWith('export ') || !literal.includes("'use strict';") ||
        template !== "export 'use strict';" || !pattern.test('export / import') ||
        after !== 42 || math.mix(0,10,.5) !== 5) throw Error('source changed');
    console.log('SOURCE_PRESERVED_OK');
    return value;
}
'''])
        self.assertIn('SOURCE_PRESERVED_OK', output)

    def test_shared_can_be_replaced_and_is_visible_to_other_modules(self):
        output = self.render_scripts([
            "shared=[2,4,6]; export function init(){ shared[1]=8; }",
            "export function update(value){"
            "if(!Array.isArray(shared)||shared[1]!==8)throw Error('shared replacement');"
            "console.log('SHARED_REPLACEMENT_OK');return value;}"
        ])
        self.assertIn('SHARED_REPLACEMENT_OK', output)


if __name__ == '__main__':
    unittest.main()
