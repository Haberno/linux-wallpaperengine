"""Native-derived propagation regressions using an owned private X11 window."""
from contextlib import contextmanager
import ctypes as c
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src/WallpaperEngine/Testing'))
from test_scene_script_resize import X11
from test_text_effect_targets import write_copy_assets, base_scene


def hooks(label):
    return '\n'.join("export function cursor" + event + "(e){console.log('CP_" + label + ':' + event[0] + "');}"
                     for event in ('Enter', 'Leave', 'Move', 'Down', 'Up', 'Click'))


def layers(blocker=None, hidden=False, no_script=False, multi=False):
    result = []
    for index, label in ((70, 'A'), (2, 'B')):
        layer = {'id': index, 'name': label, 'image': 'models/probe.json',
                 'origin': '160 90 0', 'size': '120 80',
                 'visible': {'value': not (hidden and label == 'B'), 'script': hooks(label)}}
        if label == blocker:
            layer['disablepropagation'] = True
        result.append(layer)
    if no_script:
        result[1]['visible'] = True
    if multi:
        result[1]['alpha'] = {'value': 1, 'script': hooks('X')}
    return result


class Button(c.Structure):
    _fields_ = [('type', c.c_int), ('serial', c.c_ulong), ('send_event', c.c_int),
                ('display', c.c_void_p), ('window', c.c_ulong), ('root', c.c_ulong),
                ('subwindow', c.c_ulong), ('time', c.c_ulong), ('x', c.c_int), ('y', c.c_int),
                ('x_root', c.c_int), ('y_root', c.c_int), ('state', c.c_uint),
                ('button', c.c_uint), ('same_screen', c.c_int)]


