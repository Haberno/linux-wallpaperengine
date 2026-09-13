"""Small isolated-scene renderer shared by opt-in graphics regressions."""
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time


def render_scene(test, root, scene, frames=5, parallax=False):
    from PIL import Image

    root.mkdir(parents=True, exist_ok=True)
    (root / 'project.json').write_text(json.dumps({
        'type': 'scene', 'file': 'scene.json', 'title': 'Rendering regression'}))
    (root / 'scene.json').write_text(json.dumps(scene))
    frame = root / 'frame.png'
    log_path = root / 'engine.log'
    binary = Path(os.environ['LWE_TEST_BINARY']).resolve()
    env = dict(os.environ, LD_LIBRARY_PATH=str(binary.parent), WPE_LOG_FILE='off',
               WPE_CONTROL_SOCKET=str(root / 'control.sock'),
               WPE_HEALTH_REPORT=str(root / 'health.json'))
    with log_path.open('w') as log:
        process = subprocess.Popen([
            str(binary), '--window', '0x0x320x180', '--silent', '--fps', '10',
            '--no-full-screen-pause', '--disable-mouse',
            *([] if parallax else ['--disable-parallax']),
            '--no-audio-processing', '--screenshot', str(frame),
            '--screenshot-delay', str(frames), str(root)],
            env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 20
            while process.poll() is None and time.monotonic() < deadline:
                if frame.exists():
                    time.sleep(.1)  # Finish writing the image before stopping.
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
    output = log_path.read_text()
    if os.environ.get('LWE_TEST_ARTIFACTS'):
        destination = Path(os.environ['LWE_TEST_ARTIFACTS']) / root.name
        shutil.copytree(root, destination, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('*.sock'))
    test.assertEqual(process.returncode, 0, output)
    test.assertTrue(frame.exists(), output)
    test.assertNotIn('ScriptEngine [', output)
    return Image.open(frame).convert('RGB'), output
