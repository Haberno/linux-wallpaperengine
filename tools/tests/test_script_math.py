"""Opt-in SceneScript math regression through the actual QuickJS adapters.

LWE_TEST_BINARY=/path/to/engine python3 -m unittest discover -s tools/tests -p test_script_math.py
Requires a graphics session. Expected values follow the installed SceneScript
baseclasses.js; the proprietary reference assets are not needed to run this test.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for render integration tests')
class SceneScriptMath(unittest.TestCase):
    def check_expressions(self, cases):
        with tempfile.TemporaryDirectory(prefix='lwe-script-math-') as directory:
            root = Path(directory)
            expressions = {name: expression for name, (expression, _) in cases.items()}
            script = '''
export function init(value) {
    const results = {};
    const expressions = EXPRESSIONS;
    for (const name of Object.keys(expressions)) {
        try { results[name] = eval(expressions[name]); }
        catch (error) { results[name] = {error: String(error)}; }
    }
    console.log('SCENESCRIPT_MATH_RESULT ' + JSON.stringify(results));
    return value;
}
'''.replace('EXPRESSIONS', json.dumps(expressions))
            (root / 'project.json').write_text(json.dumps({
                'type': 'scene', 'file': 'scene.json', 'title': 'SceneScript math regression'}))
            (root / 'scene.json').write_text(json.dumps({
                'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                            'clearcolor': '0 0 0'},
                'objects': [{'id': 1, 'name': 'Math probe', 'solid': True,
                             'visible': {'value': True, 'script': script}}]}))
            log_path = root / 'engine.log'
            env = dict(os.environ, WPE_CONTROL_SOCKET=str(root / 'control.sock'),
                       WPE_LOG_FILE='off', WPE_HEALTH_REPORT=str(root / 'health.json'))
            with log_path.open('w') as log:
                process = subprocess.Popen([
                    str(Path(os.environ['LWE_TEST_BINARY']).resolve()),
                    '--window', '0x0x320x180', '--silent', '--fps', '10',
                    '--no-full-screen-pause', str(root)], env=env,
                    stdout=log, stderr=subprocess.STDOUT)
                marker = 'SCENESCRIPT_MATH_RESULT '
                try:
                    deadline = time.monotonic() + 15
                    while time.monotonic() < deadline and process.poll() is None:
                        if marker in log_path.read_text():
                            break
                        time.sleep(.05)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            text = log_path.read_text()
            self.assertEqual(process.returncode, 0, text[-6000:])
            payloads = [line.split(marker, 1)[1] for line in text.splitlines() if marker in line]
            self.assertEqual(len(payloads), 1, text[-6000:])
            results = json.loads(payloads[0])
            for name, (_, expected) in cases.items():
                with self.subTest(name=name):
                    self.assertEqual(results[name], expected)

    def test_vector_overloads_and_helpers(self):
        self.check_expressions({
            'vec2_scalar': ('Object.values({x:new Vec2(2).x,y:new Vec2(2).y})', [2, 2]),
            'vec3_partial': ('[new Vec3(2,3).x,new Vec3(2,3).y,new Vec3(2,3).z]', [2, 3, 0]),
            'vec4_partial': ('[new Vec4(2,3,4).x,new Vec4(2,3,4).y,new Vec4(2,3,4).z,new Vec4(2,3,4).w]', [2, 3, 4, 4]),
            'vec4_two': ('[new Vec4(2,3).x,new Vec4(2,3).y,new Vec4(2,3).z,new Vec4(2,3).w]', [2, 3, 0, 0]),
            'vec4_missing_y': ('[new Vec4(2,undefined,4).x,new Vec4(2,undefined,4).y,new Vec4(2,undefined,4).z,new Vec4(2,undefined,4).w]', [2, 2, 4, 4]),
            'undefined_first': ('[new Vec3(undefined,3,4).x,new Vec3(undefined,3,4).y,new Vec3(undefined,3,4).z]', [0, 0, 0]),
            'vec2_string': ('[new Vec2("2 3").x,new Vec2("2 3").y]', [2, 3]),
            'vec3_string': ('[new Vec3("2 3 4").x,new Vec3("2 3 4").y,new Vec3("2 3 4").z]', [2, 3, 4]),
            'vec4_string': ('[new Vec4("2 3 4 5").x,new Vec4("2 3 4 5").y,new Vec4("2 3 4 5").z,new Vec4("2 3 4 5").w]', [2, 3, 4, 5]),
            'string_parse_float': ('[new Vec3("2px 0x10 4e1").x,new Vec3("2px 0x10 4e1").y,new Vec3("2px 0x10 4e1").z]', [2, 0, 40]),
            'string_empty_component': ('Number.isNaN(new Vec3("2  4").y)', True),
            'string_missing_component': ('Number.isNaN(new Vec3("2 3").z)', True),
            'vec2_from_vec3': ('[new Vec2(new Vec3(2,3,4)).x,new Vec2(new Vec3(2,3,4)).y]', [2, 3]),
            'vec3_from_vec2': ('[new Vec3(new Vec2(2,3)).x,new Vec3(new Vec2(2,3)).y,new Vec3(new Vec2(2,3)).z]', [2, 3, 0]),
            'vec4_from_vec3': ('[new Vec4(new Vec3(2,3,4)).x,new Vec4(new Vec3(2,3,4)).y,new Vec4(new Vec3(2,3,4)).z,new Vec4(new Vec3(2,3,4)).w]', [2, 3, 4, 0]),
            'equals_vec2_epsilon': ('new Vec2(1).equals(new Vec2(1.000001))', True),
            'equals_vec3_epsilon': ('new Vec3(1).equals(new Vec3(1.000001))', True),
            'equals_vec4_epsilon': ('new Vec4(1).equals(new Vec4(1.000001))', True),
            'equals_difference': ('new Vec3(1).equals(new Vec3(1.0001))', False),
            'equals_scalar': ('new Vec3(1).equals(1)', False),
            'equals_wrong_vector': ('new Vec3(1).equals(new Vec2(1))', False),
            'equals_plain_object': ('new Vec3(1).equals({x:1,y:1,z:1})', False),
            'equals_nan': ('new Vec3(NaN).equals(new Vec3(NaN))', False),
            'equals_infinity': ('new Vec3(Infinity).equals(new Vec3(Infinity))', False),
            'perpendicular': ('[new Vec2(2,3).perpendicular().x,new Vec2(2,3).perpendicular().y]', [3, -2]),
            'subtract_control': ('[new Vec3(5,6,7).subtract(new Vec3(1,2,3)).x,new Vec3(5,6,7).subtract(new Vec3(1,2,3)).y,new Vec3(5,6,7).subtract(new Vec3(1,2,3)).z]', [4, 4, 4]),
        })

    def test_matrix_construction_and_translation(self):
        identity3 = [1, 0, 0, 0, 1, 0, 0, 0, 1]
        identity4 = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        translated3 = [1, 0, 0, 0, 1, 0, 2, 3, 1]
        translated4 = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 3, 4, 1]
        self.check_expressions({
            'mat3_copy': ('new Mat3(Mat3.fromTranslation(new Vec2(2,3))).m', translated3),
            'mat4_copy': ('new Mat4(Mat4.fromTranslation(new Vec3(2,3,4))).m', translated4),
            'mat3_copy_independent': ('(()=>{const a=Mat3.fromTranslation(new Vec2(2,3));const b=new Mat3(a);a.m[6]=99;return b.m;})()', translated3),
            'mat4_copy_independent': ('(()=>{const a=Mat4.fromTranslation(new Vec3(2,3,4));const b=new Mat4(a);a.m[12]=99;return b.m;})()', translated4),
            'mat3_string': ('new Mat3("1 0 0 0 1 0 2 3 1").m', translated3),
            'mat4_string': ('new Mat4("1 0 0 0 0 1 0 0 0 0 1 0 2 3 4 1").m', translated4),
            'mat3_short_string': ('new Mat3("1 2 3").m', identity3),
            'mat4_short_string': ('new Mat4("1 2 3").m', identity4),
            'mat3_array': ('new Mat3([1,0,0,0,1,0,2,3,1]).m', translated3),
            'mat4_array': ('new Mat4([1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1]).m', translated4),
            'mat3_setter_identity': ('(()=>{const a=new Mat3();return a.translation(new Vec2(2,3))===a;})()', True),
            'mat4_setter_identity': ('(()=>{const a=new Mat4();return a.translation(new Vec3(2,3,4))===a;})()', True),
            'mat3_translation_chain': ('new Mat3().translation(new Vec2(2,3)).multiply(Mat3.identity()).m', translated3),
            'mat4_translation_chain': ('new Mat4().translation(new Vec3(2,3,4)).multiply(Mat4.identity()).m', translated4),
            'mat4_vec2_translation': ('Mat4.fromTranslation(new Vec3(9)).translation(new Vec2(2,3)).translation().z', 0),
            'mat3_plain_object_is_getter': ('(()=>{const a=Mat3.fromTranslation(new Vec2(2,3));const v=a.translation({x:9,y:8});return[v.x,v.y];})()', [2, 3]),
            'mat4_plain_object_is_getter': ('(()=>{const a=Mat4.fromTranslation(new Vec3(2,3,4));const v=a.translation({x:9,y:8,z:7});return[v.x,v.y,v.z];})()', [2, 3, 4]),
        })


if __name__ == '__main__':
    unittest.main()
