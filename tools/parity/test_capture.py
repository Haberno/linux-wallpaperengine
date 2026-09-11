#!/usr/bin/env python3
"""Containment controls: run without starting a display or renderer."""
import json
import os
from pathlib import Path
import tempfile
import unittest

import capture


class CaptureTests(unittest.TestCase):
    def test_original_and_bare_tmp_are_rejected(self):
        for path in ('/tmp', '/home/admin/.local/share/Steam', '/'):
            with self.assertRaises(ValueError):
                capture.private_root(Path(path))
        self.assertEqual(capture.private_root(Path('/tmp/parity-unit')), Path('/tmp/parity-unit'))

    def test_private_launch_cannot_inherit_host_display_audio_or_socket(self):
        env = capture.private_environment({'DISPLAY': ':1', 'WAYLAND_DISPLAY': 'wayland-1',
            'LD_PRELOAD': '/host/inject.so', 'WPE_CONTROL_SOCKET': '/tmp/live.sock',
            'PULSE_SERVER': 'unix:/run/user/1000/pulse/native'}, Path('/tmp/parity-unit'))
        self.assertNotIn('DISPLAY', env)
        self.assertNotIn('WAYLAND_DISPLAY', env)
        self.assertNotIn('LD_PRELOAD', env)
        self.assertNotEqual(env['WPE_CONTROL_SOCKET'], '/tmp/live.sock')
        self.assertNotEqual(env['PULSE_SERVER'], 'unix:/run/user/1000/pulse/native')
        self.assertEqual(env['LWE_HOST_DISPLAY'], ':1')

    def test_properties_are_validated_before_private_project_is_changed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'project.json'
            original = json.dumps({'general': {'properties': {'camera': {'value': 0, 'type': 'combo'}}}})
            path.write_text(original)
            with self.assertRaises(ValueError):
                capture.set_properties(path, {'camera': 1, 'missing': True})
            self.assertEqual(path.read_text(), original)
            capture.set_properties(path, {'camera': 2})
            self.assertEqual(json.loads(path.read_text())['general']['properties']['camera']['value'], 2)

    def test_capture_command_targets_exact_window_and_keeps_rgb(self):
        cmd = capture.video_command(':7', '0x123', 320, 180, 30, 90, Path('/tmp/clip.mkv'))
        self.assertEqual(cmd[cmd.index('-window_id') + 1], '291')
        self.assertEqual(cmd[cmd.index('-i') + 1], ':7')
        self.assertEqual(cmd[cmd.index('-c:v') + 1], 'ffv1')
        self.assertEqual(cmd[cmd.index('-pix_fmt') + 1], 'bgr0')
        self.assertEqual(cmd[cmd.index('-fps_mode') + 1], 'passthrough')
        self.assertNotIn('-r', cmd)
        self.assertNotIn('-vf', cmd)

    def test_prefix_ownership_accepts_proton_trailing_slash_but_not_siblings(self):
        self.assertTrue(capture.owns_prefix([b'WINEPREFIX=/tmp/owned/compat/pfx/'], Path('/tmp/owned')))
        self.assertFalse(capture.owns_prefix([b'WINEPREFIX=/tmp/owned-other/compat/pfx/'], Path('/tmp/owned')))

    def test_input_boundary_distinguishes_generated_cache_from_authored_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            item = Path(directory)
            (item / 'project.json').write_text('{}')
            expected = capture.source_manifest(item)
            cache = item / 'shaders/blobsSM40/123abc.dxs'
            cache.parent.mkdir(parents=True)
            cache.write_bytes(b'compiled shader')
            generated = capture.check_input_boundary(item, expected)
            self.assertEqual([record['path'] for record in generated], ['shaders/blobsSM40/123abc.dxs'])
            (item / 'project.json').write_text('{"changed":true}')
            with self.assertRaises(ValueError):
                capture.check_input_boundary(item, expected)


if __name__ == '__main__':
    unittest.main()
