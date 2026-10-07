#!/usr/bin/env python3
"""Validate capture integrity and report reference residuals, never fit a tone curve."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import struct

from build_manifest import digest, validate


def analyze(run, manifest, kind):
    package = Path(__file__).resolve().parent
    identities = json.loads(manifest.read_text(encoding='utf-8-sig'))
    build_path = run / 'build-manifest.json'
    build = validate(json.loads(build_path.read_text(encoding='utf-8-sig')))
    reference_path = package / 'reference.json'
    assert identities['schema'] == 1
    assert identities['reference_sha256'] == digest(reference_path), 'reference revision mismatch'
    assert identities['generator_sha256'] == digest(package / 'prepare_fixtures.py'), 'fixture generator revision mismatch'
    assert identities['clips'][kind]['exact_decode'], 'fixture decode not validated'
    capture = run / 'capture'
    wrapper = json.loads((run / 'run.json').read_text(encoding='utf-8-sig'))
    metadata = [json.loads(line) for line in (capture / 'metadata.jsonl').read_text().splitlines()]
    results = [x for x in metadata if x['kind'] == 'result']
    assert len(results) == 1 and results[0]['capture_complete'], 'incomplete producer'
    assert wrapper['exited'] and not wrapper['timed_out'] and not wrapper['failure'], 'wrapper guard failed'
    assert wrapper['exit_code'] == 0 and wrapper['inputs_unchanged'], 'exit/identity guard failed'
    assert wrapper['windows_runtime_test'], 'not declared as a real Windows capture'
    assert wrapper['before']['clip'].lower() == identities['clips'][kind]['sha256'], 'fixture kind/hash mismatch'
    assert wrapper['after'] == wrapper['before'], 'input identity mismatch'
    assert wrapper['before']['probe'].lower() == build['executable']['sha256'], 'probe revision mismatch'
    assert wrapper['before']['build_manifest'].lower() == digest(build_path), 'build manifest mismatch'
    assert wrapper['before']['fixture_manifest'].lower() == digest(manifest), 'fixture manifest mismatch'
    assert wrapper['fixture_kind'] == kind, 'fixture label mismatch'
    assert results[0]['engine_shutdown_hr'] == results[0]['mf_shutdown_hr'] == '00000000', 'shutdown failed'
    destinations = [x for x in metadata if x['kind'] == 'actual_destination']
    assert len(destinations) == 1 and destinations[0]['format'] == 10, 'not an observed RGBA16F texture'
    assert (destinations[0]['width'], destinations[0]['height'], destinations[0]['samples']) == (320, 180, 1)
    frame_records = [x for x in metadata if x['kind'] == 'frame']
    assert len(frame_records) == results[0]['frames'] >= 20
    raw = (capture / 'frames.rgba16f').read_bytes()
    stride = 320 * 180 * 8
    assert len(raw) == len(frame_records) * stride, 'truncated/extra raw frame'
    previous = -1
    for frame, record in enumerate(frame_records):
        assert record['index'] == frame and record['pts_100ns'] > previous, 'frame ordering'
        previous = record['pts_100ns']
    with (capture / 'samples.csv').open(newline='') as source_csv:
        samples = list(csv.DictReader(source_csv))
    assert len(samples) == len(frame_records) * 18, 'sample row count'
    by_frame = {}
    bands = [[] for _ in range(10)]
    for row in samples:
        frame, x, y = int(row['frame']), int(row['x']), int(row['y'])
        assert 0 <= frame < len(frame_records) and 0 <= x < 320 and 0 <= y < 180
        assert int(row['pts_100ns']) == frame_records[frame]['pts_100ns']
        offset = frame * stride + (y * 320 + x) * 8
        bits = struct.unpack_from('<4H', raw, offset)
        rgba = struct.unpack_from('<4e', raw, offset)
        assert all(math.isfinite(v) for v in rgba), 'nonfinite readback'
        for c, bit, value in zip('rgba', bits, rgba):
            assert int(row[c + '_half'], 16) == bit, 'CSV/raw half mismatch'
            assert math.isclose(float(row[c]), value, rel_tol=2e-8, abs_tol=1e-10), 'CSV float conversion'
        key = (row['kind'], int(row['index']))
        records = by_frame.setdefault(frame, {})
        assert key not in records, 'duplicate sample'
        records[key] = rgba
        if row['kind'] == 'band':
            index = int(row['index'])
            assert (x, y) == (16 + 32 * index, 80)
            bands[index].append(rgba)
        elif row['kind'] == 'marker':
            assert (x, y) == (40 + 80 * int(row['index']), 170)
        elif row['kind'] == 'checker':
            assert (x, y) == (4 + 8 * int(row['index']), 4)
    phases = set()
    for frame, records in by_frame.items():
        assert set(records) == ({('band', i) for i in range(10)} |
                                {('marker', i) for i in range(4)} |
                                {('checker', i) for i in range(4)}), 'sample identities'
        marker = [records['marker', i][0] for i in range(4)]
        phase = max(range(4), key=lambda i: marker[i])
        assert marker[phase] > sum(abs(marker[i]) for i in range(4) if i != phase) + 1e-4, 'marker contrast'
        pts = frame_records[frame]['pts_100ns']
        source_frame = round(pts / 1_000_000)
        assert abs(pts - source_frame * 1_000_000) <= 5000, 'unexpected fixture PTS grid'
        assert phase == (source_frame // 5) % 4, 'marker/PTS identity mismatch'
        phases.add(phase)
        checker = [records['checker', i][0] for i in range(4)]
        assert min(checker[1::2]) > max(checker[::2]) + 1e-4, 'spatial marker orientation/contrast'
    assert phases == {0, 1, 2, 3}, 'temporal phases incomplete'
    source = json.loads(reference_path.read_text())
    summaries = []
    for index, values in enumerate(bands):
        assert len(values) == len(frame_records)
        medians = [statistics.median(x[c] for x in values) for c in range(4)]
        entry = {'index': index, 'rgba_median': medians,
                 'rgb_min': [min(x[c] for x in values) for c in range(3)],
                 'rgb_max': [max(x[c] for x in values) for c in range(3)]}
        if kind in ('pq10', 'hlg10'):
            key = 'pq_eotf_nits' if kind == 'pq10' else 'hlg_reference_eotf_nits'
            reference = source['bands'][index][key] / 80
            entry.update(raw_standard_scrgb_reference=reference,
                         observed_minus_raw_reference=medians[0] - reference,
                         reference_is_windows_policy=False)
        summaries.append(entry)
    return {'capture_integrity_pass': True, 'windows_policy_acceptance': False,
            'fixture_kind': kind, 'frames': len(frame_records), 'marker_phases': sorted(phases),
            'bands': summaries,
            'route': 'Unknown: loaded component presence and source attributes do not identify the selected processor.',
            'inputs': {str(path): digest(path) for path in
                       (run / 'run.json', capture / 'metadata.jsonl', capture / 'samples.csv',
                        capture / 'frames.rgba16f', manifest, build_path, reference_path)}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('kind', choices=('sdr10', 'pq10', 'hlg10'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = analyze(args.run, args.run / 'fixture-manifest.json', args.kind)
    except (AssertionError, OSError, KeyError, ValueError) as error:
        result = {'capture_integrity_pass': False, 'windows_policy_acceptance': False,
                  'failure': str(error)}
    with args.output.open('x') as output:
        json.dump(result, output, indent=2)
        output.write('\n')
    return 0 if result['capture_integrity_pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
