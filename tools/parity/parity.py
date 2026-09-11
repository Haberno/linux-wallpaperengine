#!/usr/bin/env python3
"""Inventory every installed item and measure paired temporal RGB captures.

No renderer is launched by this tool. Capture via capture.py or import recordings
from real Windows. Difference measurements alone never establish a renderer bug.
"""
import argparse
from collections import Counter
from contextlib import contextmanager
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import subprocess
import sys

import numpy as np

GATES = ('assets', 'scene_time', 'random_state', 'input', 'audio', 'properties',
         'camera', 'camera_sequence', 'quality', 'framing', 'color_pipeline', 'video_seek',
         'reference_environment', 'capture_integrity')
PATTERNS = {
    'particles': r'particle',
    'control_points': r'controlpoint|control.point',
    'rope_trail': r'rope|trail',
    'scripts': r'"script"|export function|\.js\b',
    'randomness': r'random|particle',
    'clock': r'frametime|runtime|Date\(|Date\.|timeofday|daytime',
    'pointer': r'cursor|mouse|parallax',
    'audio': r'audio|sound|spectrum',
    'media': r'media|spotify',
    'video': r'\.mp4\b|\.webm\b|\.avi\b|videotexture',
    'camera_path': r'camerapath|camerashake',
    'camera_sequence': r'camerapath|camerashot|playlist|shuffle',
    'shader_clock': r'g_Time|g_Frametime|u_time',
    'shader_random': r'g_Random|randomseed|hashnoise|randomframe',
    'hdr': r'"hdr"\s*:\s*true',
}


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def asset_digest(records):
    identity = [dict(path=record['path'], bytes=record['bytes'], sha256=record['sha256'])
                for record in sorted(records, key=lambda record: record['path'])]
    return hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def inventory(corpus, hash_assets=False):
    # Reuse the corpus validator's existing package reader; do not invent another
    # package parser. Full asset inspection belongs to repkg extraction.
    validator = None
    validator_path = Path(__file__).resolve().parents[1] / 'validate-corpus.py'
    if validator_path.is_file():
        spec = importlib.util.spec_from_file_location('corpus_validator', validator_path)
        validator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(validator)
    records = []
    for item in sorted(Path(corpus).resolve().iterdir()):
        if not item.is_dir():
            continue
        entry = dict(id=item.name, path=str(item), title=item.name, type='unknown',
                     status='pending_capture', requirements={}, inspection_limits=[])
        records.append(entry)
        project = item / 'project.json'
        if not project.is_file():
            entry['status'] = 'missing_metadata'
            continue
        try:
            metadata = json.loads(project.read_text(encoding='utf-8-sig'))
            if not isinstance(metadata, dict):
                raise ValueError('project metadata is not an object')
        except (OSError, ValueError) as error:
            entry.update(status='unreadable_metadata', error=str(error))
            continue
        kind = str(metadata.get('type', 'unknown')).lower()
        entry.update(title=str(metadata.get('title', item.name)), type=kind,
                     project_sha256=sha256(project), file=metadata.get('file'),
                     properties=metadata.get('general', {}).get('properties', {})
                     if isinstance(metadata.get('general'), dict) else {})
        entry['files'] = [dict(path=str(p.relative_to(item)), bytes=p.stat().st_size,
                               mtime_ns=p.stat().st_mtime_ns)
                          for p in sorted(item.rglob('*')) if p.is_file()]
        if hash_assets:
            for record in entry['files']:
                record['sha256'] = sha256(item / record['path'])
            entry['asset_digest'] = asset_digest(entry['files'])
        entry['inspection_limits'].append('File sizes/mtimes are inventory only; not content identity proof.')
        if kind not in ('scene', 'video', 'web'):
            entry['status'] = 'non_wallpaper' if kind != 'unknown' else 'unknown_type'
            continue
        content = json.dumps(metadata)
        if kind == 'scene':
            scene_file = str(metadata.get('file', 'scene.json'))
            loose = (item / scene_file).resolve()
            raw = None
            if not loose.is_relative_to(item):
                entry['inspection_limits'].append('Scene entry escapes item directory; not read.')
            else:
                try:
                    if loose.is_file():
                        raw = loose.read_bytes()
                    elif validator is not None:
                        for pkg in sorted(item.glob('*.pkg')):
                            raw = validator.pkg_entry(pkg, scene_file)
                            if raw is not None:
                                break
                except OSError as error:
                    entry['inspection_limits'].append(str(error))
            if raw is not None:
                content += '\n' + raw.decode('utf-8', errors='replace')
                entry['scene_sha256'] = hashlib.sha256(raw).hexdigest()
            else:
                entry['inspection_limits'].append('Scene entry unreadable or missing.')
            entry['inspection_limits'].append('Only project and root scene scanned. Referenced packed particles, materials, shaders and scripts require repkg inspection; missing flags do not prove absence.')
        elif kind == 'web':
            entry['requirements']['browser_environment'] = ['Web runtime, network, storage and browser clock require a separate controlled scenario.']
        elif kind == 'video':
            entry['requirements']['video'] = ['Standalone video: matched decoder output, seek position and presentation timestamps required.']
        for name, pattern in PATTERNS.items():
            matches = list(dict.fromkeys(m.group(0) for m in re.finditer(pattern, content, re.I)))
            if matches:
                entry['requirements'][name] = matches[:8]
        entry['inspection_limits'].append('Signals are conservative text matches, not complete dependency or runtime analysis.')
    return dict(schema=1, corpus=str(Path(corpus).resolve()), items=records,
                counts=dict(Counter(x['status'] for x in records)),
                types=dict(Counter(x['type'] for x in records)))


