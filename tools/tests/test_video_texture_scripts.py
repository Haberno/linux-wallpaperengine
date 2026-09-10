"""Opt-in SceneScript controls exercised against a real embedded MP4 decoder."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from scene_render import render_scene
from test_script_property_animation import scene_with


def write_video(root):
    for folder in ('models', 'materials'):
        (root / folder).mkdir(parents=True)
    video = root / 'source.mp4'
    subprocess.run([
        'ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
        'testsrc2=size=64x64:rate=20:duration=3', '-an', '-c:v', 'libx264',
        '-preset', 'ultrafast', '-pix_fmt', 'yuv420p', str(video)], check=True)
    payload = video.read_bytes()
    header = (b'TEXV0005\0TEXI0001\0' + struct.pack('<7I', 0, 34, 64, 64, 64, 64, 0)
              + b'TEXB0004\0' + struct.pack('<9I', 1, 0xffffffff, 0, 1, 64, 64, 0, 0, len(payload)))
    (root / 'materials/video.tex').write_bytes(header + payload)
    (root / 'materials/video.json').write_text(json.dumps({'passes': [{
        'shader': 'genericimage2', 'textures': ['video'], 'blending': 'normal',
        'cullmode': 'nocull', 'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
    (root / 'models/video.json').write_text(json.dumps({
        'width': 64, 'height': 64, 'material': 'materials/video.json'}))


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY') and shutil.which('ffmpeg'),
                     'Set LWE_TEST_BINARY and install ffmpeg for graphics tests')
class VideoTextureScripts(unittest.TestCase):
    def test_video_stops_resumes_seeks_and_uses_seconds_at_low_render_fps(self):
        script = '''
let video, phase=0, since=0, pausedTime=0;
function check(ok, message) { if (!ok) throw Error(message); }
export function init() {
    video = thisLayer.getVideoTexture();
    video.loop = false;
    video.rate = 1;
    video.stop();
    video.setCurrentTime(0);
    check(!video.isPlaying(), 'init stop did not pause');
}
export function update(value) {
    const now = engine.runtime;
    if (phase === 0 && now > .4) {
        check(video.getCurrentTime() < .1, 'stopped video advanced');
        check(Math.abs(video.duration-3) < .1, 'duration');
        check(video.loop === false && video.rate === 1, 'playback properties');
        video.play(); since=now; phase++;
    } else if (phase === 1 && now-since > .8) {
        pausedTime = video.getCurrentTime();
        check(pausedTime > .55 && pausedTime < 1.2, 'wrong video time '+pausedTime);
        video.pause(); since=now; phase++;
    } else if (phase === 2 && now-since > .4) {
        check(!video.isPlaying(), 'pause state');
        check(Math.abs(video.getCurrentTime()-pausedTime) < .15, 'paused video advanced');
        video.setCurrentTime(2.2); since=now; phase++;
    } else if (phase === 3 && now-since > .4) {
        check(Math.abs(video.getCurrentTime()-2.2) < .1, 'seek while paused');
        video.stop(); since=now; phase++;
    } else if (phase === 4 && now-since > .4) {
        check(video.getCurrentTime() < .1, 'stop did not rewind');
        video.play(); since=now; phase++;
    } else if (phase === 5 && now-since > .5) {
        check(video.isPlaying() && video.getCurrentTime() > .25, 'replay did not start');
        video.pause(); phase++;
        console.log('VIDEO_CONTROLS_OK');
    }
    return value;
}
'''
        with tempfile.TemporaryDirectory(prefix='lwe-video-controls-') as directory:
            root = Path(directory)
            write_video(root)
            scene = scene_with([{'id': 1, 'name': 'video controller', 'image': 'models/video.json',
                                 'origin': '160 90 0', 'size': '128 128',
                                 'visible': {'value': True, 'script': script}}])
            frame, output = render_scene(self, root, scene, frames=48)
            self.assertIsNotNone(frame.getbbox(), 'The controlled video must decode visible pixels')
            self.assertIn('VIDEO_CONTROLS_OK', output)


if __name__ == '__main__':
    unittest.main()
