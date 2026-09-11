#!/usr/bin/env python3
"""CPU regressions using actual lossless videos and independent expected errors."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import numpy as np
import parity


@unittest.skipUnless(shutil.which('ffmpeg') and shutil.which('ffprobe'), 'ffmpeg/ffprobe required')
class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.proof = {key: {'verified': True, 'evidence': 'controlled fixture test'} for key in parity.GATES}

    def tearDown(self):
        self.temp.cleanup()

    def video(self, name, levels, fps=10, times=None):
        frames = np.stack([np.full((8, 10, 3), value, np.uint8) for value in levels])
        path = self.root / (name + '.mkv')
        timing = []
        if times is not None:
            expression = str(times[-1] * 1000)
            for index, moment in reversed(list(enumerate(times[:-1]))):
                expression = f'if(eq(N,{index}),{moment * 1000},{expression})'
            timing = ['-vf', f'settb=1/1000,setpts={expression.replace(",", chr(92) + ",")}',
                      '-enc_time_base', '1:1000', '-fps_mode', 'passthrough']
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
                        '-s', '10x8', '-framerate', str(fps), '-i', '-', *timing, '-c:v', 'ffv1',
                        '-level', '3', '-pix_fmt', 'bgr0', str(path)], input=frames.tobytes(), check=True)
        return path

    def test_exact_rgb_and_known_spatial_error(self):
        a = self.video('a', [20, 30, 40, 50])
        same = parity.compare(a, a, evidence=self.proof)
        self.assertEqual(same['metrics']['mae_rgb8'], 0)
        self.assertEqual(same['status'], 'exploratory')
        self.assertTrue(any('Determinism:' in value for value in same['blockers']))
        b = self.video('b', [22, 32, 42, 52])
        changed = parity.compare(a, b, tolerance=1, evidence=self.proof)
        self.assertEqual(changed['metrics']['mae_rgb8'], 2)
        self.assertEqual(changed['metrics']['rmse_rgb8'], 2)
        self.assertEqual(changed['metrics']['changed_pixel_fraction'], 1)
        self.assertEqual(changed['metrics']['temporal_delta_mae_rgb8'], 0)

    def test_equal_temporal_mean_does_not_hide_reversed_motion(self):
        a = self.video('a', [10, 30, 10, 30])
        b = self.video('b', [30, 10, 30, 10])
        result = parity.compare(a, b)
        self.assertEqual(result['status'], 'exploratory')
        self.assertEqual(result['metrics']['temporal_mean_image_mae_rgb8'], 0)
        self.assertEqual(result['metrics']['temporal_delta_mae_rgb8'], 40)
        self.assertTrue(any('random_state' in value for value in result['blockers']))

    def test_explicit_offset_does_not_silently_hide_unmatched_frames(self):
        a = self.video('a', [10, 20, 30, 40])
        b = self.video('b', [0, 10, 20, 30, 40])
        result = parity.compare(a, b, offset=.1, evidence=self.proof)
        self.assertEqual(result['metrics']['mae_rgb8'], 0)
        self.assertEqual(result['matched_frames'], 4)
        self.assertEqual(result['unmatched_candidate_frames'], 1)
        self.assertEqual(result['status'], 'exploratory')

    def test_sampling_cadence_and_frozen_frames_are_reported(self):
        a = self.video('a', [10, 20, 30, 40])
        b = self.video('b', [10, 10, 10, 10])
        result = parity.compare(a, b)
        self.assertIn('cadence', result)
        self.assertAlmostEqual(result['cadence']['reference']['median_interval_seconds'], .1)
        self.assertEqual(result['metrics']['candidate_identical_adjacent_fraction'], 1)
        self.assertEqual(result['metrics']['reference_identical_adjacent_fraction'], 0)

    def test_evidence_boolean_is_not_proof(self):
        self.assertEqual(len(parity.evidence_blockers({key: True for key in parity.GATES})), len(parity.GATES))

    def test_repeat_run_baselines_do_not_certify_random_particles(self):
        a = self.video('a', [10, 20, 30, 40])
        b = self.video('b', [30, 40, 50, 60])
        ar = self.video('ar', [11, 21, 31, 41])
        br = self.video('br', [32, 42, 52, 62])
        result = parity.compare_with_repeats(a, b, ar, br)
        self.assertEqual(result['repeat_runs']['reference']['metrics']['mae_rgb8'], 1)
        self.assertEqual(result['repeat_runs']['candidate']['metrics']['mae_rgb8'], 2)
        self.assertEqual(result['status'], 'exploratory')

    def test_contradictory_repeats_override_manually_asserted_evidence(self):
        a = self.video('a', [10, 20, 30, 40])
        repeat = self.video('repeat', [40, 30, 20, 10])
        result = parity.compare_with_repeats(a, a, repeat, a, evidence=self.proof)
        self.assertEqual(result['status'], 'exploratory')
        self.assertTrue(any('repeat' in blocker for blocker in result['blockers']))

    def test_drifting_clock_is_not_absorbed_by_nearby_frame_matching(self):
        a = self.video('a', [10, 20, 30, 40, 50])
        b = self.video('b', [10, 20, 30, 40, 50], times=[0, .11, .22, .33, .44])
        result = parity.compare(a, b, evidence=self.proof)
        self.assertEqual(result['matched_frames'], 5)
        self.assertEqual(result['status'], 'exploratory')
        self.assertAlmostEqual(result['alignment']['sample_clock_drift_ppm'], 100000)

    def test_missing_sample_stays_visible_even_if_remaining_pixels_match(self):
        a = self.video('a', [10, 20, 30, 40, 50])
        b = self.video('b', [10, 20, 40, 50], times=[0, .1, .3, .4])
        result = parity.compare(a, b, evidence=self.proof)
        self.assertEqual(result['metrics']['mae_rgb8'], 0)
        self.assertEqual(result['unmatched_reference_frames'], 1)
        self.assertEqual(result['cadence']['candidate']['gaps_over_1_5_median'], 1)
        self.assertEqual(result['status'], 'exploratory')

    def test_contradictory_color_transfer_tags_block_equal_rgb_pixels(self):
        a = self.video('a', [10, 20, 30, 40])
        tagged = []
        for tag in ('linear', 'iec61966-2-1'):
            path = self.root / (tag + '.mkv')
            subprocess.run(['ffmpeg', '-v', 'error', '-i', str(a), '-vf',
                f'setparams=range=full:color_primaries=bt709:color_trc={tag}:colorspace=gbr',
                '-c:v', 'ffv1', str(path)], check=True)
            tagged.append(path)
        result = parity.compare(*tagged, evidence=self.proof)
        self.assertEqual(result['metrics']['mae_rgb8'], 0)
        self.assertTrue(any('color_transfer' in value and 'differ' in value for value in result['blockers']))


class InventoryTests(unittest.TestCase):
    def test_unchanged_project_does_not_hide_changed_scene_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            item = root / 'corpus/1'
            item.mkdir(parents=True)
            parity.write_json(item / 'project.json', {'title': 'fixture', 'type': 'scene', 'file': 'scene.json'})
            parity.write_json(item / 'scene.json', {'objects': []})
            before = parity.inventory(root / 'corpus', hash_assets=True)
            parity.write_json(item / 'scene.json', {'objects': [{'id': 1}]})
            after = parity.inventory(root / 'corpus', hash_assets=True)
            self.assertEqual(before['items'][0]['project_sha256'], after['items'][0]['project_sha256'])
            self.assertNotEqual(before['items'][0]['asset_digest'], after['items'][0]['asset_digest'])
            parity.write_json(root / 'inventory.json', after)
            parity.write_json(root / 'results/1/comparison.json', {'status': 'no_difference_in_capture',
                'item': before['items'][0]})
            result = parity.corpus_report(root / 'inventory.json', root / 'results')
            self.assertEqual(result['items'][0]['status'], 'stale_report')

    def test_every_directory_retained_and_reports_bound_to_item(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            corpus = root / 'corpus'
            for name, data in [('1', {'title': 'Video', 'type': 'video', 'file': 'a.mp4'}),
                               ('2', {'title': 'Effect', 'type': 'effect'}), ('3', None)]:
                (corpus / name).mkdir(parents=True)
                if data is not None:
                    parity.write_json(corpus / name / 'project.json', data)
            listing = parity.inventory(corpus)
            self.assertEqual(len(listing['items']), 3)
            self.assertEqual(listing['counts']['non_wallpaper'], 1)
            inv = root / 'inventory.json'
            parity.write_json(inv, listing)
            parity.write_json(root / 'results/1/comparison.json', {
                'status': 'no_difference_in_capture', 'item': {'id': 'wrong', 'project_sha256': 'wrong'}})
            result = parity.corpus_report(inv, root / 'results')
            self.assertEqual(result['items'][0]['status'], 'invalid_report')
            self.assertEqual(result['items'][2]['status'], 'missing_metadata')


if __name__ == '__main__':
    unittest.main()