def probe(path):
    result = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
        '-show_streams', '-show_frames', '-show_entries',
        'stream=width,height,pix_fmt,codec_name,color_range,color_space,color_transfer,color_primaries:frame=best_effort_timestamp_time',
        '-of', 'json', str(path)], capture_output=True, text=True, check=True)
    data = json.loads(result.stdout)
    if len(data.get('streams', [])) != 1:
        raise ValueError('Exactly one selected video stream is required')
    pts = [float(f['best_effort_timestamp_time']) for f in data.get('frames', [])]
    if len(pts) < 2 or any(not math.isfinite(t) for t in pts) or any(b <= a for a, b in zip(pts, pts[1:])):
        raise ValueError('Video needs at least two frames with strictly increasing timestamps')
    return dict(data['streams'][0], timestamps=pts)


@contextmanager
def decoder(path, width, height):
    # Passthrough preserves decoded frames; no fps filter, scaling, tone mapping,
    # color normalization or frame interpolation. Conversion to RGB8 is disclosed.
    process = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', str(path), '-map', '0:v:0',
        '-fps_mode', 'passthrough', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE)
    def frames():
        count = width * height * 3
        while True:
            raw = process.stdout.read(count)
            if not raw:
                break
            if len(raw) != count:
                raise ValueError('Truncated decoded video frame')
            yield np.frombuffer(raw, np.uint8).reshape(height, width, 3).astype(np.float32)
        if process.wait(timeout=10):
            raise ValueError('Video decoding failed')
    try:
        yield frames()
    finally:
        process.stdout.close()
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def evidence_blockers(evidence):
    evidence = evidence or {}
    if not isinstance(evidence, dict):
        raise ValueError('Evidence must be a JSON object')
    # A bare true/false is an assertion, not a record of how a control was verified.
    return [f'{key}: missing verified evidence' for key in GATES
            if not isinstance(evidence.get(key), dict)
            or evidence[key].get('verified') is not True
            or not isinstance(evidence[key].get('evidence'), str)
            or not evidence[key]['evidence'].strip()]


def cadence(timestamps):
    intervals = np.diff(timestamps)
    median = float(np.median(intervals))
    return dict(median_interval_seconds=median, min_interval_seconds=float(intervals.min()),
                max_interval_seconds=float(intervals.max()),
                gaps_over_1_5_median=int(np.count_nonzero(intervals > median * 1.5)),
                scope='Capture sample timestamps; not simulation steps or renderer presentation times.')


