"""Muted scene-script sound regressions with an isolated media bus.

LWE_TEST_BINARY=build-release/output/linux-wallpaperengine python3 -m unittest \
    discover -s tools/tests -p test_sound_layers.py
Requires a graphics session and dbus-daemon. Produces no audio output.
Set LWE_TEST_ARTIFACT_DIR to retain diagnostic logs for before/after comparisons.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for scene integration tests')
class SoundLayers(unittest.TestCase):
    def test_muted_sound_volume_scripts_properties_and_layer_writes(self):
        script = '''
let frames = 0;
export function init(value) {
    const dynamic = thisScene.getLayer('Scripted Sound');
    const user = thisScene.getLayer('User Sound');
    if (Math.abs(dynamic.volume - 0.5) > 0.00001 || typeof dynamic.volume !== 'number')
        throw new Error('Authored sound volume is missing');
    if (Math.abs(user.volume - 0.6) > 0.00001 || typeof user.volume !== 'number')
        throw new Error('Sound user property did not bind');
    dynamic.volume = 0.33;
    if (Math.abs(dynamic.volume - 0.33) > 0.00001)
        throw new Error('Sound volume cannot be written');
    return value;
}
export function update(value) {
    if (++frames === 3) {
        const sound = thisScene.getLayer('Scripted Sound');
        if (!shared.soundVolumeUpdated || Math.abs(sound.volume - 0.2) > 0.00001)
            throw new Error('Muted sound volume script did not run');
        if (!sound.isPlaying()) throw new Error('Volume changed logical playback state');
        console.log('SOUND_VOLUME_OK authored=0.5 user=0.6 write=0.33 update=0.2');
    }
    return value;
}
'''
        sounds = [
            {'id': 1, 'name': 'Scripted Sound', 'sound': ['sounds/not-opened-while-muted.ogg'],
             'playbackmode': 'loop', 'volume': {'value': 0.5, 'script': '''
export function update(value) { shared.soundVolumeUpdated = true; return 0.2; }
'''}},
            {'id': 2, 'name': 'User Sound', 'sound': [], 'volume': {'value': 0.5, 'user': 'soundvolume'}},
        ]
        self.run_scene('gain', sounds, script, 'SOUND_VOLUME_OK')

    def test_initial_silence_and_explicit_controls_work_while_muted(self):
        script = '''
export function init(value) {
    const automatic = thisScene.getLayer('Automatic Sound');
    const silent = thisScene.getLayer('Silent Sound');
    if (!automatic.isPlaying()) throw new Error('Default sound should start playing');
    if (silent.isPlaying()) throw new Error('startsilent was ignored');
    silent.play();
    if (!silent.isPlaying()) throw new Error('play did not override initial silence');
    silent.pause();
    if (silent.isPlaying()) throw new Error('pause did not preserve explicit state');
    silent.play();
    if (!silent.isPlaying()) throw new Error('play did not resume');
    silent.stop();
    if (silent.isPlaying()) throw new Error('stop did not preserve explicit state');
    console.log('SOUND_STARTSILENT_OK default=playing silent=stopped play/pause/resume/stop=ok');
    return value;
}
'''
        sounds = [
            {'id': 1, 'name': 'Automatic Sound', 'sound': [], 'playbackmode': 'loop'},
            {'id': 2, 'name': 'Silent Sound', 'sound': ['sounds/not-opened-while-muted.ogg'],
             'playbackmode': 'loop', 'startsilent': True},
        ]
        self.run_scene('startsilent', sounds, script, 'SOUND_STARTSILENT_OK')

    def run_scene(self, label, sounds, script, marker):
        with tempfile.TemporaryDirectory(prefix='lwe-sound-layers-') as directory:
            root = Path(directory)
            (root / 'project.json').write_text(json.dumps({
                'type': 'scene', 'file': 'scene.json', 'title': 'Sound layer regression',
                'general': {'properties': {'soundvolume': {'type': 'slider', 'value': 0.6}}}}))
            (root / 'scene.json').write_text(json.dumps({
                'camera': {'eye': '0 0 1', 'center': '0 0 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': {'width': 640, 'height': 360},
                            'clearcolor': '0 0 0', 'clearenabled': True},
                'objects': sounds + [{'id': 3, 'name': 'Probe', 'origin': '320 180 0', 'pointsize': 12,
                                      'text': {'value': 'Sound regression', 'script': script}}]}))
            path = root / 'engine.log'
            # No service directories: failures must not auto-start notification or
            # keyring services on the isolated bus during a before-fix run.
            bus_config = root / 'dbus.conf'
            bus_config.write_text('''<busconfig>
<type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth>
<policy context="default"><allow send_destination="*"/><allow receive_sender="*"/><allow own="*"/></policy>
</busconfig>''')
            daemon = subprocess.Popen(['dbus-daemon', '--nofork', '--print-address=1',
                                       '--config-file=' + str(bus_config)],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            def stop_bus():
                if daemon.poll() is None:
                    daemon.terminate()
                    daemon.wait(timeout=3)
                daemon.stdout.close()
                daemon.stderr.close()
            self.addCleanup(stop_bus)
            address = daemon.stdout.readline().strip()
            self.assertTrue(address.startswith('unix:'), address)
            env = dict(os.environ, WPE_CONTROL_SOCKET=str(root / 'control.sock'), WPE_LOG_FILE='off',
                       WPE_HEALTH_REPORT=str(root / 'health.json'), DBUS_SESSION_BUS_ADDRESS=address)
            with path.open('w') as log:
                process = subprocess.Popen([
                    str(Path(os.environ['LWE_TEST_BINARY']).resolve()),
                    '--window', '0x0x320x180', '--silent', '--fps', '30', '--no-full-screen-pause', str(root)],
                    env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    deadline = time.monotonic() + 12
                    while marker not in path.read_text() and process.poll() is None and time.monotonic() < deadline:
                        if 'ScriptEngine [' in path.read_text():
                            break
                        time.sleep(.05)
                    self.assertIn(marker, path.read_text(), path.read_text()[-6000:])
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()
                    artifacts = os.environ.get('LWE_TEST_ARTIFACT_DIR')
                    if artifacts:
                        output = Path(artifacts)
                        output.mkdir(parents=True, exist_ok=True)
                        (output / f'sound-{label}.log').write_text(path.read_text())
            self.assertEqual(process.returncode, 0, path.read_text()[-6000:])
            self.assertNotIn('ScriptEngine [', path.read_text())