class Pointer(X11):
    def __init__(self):
        super().__init__()
        self.position = (10, 150)
        self.lib.XWarpPointer.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int,
                                         c.c_uint, c.c_uint, c.c_int, c.c_int]
        self.lib.XSendEvent.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_long, c.c_void_p]
        self.lib.XTranslateCoordinates.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int,
                                                  c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_ulong)]

    def move(self, window, x, y):
        self.position = (x, y)
        self.lib.XWarpPointer(self.display, 0, window, 0, 0, 0, 0, x, y)
        self.lib.XSync(self.display, False)
        time.sleep(.12)

    def button(self, window, pressed):
        ox, oy, child = c.c_int(), c.c_int(), c.c_ulong()
        self.lib.XTranslateCoordinates(self.display, window, self.root, *self.position,
                                       c.byref(ox), c.byref(oy), c.byref(child))
        event = Button(type=4 if pressed else 5, display=self.display, window=window,
                       root=self.root, x=self.position[0], y=self.position[1], x_root=ox.value, y_root=oy.value,
                       button=1, same_screen=1, state=0 if pressed else 256)
        storage = c.create_string_buffer(24 * c.sizeof(c.c_long))
        c.memmove(storage, c.byref(event), c.sizeof(event))
        assert self.lib.XSendEvent(self.display, window, False, 1 << (event.type - 2), storage)
        self.lib.XSync(self.display, False)
        time.sleep(.12)


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for runtime tests')
class CursorPropagation(unittest.TestCase):
    @contextmanager
    def running(self, objects, screenshot=False):
        self.assertTrue(os.environ.get('DISPLAY'))
        self.assertNotEqual(os.environ['DISPLAY'], os.environ.get('LWE_HOST_DISPLAY'),
                            'Input tests require a private display')
        with tempfile.TemporaryDirectory(prefix='lwe-cursor-propagation-') as directory:
            root = Path(directory)
            write_copy_assets(root)
            (root / 'shaders/probe.vert').write_text((root / 'shaders/copy.vert').read_text())
            (root / 'shaders/probe.frag').write_text('uniform vec3 g_Color;void main(){gl_FragColor=vec4(g_Color,1);}')
            (root / 'materials/probe.json').write_text(json.dumps({'passes': [{'shader': 'probe',
                'blending': 'normal', 'depthtest': 'disabled', 'depthwrite': 'disabled', 'cullmode': 'nocull'}]}))
            (root / 'models/probe.json').write_text(json.dumps({'material': 'materials/probe.json'}))
            scene = base_scene(objects)
            scene['general'].update(camerafade=False, cameraparallax=False)
            scene['general']['bloomstrength'] = {'value': 0, 'script': "export function init(v){console.log('CP_READY');return v;}"}
            (root / 'scene.json').write_text(json.dumps(scene))
            (root / 'project.json').write_text(json.dumps({'type': 'scene', 'file': 'scene.json', 'title': 'Cursor propagation'}))
            log = root / 'engine.log'
            env = dict(os.environ, WPE_LOG_FILE='off', WPE_CONTROL_SOCKET=str(root / 'control.sock'),
                       WPE_HEALTH_REPORT=str(root / 'health.json'))
            env.pop('WAYLAND_DISPLAY', None)
            pointer = Pointer()
            with log.open('w') as output:
                process = subprocess.Popen([os.environ['LWE_TEST_BINARY'], '--window', '0x0x320x180',
                    '--silent', '--no-audio-processing', '--no-full-screen-pause', '--fps', '30',
                    *(['--screenshot', str(root / 'frame.png'), '--screenshot-delay', '5'] if screenshot else []), str(root)],
                    env=env, stdout=output, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 20
                    window = None
                    while process.poll() is None and time.monotonic() < deadline:
                        window = pointer.find(process.pid)
                        if window and 'CP_READY' in log.read_text() and (root / 'control.sock').exists():
                            break
                        time.sleep(.05)
                    self.assertIsNotNone(window, log.read_text())
                    yield pointer, window, log
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                    pointer.close()
                    if os.environ.get('LWE_TEST_ARTIFACTS'):
                        shutil.copytree(root, Path(os.environ['LWE_TEST_ARTIFACTS']) / root.name,
                                        dirs_exist_ok=True, ignore=shutil.ignore_patterns('*.sock'))
            self.assertEqual(process.returncode, 0, log.read_text())
            self.assertNotIn('ScriptEngine [', log.read_text())

    def trace(self, objects, include_moves=False):
        with self.running(objects) as (pointer, window, log):
            pointer.move(window, 10, 150)
            offset = len(log.read_text())
            pointer.move(window, 160, 90)
            pointer.button(window, True)
            pointer.button(window, False)
            pointer.move(window, 10, 150)
            return [line.split('CP_', 1)[1] for line in log.read_text()[offset:].splitlines()
                    if 'CP_' in line and (include_moves or not line.endswith(':M'))]

    def test_default_events_follow_front_to_back_layer_order(self):
        self.assertEqual(self.trace(layers()), ['B:E', 'A:E', 'B:D', 'A:D', 'B:U', 'B:C', 'A:U', 'A:C', 'B:L', 'A:L'])

    def test_blocker_stops_lower_hover_and_button_events(self):
        self.assertEqual(self.trace(layers('B')), ['B:E', 'B:D', 'B:U', 'B:C', 'B:L'])

    def test_move_reaches_front_layer_but_not_covered_layer(self):
        events = self.trace(layers('B'), include_moves=True)
        self.assertIn('B:M', events)
        self.assertFalse(any(event.startswith('A:') for event in events), events)

    def test_blocker_does_not_block_layers_above_it(self):
        self.assertEqual(self.trace(layers('A')), ['B:E', 'A:E', 'B:D', 'A:D', 'B:U', 'B:C', 'A:U', 'A:C', 'B:L', 'A:L'])

    def test_blocker_without_scripts_still_intercepts_events(self):
        self.assertEqual(self.trace(layers('B', no_script=True), include_moves=True), [])

    def test_hidden_blocker_receives_events_without_intercepting(self):
        self.assertEqual(self.trace(layers('B', hidden=True)), ['B:E', 'A:E', 'B:D', 'A:D', 'B:U', 'B:C', 'A:U', 'A:C', 'B:L', 'A:L'])

    def test_all_modules_on_blocking_layer_receive_each_event(self):
        self.assertEqual(self.trace(layers('B', multi=True)), ['X:E', 'B:E', 'X:D', 'B:D', 'X:U', 'B:U', 'X:C', 'B:C', 'X:L', 'B:L'])


    def test_reversing_authored_order_reverses_input_priority(self):
        self.assertEqual(self.trace(list(reversed(layers()))), ['A:E', 'B:E', 'A:D', 'B:D', 'A:U', 'A:C', 'B:U', 'B:C', 'A:L', 'B:L'])

    def test_transparent_blocker_still_intercepts(self):
        objects = layers('B')
        objects[1]['alpha'] = 0
        self.assertEqual(self.trace(objects), ['B:E', 'B:D', 'B:U', 'B:C', 'B:L'])

    def test_hidden_parent_disables_blocking_but_keeps_child_events(self):
        objects = layers('B')
        objects[1]['parent'] = 90
        objects.append({'id': 90, 'name': 'Parent', 'image': 'models/probe.json',
                        'size': '1 1', 'origin': '0 0 0', 'visible': False})
        self.assertEqual(self.trace(objects), ['B:E', 'A:E', 'B:D', 'A:D', 'B:U', 'B:C', 'A:U', 'A:C', 'B:L', 'A:L'])

    def test_revealing_blocker_in_down_suppresses_lower_down_without_forced_leave(self):
        objects = layers('B', hidden=True)
        objects[1]['visible']['script'] = hooks('B').replace("console.log('CP_B:D');", "console.log('CP_B:D');thisLayer.visible=true;")
        self.assertEqual(self.trace(objects), ['B:E', 'A:E', 'B:D', 'B:U', 'B:C', 'B:L', 'A:L'])

    def test_hiding_blocker_in_enter_immediately_allows_lower_events(self):
        objects = layers('B')
        objects[1]['visible']['script'] = hooks('B').replace("console.log('CP_B:E');", "console.log('CP_B:E');thisLayer.visible=false;")
        self.assertEqual(self.trace(objects), ['B:E', 'A:E', 'B:D', 'A:D', 'B:U', 'B:C', 'A:U', 'A:C', 'B:L', 'A:L'])

    def test_moving_blocker_in_enter_releases_the_old_hit_on_next_frame(self):
        objects = layers('B')
        objects[1]['visible']['script'] = hooks('B').replace("console.log('CP_B:E');", "console.log('CP_B:E');thisLayer.origin=new Vec3(260,90,0);")
        self.assertEqual(self.trace(objects), ['B:E', 'B:L', 'A:E', 'A:D', 'A:U', 'A:C', 'A:L'])


    def test_property_hooks_follow_native_key_order(self):
        objects = layers('B')
        for prop, label, value in [('alpha', 'X', 1), ('angles', 'R', '0 0 0'),
                                   ('color', 'C', '1 1 1'), ('origin', 'O', '160 90 0'),
                                   ('scale', 'S', '1 1 1'), ('visible', 'B', True)]:
            objects[1][prop] = {'value': value, 'script': "export function cursorDown(){console.log('CP_" + label + ":D');}"}
        self.assertEqual(self.trace(objects), ['X:D', 'R:D', 'C:D', 'O:D', 'S:D', 'B:D'])

    def test_dependencies_do_not_change_cursor_stacking(self):
        objects = layers('B')
        objects[0]['dependencies'] = [2]
        self.assertEqual(self.trace(objects), ['B:E', 'B:D', 'B:U', 'B:C', 'B:L'])

    def test_noop_sort_preserves_dependency_render_schedule(self):
        from PIL import Image
        pixels = []
        for sort in (False, True):
            objects = layers('B')
            objects[0]['dependencies'] = [2]
            objects[0]['color'] = '1 0 0'
            objects[1]['color'] = '0 0 1'
            if sort:
                objects[1]['visible']['script'] += 'export function init(v){thisScene.sortLayer(thisLayer,thisScene.getLayerIndex(thisLayer));return v;}'
            with self.running(objects, screenshot=True) as (_, window, log):
                frame = log.parent / 'frame.png'
                deadline = time.monotonic() + 10
                while not frame.exists() and time.monotonic() < deadline:
                    time.sleep(.05)
                self.assertTrue(frame.exists(), log.read_text())
                time.sleep(.05)
                with Image.open(frame) as image:
                    pixels.append(image.convert('RGB').getpixel((160, 90)))
        self.assertEqual(pixels[0], pixels[1], 'A no-op authored sort must not reschedule dependency rendering')

    def test_dependency_index_roundtrip_preserves_cursor_priority(self):
        objects = layers('B')
        objects[0]['dependencies'] = [2]
        objects[1]['visible']['script'] += "export function init(v){if(thisScene.getLayerIndex(thisLayer)!==1 || thisScene.enumerateLayers()[1]!==thisLayer)throw Error('wrong authored index');thisScene.sortLayer(thisLayer,thisScene.getLayerIndex(thisLayer));return v;}"
        self.assertEqual(self.trace(objects), ['B:E', 'B:D', 'B:U', 'B:C', 'B:L'])

    def test_sort_layer_updates_cursor_stacking(self):
        objects = layers('B')
        objects[0]['visible']['script'] += 'export function init(v){thisScene.sortLayer(thisLayer,99);return v;}'
        self.assertEqual(self.trace(objects), ['A:E', 'B:E', 'A:D', 'B:D', 'A:U', 'A:C', 'B:U', 'B:C', 'A:L', 'B:L'])

    def test_blocker_created_during_down_joins_on_next_frame_without_inheriting_press(self):
        objects = layers('B')
        child = layers('B')[1]
        child['visible']['script'] = hooks('X')
        objects[1]['visible']['script'] = hooks('B').replace("console.log('CP_B:D');", "console.log('CP_B:D');thisScene.createLayer(" + json.dumps(child) + ');')
        self.assertEqual(self.trace(objects), ['B:E', 'B:D', 'X:E', 'X:U', 'X:L', 'B:L'])

    def test_destroyed_blocker_does_not_leave_dangling_cursor_entries(self):
        objects = layers('B')
        objects[1]['visible']['script'] = hooks('B').replace("console.log('CP_B:D');", "console.log('CP_B:D');thisScene.destroyLayer(thisLayer);")
        self.assertEqual(self.trace(objects), ['B:E', 'B:D', 'A:E', 'A:U', 'A:L'])

    def test_covered_release_clears_press_before_later_outside_press(self):
        objects = layers('B', hidden=True)
        objects[0]['visible']['script'] = "let presses=0;export function cursorDown(){console.log('CP_A:D');if(++presses===1)thisScene.getLayer('B').visible=true;}export function cursorUp(){console.log('CP_A:U');}export function cursorClick(){console.log('CP_A:C');}"
        objects[1]['visible']['script'] = "let hide=false;export function cursorDown(){console.log('CP_B:D');}export function cursorUp(){console.log('CP_B:U');hide=true;}export function cursorClick(){console.log('CP_B:C');}export function update(v){return hide?false:v;}"
        with self.running(objects) as (pointer, window, log):
            pointer.move(window, 10, 150)
            offset = len(log.read_text())
            pointer.move(window, 160, 90)
            pointer.button(window, True)
            pointer.button(window, False)
            pointer.move(window, 10, 150)
            pointer.button(window, True)
            pointer.move(window, 160, 90)
            pointer.button(window, False)
            events = [line.split('CP_', 1)[1] for line in log.read_text()[offset:].splitlines() if 'CP_' in line]
            self.assertEqual(events, ['B:D', 'A:D', 'B:U', 'B:C', 'B:U', 'A:U'])


if __name__ == '__main__':
    unittest.main()