def compare(reference, candidate, offset=0.0, tolerance=2.0, evidence=None, out=None, controls=None):
    if not math.isfinite(offset) or not math.isfinite(tolerance) or tolerance < 0:
        raise ValueError('Offset and tolerance must be finite; tolerance must be nonnegative')
    ref, cand = probe(reference), probe(candidate)
    size = ref['width'], ref['height']
    if size != (cand['width'], cand['height']):
        raise ValueError('Video geometry differs; no implicit resize or crop is permitted')
    # Positive offset means candidate content appears later in its recording.
    rt = np.array(ref['timestamps']) - ref['timestamps'][0]
    ct = np.array(cand['timestamps']) - cand['timestamps'][0] - offset
    max_skew = min(float(np.median(np.diff(rt))), float(np.median(np.diff(ct)))) * .51
    blockers = evidence_blockers(evidence)
    from determinism import preflight
    control_report = preflight(controls) if controls is not None else None
    if control_report is None:
        blockers.append('Determinism: per-wallpaper input preflight missing')
    elif control_report['blockers']:
        blockers.extend('Determinism: ' + blocker for blocker in control_report['blockers'])
    else:
        blockers.append('Determinism: exact same-engine repeated captures not yet verified')
    rgb_formats = ('bgr0', 'bgra', 'rgb24', 'bgr24', 'gbrp', 'rgba', 'rgb0', '0rgb', '0bgr')
    for role, info in [('reference', ref), ('candidate', cand)]:
        if info.get('pix_fmt') not in rgb_formats:
            blockers.append(f'{role}: decoded {info.get("pix_fmt")} to RGB8; conversion/precision needs review')
        if info.get('codec_name') not in ('ffv1', 'rawvideo', 'png', 'huffyuv', 'qtrle'):
            blockers.append(f'{role}: lossless recording codec not established')
        if cadence(info['timestamps'])['gaps_over_1_5_median']:
            blockers.append(f'{role}: recording timestamp gaps exceed 1.5 median intervals')
    for field in ('color_range', 'color_space', 'color_transfer', 'color_primaries'):
        left, right = ref.get(field), cand.get(field)
        if left in (None, 'unknown', 'unspecified') or right in (None, 'unknown', 'unspecified'):
            blockers.append(f'{field}: metadata unknown; equal RGB numbers do not establish color encoding')
        elif left != right:
            blockers.append(f'{field}: reference/candidate metadata differ ({left} vs {right})')
    per_frame, skews = [], []
    sums = [np.zeros((size[1], size[0], 3), np.float64) for _ in range(4)]
    previous = None
    temporal = []
    motion = [[], []]
    unchanged = [[], []]
    worst = -1
    with decoder(reference, *size) as rf, decoder(candidate, *size) as cf:
        ri = ci = 0
        a, b = next(rf), next(cf)
        while ri < len(rt) and ci < len(ct):
            skew = ct[ci] - rt[ri]
            if abs(skew) <= max_skew:
                delta = np.abs(a - b)
                mae = float(delta.mean())
                mse = float(np.square(delta).mean())
                per_frame.append(dict(reference_frame=ri, candidate_frame=ci,
                    reference_seconds=float(rt[ri]), candidate_seconds=float(ct[ci]),
                    mae_rgb8=mae, mse_rgb8=mse,
                    changed_pixel_fraction=float(np.any(delta > tolerance, axis=2).mean())))
                skews.append(float(skew))
                if previous is not None and previous[0:2] == (ri - 1, ci - 1):
                    temporal.append(float(np.abs((a - previous[2]) - (b - previous[3])).mean()))
                    for role, difference in enumerate((a - previous[2], b - previous[3])):
                        motion[role].append(float(np.abs(difference).mean()))
                        unchanged[role].append(not bool(np.any(difference)))
                previous = (ri, ci, a, b)
                for accumulator, frame in zip(sums, (a, b, a * a, b * b)):
                    accumulator += frame
                if out and mae > worst:
                    from PIL import Image
                    Path(out).mkdir(parents=True, exist_ok=True)
                    for name, frame in [('reference', a), ('candidate', b), ('absolute-difference', delta)]:
                        Image.fromarray(frame.astype(np.uint8)).save(Path(out) / f'worst-{name}.png')
                    worst = mae
                ri += 1
                ci += 1
                a, b = next(rf, None), next(cf, None)
            elif skew < 0:
                ci += 1
                b = next(cf, None)
            else:
                ri += 1
                a = next(rf, None)
            if (a is None and ri < len(rt)) or (b is None and ci < len(ct)):
                raise ValueError('Decoded frame count differs from ffprobe timestamps')
        # Drain to verify decode success and complete frame counts, including tails.
        for remaining, frames, index in ((len(rt), rf, ri), (len(ct), cf, ci)):
            consumed = index + (1 if index < remaining else 0) + sum(1 for _ in frames)
            if consumed != remaining:
                raise ValueError('Decoded frame count differs from ffprobe timestamps')
    n = len(per_frame)
    if n < 2:
        raise ValueError('Fewer than two temporally overlapping frames')
    mae = float(np.mean([f['mae_rgb8'] for f in per_frame]))
    mse = float(np.mean([f['mse_rgb8'] for f in per_frame]))
    means = [sums[i] / n for i in (0, 1)]
    deviations = [np.sqrt(np.maximum(sums[i + 2] / n - means[i] ** 2, 0)) for i in (0, 1)]
    if n < len(rt) or n < len(ct):
        blockers.append('Unmatched recording frames: review startup, offset and duration coverage')
    skew_limit = max(.001, max_skew / .51 * .1)
    if max(abs(s) for s in skews) > skew_limit:
        blockers.append('Paired capture timestamp skew exceeds 10% of a frame interval (minimum 1ms); phase/drift unresolved')
    matched_times = np.array([frame['reference_seconds'] for frame in per_frame])
    drift = float(np.polyfit(matched_times, skews, 1)[0])
    status = 'exploratory' if blockers else ('no_difference_in_capture' if mse == 0 else 'comparable_difference')
    report = dict(schema=1, status=status, blockers=blockers,
        scope='Measured RGB8 footage only; never a complete parity or renderer-defect verdict.',
        reference=dict(path=str(Path(reference).resolve()), sha256=sha256(reference), **ref),
        candidate=dict(path=str(Path(candidate).resolve()), sha256=sha256(candidate), **cand),
        offset_seconds=offset, matching_tolerance_seconds=max_skew,
        matched_frames=n, unmatched_reference_frames=len(rt)-n, unmatched_candidate_frames=len(ct)-n,
        max_timestamp_skew_seconds=max(abs(s) for s in skews), pixel_tolerance_rgb8=tolerance,
        cadence=dict(reference=cadence(rt), candidate=cadence(ct)),
        alignment=dict(first_skew_seconds=skews[0], last_skew_seconds=skews[-1],
                       sample_clock_drift_ppm=drift * 1e6, skew_limit_seconds=skew_limit,
                       scope='Least-squares drift of paired capture sample timestamps, not simulation clock drift. No time warping or camera-shot reordering.'),
        metrics=dict(mae_rgb8=mae, rmse_rgb8=math.sqrt(mse),
            psnr_db=10 * math.log10(255 ** 2 / mse) if mse else None,
            psnr_infinite=mse == 0,
            changed_pixel_fraction=float(np.mean([f['changed_pixel_fraction'] for f in per_frame])),
            temporal_delta_mae_rgb8=float(np.mean(temporal)) if temporal else None,
            temporal_delta_pairs=len(temporal),
            reference_motion_mae_rgb8=float(np.mean(motion[0])) if motion[0] else None,
            candidate_motion_mae_rgb8=float(np.mean(motion[1])) if motion[1] else None,
            reference_identical_adjacent_fraction=float(np.mean(unchanged[0])) if unchanged[0] else None,
            candidate_identical_adjacent_fraction=float(np.mean(unchanged[1])) if unchanged[1] else None,
            temporal_mean_image_mae_rgb8=float(np.abs(means[0]-means[1]).mean()),
            temporal_std_image_mae_rgb8=float(np.abs(deviations[0]-deviations[1]).mean())),
        evidence=evidence or {}, frames=per_frame)
    report['determinism'] = control_report
    report['controls'] = controls
    return report


