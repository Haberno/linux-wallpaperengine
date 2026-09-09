"""Opt-in render regression for text scripts and MPRIS startup delivery.

LWE_TEST_BINARY=/path/to/engine python3 -m unittest discover -s tools/tests -p test_text_media.py
Requires a graphics session, dbus-python, PyGObject, Pillow and dbus-daemon.
The fake player runs on a private bus, leaving desktop media players untouched.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest


PLAYER = r'''
import dbus, dbus.service, dbus.mainloop.glib, json, sys
from gi.repository import GLib
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
class Player(dbus.service.Object):
    def __init__(self):
        self.name = dbus.service.BusName('org.mpris.MediaPlayer2.lwe_test', dbus.SessionBus())
        super().__init__(self.name, '/org/mpris/MediaPlayer2')
        self.state = 'Paused'
        self.title = 'Track One'
    @dbus.service.method('org.freedesktop.DBus.Properties', in_signature='ss', out_signature='v')
    def Get(self, iface, prop):
        if prop == 'PlaybackStatus': return self.state
        if prop == 'Position': return dbus.Int64(0)
        if prop == 'Metadata': return dbus.Dictionary({
            'xesam:title': self.title,
            'xesam:artist': dbus.Array(['Code therapy w / R...'], signature='s'),
        }, signature='sv')
        raise dbus.exceptions.DBusException('Unknown property')
    @dbus.service.signal('org.freedesktop.DBus.Properties', signature='sa{sv}as')
    def PropertiesChanged(self, iface, props, invalidated): pass
    def command(self, source, condition):
        line = sys.stdin.readline()
        if not line: return False
        self.state, self.title = json.loads(line)
        self.PropertiesChanged('org.mpris.MediaPlayer2.Player', {
            'PlaybackStatus': self.state,
            'Metadata': self.Get('', 'Metadata')}, [])
        print('READY', flush=True)
        return True
player = Player()
GLib.io_add_watch(sys.stdin, GLib.IO_IN, player.command)
print('READY', flush=True)
GLib.MainLoop().run()
'''

TITLE_SCRIPT = '''
let title = '';
export function init() {
    shared.titleInits = (shared.titleInits || 0) + 1;
    if (shared.titleInits !== 1) throw new Error('Text script initialized twice');
}
export function mediaPropertiesChanged(event) {
    title = event.title;
    console.log('TEXT_MEDIA_TITLE ' + title);
}
export function update(value) { return title; }
'''

FALLBACK_SCRIPT = '''
export var scriptProperties = createScriptProperties()
    .addText({name: 'layerName', value: ''}).finish();
let sound, state = -1;
export function init() {
    sound = thisScene.getLayer(scriptProperties.layerName);
    if (!sound) throw new Error('Missing named sound layer');
    sound.pause();
    if (sound.isPlaying()) throw new Error('pause did not pause');
    sound.play();
    if (!sound.isPlaying()) throw new Error('play did not resume');
    sound.stop();
    if (sound.isPlaying()) throw new Error('stop did not stop');
    sound.play();
    console.log('TEXT_SOUND_CONTROLS_OK');
}
export function mediaPlaybackChanged(event) {
    state = event.state;
    if (state === 1) sound.pause(); else sound.play();
    console.log('TEXT_MEDIA_STATE ' + state);
}
export function update() {
    // This initially empty layer has no text script. The visible-property
    // script writes it, just as A Solitary Reflection's fallback caption does.
    thisLayer.text = sound.isPlaying() ? scriptProperties.layerName : 'External player';
    return true;
}
'''


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for render integration tests')
class TextMedia(unittest.TestCase):
    def test_startup_metadata_and_cross_property_text_reach_the_renderer(self):
        from PIL import Image, ImageChops

        with tempfile.TemporaryDirectory(prefix='lwe-text-media-') as directory:
            root = Path(directory)
            daemon = subprocess.Popen(['dbus-daemon', '--session', '--nofork', '--print-address=1'],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.addCleanup(self.stop, daemon)
            address = daemon.stdout.readline().strip()
            self.assertTrue(address.startswith('unix:'))
            env = dict(os.environ, DBUS_SESSION_BUS_ADDRESS=address, WPE_LOG_FILE='off')
            player = subprocess.Popen([sys.executable, '-u', '-c', PLAYER], env=env,
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.addCleanup(self.stop, player)
            self.assertEqual(player.stdout.readline().strip(), 'READY')

            for state, title, fallback in [('Paused', 'Track One', 'mxpheebz - The Beach'),
                                           ('Playing', 'Track Two', 'External player')]:
                player.stdin.write(json.dumps([state, title]) + '\n')
                player.stdin.flush()
                self.assertEqual(player.stdout.readline().strip(), 'READY')
                frames = []
                for scripted in (True, False):
                    fixture = root / f'{state}-{scripted}'
                    fixture.mkdir()
                    objects = []
                    for ident, text in enumerate((title, 'Code therapy w / R...', fallback), 1):
                        obj = {'id': ident, 'name': f'Text {ident}', 'origin': f'320 {310-ident*75} 0',
                               'pointsize': 10, 'text': text}
                        if scripted and ident == 1:
                            obj['text'] = {'value': '', 'script': TITLE_SCRIPT}
                        if scripted and ident == 2:
                            obj['text'] = {'value': '', 'script': '''
let artist = '';
export function mediaPropertiesChanged(event) { artist = event.artist; }
export function update(value) { return artist; }
'''}
                        if scripted and ident == 3:
                            obj['text'] = ''
                            obj['visible'] = {'value': True, 'script': FALLBACK_SCRIPT,
                                              'scriptproperties': {'layerName': 'mxpheebz - The Beach'}}
                        objects.append(obj)
                    # Muted engine keeps logical sound state without opening a decoder.
                    objects.append({'id': 4, 'name': 'mxpheebz - The Beach', 'sound': [], 'playbackmode': 'loop'})
                    (fixture / 'project.json').write_text(json.dumps({
                        'type': 'scene', 'file': 'scene.json', 'title': 'Text media regression'}))
                    (fixture / 'scene.json').write_text(json.dumps({
                        'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                        'general': {'orthogonalprojection': {'width': 640, 'height': 360},
                                    'clearcolor': '0 0 0', 'clearenabled': True}, 'objects': objects}))
                    frame = fixture / 'frame.png'
                    log_path = fixture / 'engine.log'
                    run_env = dict(env, WPE_CONTROL_SOCKET=str(fixture / 'control.sock'),
                                   WPE_HEALTH_REPORT=str(fixture / 'health.json'))
                    with log_path.open('w') as log:
                        process = subprocess.Popen([
                            str(Path(os.environ['LWE_TEST_BINARY']).resolve()), '--window', '0x0x640x360',
                            '--volume', '0', '--fps', '20', '--no-full-screen-pause',
                            '--screenshot', str(frame), '--screenshot-delay', '5', str(fixture)],
                            env=run_env, stdout=log, stderr=subprocess.STDOUT)
                        try:
                            deadline = time.monotonic() + 15
                            while process.poll() is None and not frame.exists() and time.monotonic() < deadline:
                                time.sleep(.05)
                            self.assertTrue(frame.exists(), log_path.read_text()[-4000:])
                            if scripted:
                                # An unchanged player has position zero and sends no signals
                                # during startup: only cached-event delivery can populate this.
                                self.assertIn('TEXT_MEDIA_TITLE ' + title, log_path.read_text())
                                self.assertIn('TEXT_SOUND_CONTROLS_OK', log_path.read_text())
                                # Also exercise later events on the same live script instances.
                                player.stdin.write(json.dumps([state, 'Changed Track']) + '\n')
                                player.stdin.flush()
                                self.assertEqual(player.stdout.readline().strip(), 'READY')
                                deadline = time.monotonic() + 3
                                while 'TEXT_MEDIA_TITLE Changed Track' not in log_path.read_text() and time.monotonic() < deadline:
                                    time.sleep(.05)
                                self.assertIn('TEXT_MEDIA_TITLE Changed Track', log_path.read_text())
                        finally:
                            self.stop(process)
                    self.assertEqual(process.returncode, 0, log_path.read_text()[-4000:])
                    self.assertNotIn('ScriptEngine [', log_path.read_text())
                    frames.append(Image.open(frame).convert('RGB'))
                self.assertIsNotNone(frames[1].getbbox(), 'Static reference must contain visible text')
                self.assertIsNone(ImageChops.difference(*frames).getbbox(),
                                  f'{state}: rendered scripts differ from the expected static captions')

    @staticmethod
    def stop(process):
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
        for name in ('stdin', 'stdout', 'stderr'):
            stream = getattr(process, name, None)
            if stream:
                stream.close()


if __name__ == '__main__':
    unittest.main()
