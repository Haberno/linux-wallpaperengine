"""General-language callbacks through the real scene runtime and control socket."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import unittest


def hooks(label):
    return '''
let first = true;
export function init(value) { console.log('GS_LABEL_INIT'); return value; }
export function applyUserProperties(p) { console.log('GS_LABEL_USER'); }
export function applyGeneralSettings(s) {
    console.log('GS_LABEL_GENERAL ' + JSON.stringify(s));
}
export function update(value) {
    if (first) { first = false; console.log('GS_LABEL_READY'); }
    return value;
}
'''.replace('LABEL', label)


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for runtime tests')
class GeneralSettings(unittest.TestCase):
    def wait_for(self, log, text, process):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline and process.poll() is None:
            if text in log.read_text() and (log.parent / 'control.sock').exists():
                return
            time.sleep(.05)
        self.fail(f'Missing {text}:\n{log.read_text()}')

    @contextmanager
    def running(self, objects=None, scene_script=None, arguments=(), locale='en_US.UTF-8'):
        with tempfile.TemporaryDirectory(prefix='lwe-general-settings-') as directory:
            root = Path(directory)
            scene = {
                'camera': {'eye': '160 90 100', 'center': '160 90 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                            'clearcolor': '0 0 0', 'camerafade': False},
                'objects': objects or [self.layer(1, hooks('LAYER'))]}
            if scene_script:
                scene['general']['bloomstrength'] = {'value': 0, 'script': scene_script}
            (root / 'scene.json').write_text(json.dumps(scene))
            (root / 'project.json').write_text(json.dumps({
                'type': 'scene', 'file': 'scene.json', 'title': 'General settings test',
                'general': {'properties': {'probe': {'type': 'slider', 'value': 1, 'min': 0, 'max': 2}}}}))
            log = root / 'engine.log'
            env = dict(os.environ, LC_ALL=locale, WPE_LOG_FILE='off',
                       WPE_CONTROL_SOCKET=str(root / 'control.sock'),
                       WPE_HEALTH_REPORT=str(root / 'health.json'))
            with log.open('w') as output:
                process = subprocess.Popen([
                    os.environ['LWE_TEST_BINARY'], '--window', '0x0x320x180', '--silent',
                    '--no-audio-processing', '--no-full-screen-pause', '--fps', '15',
                    *arguments, str(root)], env=env, stdout=output, stderr=subprocess.STDOUT)
                try:
                    yield root, log, process
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                    if os.environ.get('LWE_TEST_ARTIFACTS'):
                        shutil.copytree(root, Path(os.environ['LWE_TEST_ARTIFACTS']) / root.name,
                                        dirs_exist_ok=True, ignore=shutil.ignore_patterns('*.sock'))
            self.assertEqual(process.returncode, 0, log.read_text())

    @staticmethod
    def layer(index, script):
        return {'id': index, 'name': 'Layer' + str(index), 'image': 'models/util/solidlayer.json',
                'size': '10 10', 'visible': {'value': False, 'script': script}}

    def command(self, root, text):
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(5)
            client.connect(str(root / 'control.sock'))
            client.sendall((text + '\n').encode())
            return client.recv(65536).decode().strip()

    def test_startup_language_arrives_after_user_properties_before_update(self):
        with self.running(scene_script=hooks('SCENE'), locale='fr_CA.UTF-8') as (_, log, process):
            self.wait_for(log, 'GS_LAYER_READY', process)
            output = log.read_text()
            for label in ('LAYER', 'SCENE'):
                events = ['GS_' + label + suffix for suffix in (
                    '_INIT', '_USER', '_GENERAL {"language":"fr-fr"}', '_READY')]
                for event in events:
                    self.assertEqual(output.count(event), 1, output)
                self.assertEqual([output.index(event) for event in events],
                                 sorted(output.index(event) for event in events), output)
            self.assertNotIn('ScriptEngine [', output)

    def test_startup_created_layers_receive_language_after_parent_initialization(self):
        child = self.layer(2, hooks('CHILD'))
        parent = hooks('PARENT').replace("console.log('GS_PARENT_INIT');",
                                        "console.log('GS_PARENT_INIT'); thisScene.createLayer(" + json.dumps(child) + ');')
        with self.running(objects=[self.layer(1, parent)]) as (_, log, process):
            self.wait_for(log, 'GS_CHILD_READY', process)
            output = log.read_text()
            self.assertIn('GS_CHILD_GENERAL {"language":"en-us"}', output)
            self.assertLess(output.index('GS_PARENT_USER'), output.index('GS_CHILD_GENERAL'), output)
            self.assertLess(output.index('GS_PARENT_GENERAL'), output.index('GS_CHILD_GENERAL'), output)
            self.assertLess(output.index('GS_CHILD_GENERAL'), output.index('GS_CHILD_READY'), output)

    def test_live_changes_override_locale_and_skip_unchanged_language(self):
        with self.running(arguments=('--language', 'zh_TW'), locale='fr_FR.UTF-8') as (root, log, process):
            self.wait_for(log, 'GS_LAYER_READY', process)
            self.assertIn('GS_LAYER_GENERAL {"language":"zh-cht"}', log.read_text())
            self.assertEqual(self.command(root, 'language'), 'ok zh-cht')
            self.assertEqual(self.command(root, 'language fr_CA.UTF-8'), 'ok fr-fr')
            self.wait_for(log, 'GS_LAYER_GENERAL {"language":"fr-fr"}', process)
            self.assertEqual(self.command(root, 'language fr-fr'), 'ok fr-fr')
            self.assertEqual(self.command(root, 'language ja-jp'), 'ok ja-jp')
            self.wait_for(log, 'GS_LAYER_GENERAL {"language":"ja-jp"}', process)
            output = log.read_text()
            self.assertEqual(output.count('GS_LAYER_GENERAL'), 3, output)
            self.assertEqual(output.count('GS_LAYER_INIT'), 1, output)
            self.assertEqual(output.count('GS_LAYER_USER'), 1, output)

    def test_layers_created_during_update_wait_for_the_next_language_change(self):
        child = self.layer(2, hooks('CHILD'))
        parent = hooks('PARENT').replace("console.log('GS_PARENT_READY');",
                                        "console.log('GS_PARENT_READY'); thisScene.createLayer(" + json.dumps(child) + ');')
        with self.running(objects=[self.layer(1, parent)]) as (root, log, process):
            self.wait_for(log, 'GS_CHILD_READY', process)
            self.assertNotIn('GS_CHILD_GENERAL', log.read_text())
            self.assertEqual(self.command(root, 'language ja-jp'), 'ok ja-jp')
            self.wait_for(log, 'GS_CHILD_GENERAL {"language":"ja-jp"}', process)
            self.assertEqual(log.read_text().count('GS_CHILD_GENERAL'), 1)

    def test_language_survives_wallpaper_switch(self):
        with self.running() as (root, log, process):
            self.wait_for(log, 'GS_LAYER_READY', process)
            self.assertEqual(self.command(root, 'language fr-fr'), 'ok fr-fr')
            replacement = root / 'replacement'
            replacement.mkdir()
            shutil.copyfile(root / 'project.json', replacement / 'project.json')
            scene = json.loads((root / 'scene.json').read_text())
            scene['objects'] = [self.layer(1, hooks('REPLACEMENT'))]
            (replacement / 'scene.json').write_text(json.dumps(scene))
            self.assertEqual(self.command(root, 'switch default fade ' + str(replacement)), 'ok')
            self.wait_for(log, 'GS_REPLACEMENT_READY', process)
            self.assertIn('GS_REPLACEMENT_GENERAL {"language":"fr-fr"}', log.read_text())
            self.assertEqual(self.command(root, 'language ja-jp'), 'ok ja-jp')
            self.wait_for(log, 'GS_REPLACEMENT_GENERAL {"language":"ja-jp"}', process)
            self.assertEqual(log.read_text().count('GS_REPLACEMENT_GENERAL'), 2)

    def test_callback_creation_mutation_and_exception_do_not_break_other_scripts(self):
        child = self.layer(3, hooks('CHILD'))
        parent = hooks('PARENT').replace(
            "console.log('GS_PARENT_GENERAL ' + JSON.stringify(s));",
            "console.log('GS_PARENT_GENERAL ' + JSON.stringify(s));"
            "if (s.language === 'fr-fr') { thisScene.createLayer(" + json.dumps(child) + ");"
            "s.language='modified'; throw Error('deliberate general-settings exception'); }")
        with self.running(objects=[self.layer(1, parent), self.layer(2, hooks('OBSERVER'))]) as (root, log, process):
            self.wait_for(log, 'GS_OBSERVER_READY', process)
            self.assertEqual(self.command(root, 'language fr-fr'), 'ok fr-fr')
            self.wait_for(log, 'GS_CHILD_READY', process)
            self.assertEqual(self.command(root, 'language ja-jp'), 'ok ja-jp')
            self.wait_for(log, 'GS_CHILD_GENERAL {"language":"ja-jp"}', process)
            output = log.read_text()
            for label in ('PARENT', 'OBSERVER', 'CHILD'):
                self.assertIn('GS_' + label + '_GENERAL {"language":"fr-fr"}', output)
                self.assertIn('GS_' + label + '_GENERAL {"language":"ja-jp"}', output)
            self.assertEqual(output.count('GS_CHILD_GENERAL {"language":"fr-fr"}'), 1, output)
            self.assertLess(output.index('GS_CHILD_GENERAL'), output.index('GS_CHILD_READY'), output)
            self.assertIn('deliberate general-settings exception', output)


if __name__ == '__main__':
    unittest.main()