def compare_with_repeats(reference, candidate, reference_repeat=None, candidate_repeat=None, **kwargs):
    verification = kwargs.pop('repeat_verification', None)
    report = compare(reference, candidate, **kwargs)
    if bool(reference_repeat) != bool(candidate_repeat):
        raise ValueError('Provide both reference and candidate repeat recordings')
    if reference_repeat:
        report['repeat_runs'] = {'scope': 'Independent-run variability baseline, not seed synchronization or statistical equivalence.'}
        for role, first, repeat in (('reference', reference, reference_repeat),
                                    ('candidate', candidate, candidate_repeat)):
            baseline = compare(first, repeat, tolerance=kwargs.get('tolerance', 2.0))
            report['repeat_runs'][role] = {key: baseline[key] for key in
                ('reference', 'candidate', 'metrics', 'matched_frames', 'blockers')}
            if baseline['metrics']['rmse_rgb8'] > 0:
                report['blockers'].append(f'{role} repeat has different pixels/motion; deterministic state or capture repeatability is contradicted')
            for blocker in baseline['blockers']:
                if 'missing verified evidence' not in blocker and not blocker.startswith('Determinism:'):
                    report['blockers'].append(f'{role} repeat: {blocker}')
        maximum = max(report['repeat_runs'][role]['metrics']['rmse_rgb8'] for role in ('reference', 'candidate'))
        report['repeat_runs']['cross_rmse_minus_max_repeat_rmse_rgb8'] = report['metrics']['rmse_rgb8'] - maximum
        report['repeat_runs']['scope'] += ' Cross-minus-baseline is descriptive only, not a significance test or deterministic acceptance fallback.'
        stable = all(report['repeat_runs'][role]['metrics']['rmse_rgb8'] == 0 and
                     not any(blocker.startswith(tuple(f'{role} repeat: {prefix}' for prefix in
                         ('reference:', 'candidate:', 'Unmatched', 'Paired capture'))) for blocker in report['blockers'])
                     for role in ('reference', 'candidate'))
        from determinism import controls_digest
        bound_verification = (isinstance(verification, dict) and
            verification.get('status') == 'repeatable_for_tested_scenario_window' and
            verification.get('determinism_verified') is True and not verification.get('blockers') and
            verification.get('controls_sha256') == controls_digest(kwargs.get('controls')))
        if bound_verification:
            for key, path in (('reference_a', reference), ('candidate_a', candidate),
                              ('reference_b', reference_repeat), ('candidate_b', candidate_repeat)):
                if verification.get('videos', {}).get(key, {}).get('sha256') != sha256(path):
                    bound_verification = False
                    report['blockers'].append('Determinism: verified repeat artifact hashes do not match these recordings')
        if stable and bound_verification and report['determinism'] and not report['determinism']['blockers']:
            report['blockers'].remove('Determinism: exact same-engine repeated captures not yet verified')
            report['determinism'] = verification
        report['status'] = 'exploratory' if report['blockers'] else (
            'no_difference_in_capture' if report['metrics']['rmse_rgb8'] == 0 else 'comparable_difference')
    return report


