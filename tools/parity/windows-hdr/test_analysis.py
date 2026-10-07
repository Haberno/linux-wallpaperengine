#!/usr/bin/env python3
"""CPU-only generated transport data: not a Windows or MediaEngine execution."""
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('hdr_windows_analyzer', ROOT / 'analyze.py')
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)
REFERENCE = ROOT / 'reference.json'
from build_manifest import digest, source_hashes, validate
from prepare_fixtures import raw_frame


class AnalyzerTests(unittest.TestCase):
    def test_all_binary16_values_from_actual_c_function(self):
        source = (ROOT / 'mediaengine_readback.c').read_text()
        function = source[source.index('static float half_float('):source.index('static void sample(')]
        harness = '#include <stdio.h>\n' + function + '\nint main(void) { for(unsigned h=0; h<65536; ++h) { float f=half_float(h); fwrite(&f,4,1,stdout); } }\n'
        with tempfile.TemporaryDirectory() as temp:
            src, exe = Path(temp) / 'half.c', Path(temp) / 'half'
            src.write_text(harness)
            subprocess.run(['cc', '-std=c11', '-O2', str(src), '-o', str(exe)], check=True)
            data = subprocess.check_output([str(exe)])
        self.assertEqual(len(data), 65536 * 4)
        for h in range(65536):
            expected = struct.unpack('<e', struct.pack('<H', h))[0]
            actual = struct.unpack_from('<f', data, 4*h)[0]
            if math.isnan(expected):
                self.assertTrue(math.isnan(actual), h)
            else:
                self.assertEqual(actual, expected, h)
                self.assertEqual(math.copysign(1, actual), math.copysign(1, expected), h)

    def test_local_build_manifest_rejects_source_drift(self):
        data = {'schema': 1, 'sources': source_hashes(),
                'executable': {'name': 'mediaengine_readback.exe', 'sha256': '1'*64}}
        self.assertEqual(validate(data), data)
        data['sources']['mediaengine_readback.c'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'source revision'):
            validate(data)

    def test_calibrated_raw_planes_match_retained_fixture(self):
        codes = [band['y10'] for band in json.loads(REFERENCE.read_text())['bands']]
        data = b''.join(raw_frame(frame, codes) for frame in range(40))
        self.assertEqual(hashlib.sha256(data).hexdigest(), 'c0c2734a9feb77d8acf8b1ede4a9c4e5fc59ac63d03a110182ea2689eb1a8b56')

    def test_generated_capture_and_rejection_gates(self):
        identities = {'schema': 1, 'reference_sha256': digest(REFERENCE),
                      'generator_sha256': digest(ROOT / 'prepare_fixtures.py'),
                      'clips': {kind: {'sha256': hashlib.sha256(kind.encode()).hexdigest(), 'exact_decode': True}
                                for kind in ('sdr10', 'pq10', 'hlg10')}}
        build = {'schema': 1, 'sources': source_hashes(),
                 'executable': {'name': 'mediaengine_readback.exe', 'sha256': '1'*64}}
        source = json.loads(REFERENCE.read_text())
        with tempfile.TemporaryDirectory() as temp:
            run = Path(temp); capture = run / 'capture'; capture.mkdir()
            manifest = run / 'fixture-manifest.json'; manifest.write_text(json.dumps(identities))
            build_path = run / 'build-manifest.json'; build_path.write_text(json.dumps(build))
            hashes = {'clip': identities['clips']['pq10']['sha256'], 'probe': build['executable']['sha256'],
                      'build_manifest': digest(build_path), 'fixture_manifest': digest(manifest)}
            wrapper = {'fixture_kind': 'pq10', 'windows_runtime_test': True, 'exited': True, 'timed_out': False, 'failure': None,
                       'exit_code': 0, 'inputs_unchanged': True, 'before': hashes.copy(), 'after': hashes.copy()}
            wrapper_path = run / 'run.json'; wrapper_path.write_text(json.dumps(wrapper))
            meta = [{'kind': 'actual_destination', 'format': 10, 'width': 320, 'height': 180, 'samples': 1}]
            rows = []
            raw = bytearray()
            for frame in range(20):
                pixels = bytearray(struct.pack('<4e', 0, 0, 0, 1) * (320*180))
                meta.append({'kind': 'frame', 'index': frame, 'pts_100ns': frame * 1_000_000})
                targets = [('band', i, 16+32*i, 80, band['pq_eotf_nits']/80) for i, band in enumerate(source['bands'])]
                targets += [('marker', i, 40+80*i, 170, 125 if i == frame//5 else 0) for i in range(4)]
                targets += [('checker', i, 4+8*i, 4, 125 if i%2 else 0) for i in range(4)]
                for kind, index, x, y, value in targets:
                    pixel = struct.pack('<4e', value, value, value, 1)
                    pixels[(y*320+x)*8:(y*320+x+1)*8] = pixel
                    row = [frame, frame*1_000_000, kind, index, x, y]
                    for bit, val in zip(struct.unpack('<4H', pixel), struct.unpack('<4e', pixel)):
                        row.extend([f'{bit:04x}', f'{val:.9g}'])
                    rows.append(row)
                raw += pixels
            meta.append({'kind': 'result', 'capture_complete': True, 'frames': 20,
                         'engine_shutdown_hr': '00000000', 'mf_shutdown_hr': '00000000'})
            meta_path = capture / 'metadata.jsonl'
            meta_path.write_text(''.join(json.dumps(x)+'\n' for x in meta))
            raw_path = capture / 'frames.rgba16f'; raw_path.write_bytes(raw)
            with (capture / 'samples.csv').open('w', newline='') as output:
                writer = csv.writer(output)
                writer.writerow('frame,pts_100ns,kind,index,x,y,r_half,r,g_half,g,b_half,b,a_half,a'.split(','))
                writer.writerows(rows)
            accepted = ANALYZER.analyze(run, manifest, 'pq10')
            self.assertTrue(accepted['capture_integrity_pass'])
            self.assertFalse(accepted['windows_policy_acceptance'])
            with self.assertRaisesRegex(AssertionError, 'kind/hash'):
                ANALYZER.analyze(run, manifest, 'hlg10')
            wrapper['before']['probe'] = wrapper['after']['probe'] = '0'*64
            wrapper_path.write_text(json.dumps(wrapper))
            with self.assertRaisesRegex(AssertionError, 'probe revision'):
                ANALYZER.analyze(run, manifest, 'pq10')
            wrapper['before']['probe'] = wrapper['after']['probe'] = hashes['probe']
            wrapper_path.write_text(json.dumps(wrapper))
            raw_path.write_bytes(raw[:-1])
            with self.assertRaisesRegex(AssertionError, 'truncated'):
                ANALYZER.analyze(run, manifest, 'pq10')
            raw_path.write_bytes(raw)
            meta[2]['pts_100ns'] = 0
            meta_path.write_text(''.join(json.dumps(x)+'\n' for x in meta))
            with self.assertRaisesRegex(AssertionError, 'ordering'):
                ANALYZER.analyze(run, manifest, 'pq10')


if __name__ == '__main__':
    unittest.main()