def corpus_report(inventory_path, results):
    listing = json.loads(Path(inventory_path).read_text())
    items = []
    for item in listing['items']:
        record = {key: item[key] for key in ('id', 'title', 'type', 'status')}
        report_path = Path(results) / item['id'] / 'comparison.json'
        if report_path.is_file():
            try:
                report = json.loads(report_path.read_text())
                identity = report.get('item', {})
                if (identity.get('id') != item['id'] or
                        identity.get('project_sha256') != item.get('project_sha256')):
                    raise ValueError('Report item identity does not match this inventory')
                if report['status'] not in ('exploratory', 'no_difference_in_capture',
                                            'comparable_difference', 'capture_failed', 'determinism_blocked', 'unsupported'):
                    raise ValueError('Unknown comparison status')
                record.update(status=report['status'], metrics=report.get('metrics'),
                              blockers=report.get('blockers', []), report=str(report_path.resolve()))
                if item.get('asset_digest') and identity.get('asset_digest') != item['asset_digest']:
                    record.update(status='stale_report', blockers=['Wallpaper asset content identity changed or is missing in the report'])
                elif not item.get('asset_digest') and record['status'] in ('no_difference_in_capture', 'comparable_difference'):
                    record.update(status='unverified_asset_identity', blockers=['Inventory needs --hash-assets before attaching a comparable result'])
            except (OSError, ValueError, KeyError) as error:
                record.update(status='invalid_report', error=str(error))
        items.append(record)
    return dict(schema=1, items=items, counts=dict(Counter(x['status'] for x in items)),
                scope='Missing, failed and unsupported items remain visible; no aggregate parity percentage.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    inv = commands.add_parser('inventory')
    inv.add_argument('corpus', type=Path)
    inv.add_argument('--out', type=Path, required=True)
    inv.add_argument('--hash-assets', action='store_true', help='Hash full wallpaper contents for stale-report detection')
    cmp = commands.add_parser('compare')
    cmp.add_argument('reference', type=Path)
    cmp.add_argument('candidate', type=Path)
    cmp.add_argument('--offset', type=float, default=0.0)
    cmp.add_argument('--tolerance', type=float, default=2.0)
    cmp.add_argument('--evidence', type=Path)
    cmp.add_argument('--item', type=Path, help='Bind report to this original workshop project directory')
    cmp.add_argument('--reference-repeat', type=Path)
    cmp.add_argument('--candidate-repeat', type=Path)
    cmp.add_argument('--controls', type=Path, help='Reviewed deterministic-input manifest')
    cmp.add_argument('--repeat-verification', type=Path, help='Artifact-bound verify-repeats report')
    cmp.add_argument('--out', type=Path, required=True)
    summary = commands.add_parser('report')
    summary.add_argument('inventory', type=Path)
    summary.add_argument('results', type=Path)
    summary.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'inventory':
        result = inventory(args.corpus, args.hash_assets)
    elif args.command == 'compare':
        result = compare_with_repeats(args.reference, args.candidate,
                         args.reference_repeat, args.candidate_repeat,
                         offset=args.offset, tolerance=args.tolerance,
                         evidence=json.loads(args.evidence.read_text()) if args.evidence else None,
                         controls=json.loads(args.controls.read_text()) if args.controls else None,
                         repeat_verification=json.loads(args.repeat_verification.read_text()) if args.repeat_verification else None,
                         out=args.out.parent)
        if args.item:
            files = [dict(path=str(path.relative_to(args.item)), bytes=path.stat().st_size, sha256=sha256(path))
                     for path in sorted(args.item.rglob('*')) if path.is_file()]
            result['item'] = dict(id=args.item.resolve().name,
                                 project_sha256=sha256(args.item / 'project.json'), asset_digest=asset_digest(files))
            if args.controls and result['controls'].get('item') != result['item']:
                result['blockers'].append('Determinism: control manifest does not match the current item content identity')
                result['status'] = 'exploratory'
        elif args.controls:
            parser.error('--controls requires --item to bind the evidence to current wallpaper content')
    else:
        result = corpus_report(args.inventory, args.results)
    write_json(args.out, result)
    print(json.dumps({key: result[key] for key in ('counts', 'types', 'status', 'metrics', 'matched_frames') if key in result}, indent=2))


if __name__ == '__main__':
    main()
