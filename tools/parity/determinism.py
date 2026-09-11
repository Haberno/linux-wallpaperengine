#!/usr/bin/env python3
"""Per-wallpaper deterministic-input preflight and private input substitutions.

Records reviewed evidence; never discovers a universal native RNG/clock control.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

from parity import sha256, write_json

SOURCES = ('shader_clock', 'shader_random', 'particle_rng', 'control_points_trails',
           'camera_sequence', 'script_clock', 'script_rng', 'input_audio_media',
           'video_playback', 'property_events', 'simulation_timestep')
RNG_SOURCES = ('shader_random', 'particle_rng', 'script_rng')
RNG_FIELDS = ('algorithm', 'distribution', 'seed', 'consumption_order', 'initial_state')


def template(item):
    return dict(schema=1, item=item, scope='original_inputs', coverage_exclusions=[],
        dependency_coverage={'verified': False, 'evidence': ''},
        cross_engine_known_inputs={'verified': False, 'evidence': ''},
        observability={'verified': False, 'evidence': '', 'probes': {}},
        sources={key: dict(state='unresolved', native={'evidence': ''}, fork={'evidence': ''})
                 for key in SOURCES}, substitutions=[],
        instructions='Resolve every source in both engines before controlled comparison. Equal FPS, seed, warmup or statistical means is insufficient. Record original behavior omitted by fixed inputs.')


def has_evidence(value):
    return isinstance(value, dict) and isinstance(value.get('evidence'), str) and bool(value['evidence'].strip())


def preflight(controls):
    blockers = []
    for key in ('dependency_coverage', 'cross_engine_known_inputs', 'observability'):
        value = controls.get(key)
        if not has_evidence(value) or value.get('verified') is not True:
            blockers.append(f'{key}: reviewed evidence missing')
    for key in SOURCES:
        source = controls.get('sources', {}).get(key, {})
        state = source.get('state')
        if state not in ('absent', 'fixed', 'matched'):
            blockers.append(f'{key}: unresolved source')
            continue
        native, fork = source.get('native', {}), source.get('fork', {})
        for role, binding in (('native', native), ('fork', fork)):
            if not has_evidence(binding):
                blockers.append(f'{key}: {role} runtime/source evidence missing')
        if state == 'fixed':
            if 'value' not in native or 'value' not in fork or native['value'] != fork['value']:
                blockers.append(f'{key}: fixed native/fork input values differ or are missing')
        elif state == 'matched':
            fields = RNG_FIELDS if key in RNG_SOURCES else ('contract',)
            if key == 'camera_sequence':
                fields = ('contract', 'ordered_sequence')
            for field in fields:
                if (field not in native or field not in fork or native[field] in ('', None, 'unknown')
                        or native[field] != fork[field]):
                    blockers.append(f'{key}: matching {field} not established')
    altered = bool(controls.get('substitutions')) or any(
        source.get('state') == 'fixed' for source in controls.get('sources', {}).values())
    if altered and (controls.get('scope') != 'altered_controls' or not controls.get('coverage_exclusions')):
        blockers.append('Fixed/substituted inputs require altered_controls scope and explicit original-behavior coverage exclusions')
    return dict(schema=1, item=controls.get('item'),
        status='blocked' if blockers else 'controls_recorded_pending_repeats',
        determinism_verified=False, blockers=blockers,
        scope=controls.get('scope'), coverage_exclusions=controls.get('coverage_exclusions', []),
        required_next='Exact same-engine repeatability in native and fork plus independently verified cross-engine known inputs. This manifest records assertions and provenance; it does not prove native controls exist.')


def apply_substitutions(root, controls):
    root = root.resolve()
    if root == Path('/tmp') or not root.is_relative_to('/tmp'):
        raise ValueError('Substitutions require a private directory below /tmp')
    item = root / 'item'
    pending = []
    seen = set()
    for change in controls.get('substitutions', []):
        relative = Path(change['path'])
        path = (item / relative).resolve()
        if (relative.is_absolute() or not path.is_relative_to(item) or path in seen or
                path.suffix not in ('.frag', '.vert', '.js', '.json')):
            raise ValueError('Use each private loose shader/script/JSON path once without traversal')
        seen.add(path)
        if not change.get('input_binding', '').strip():
            raise ValueError('Each substitution must identify the controlled input binding')
        if sha256(path) != change['sha256']:
            raise ValueError(f'Private source hash mismatch: {relative}')
        original = path.read_text()
        old, new, count = change['old'], change['new'], change['count']
        if not old or not isinstance(count, int) or count < 1 or original.count(old) != count:
            raise ValueError(f'Exact substitution count mismatch: {relative}')
        pending.append((path, original.replace(old, new), change))
    # Validate the complete plan before writing any file. Only local copied
    # loose files are supported; package extraction/precedence needs review.
    records = []
    for path, replacement, change in pending:
        path.write_text(replacement)
        records.append(dict(path=change['path'], before_sha256=change['sha256'],
            after_sha256=sha256(path), input_binding=change['input_binding'],
            old=change['old'], new=change['new'], count=change['count']))
    return records


def controls_digest(controls):
    return hashlib.sha256(json.dumps(controls, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def check_observability(video, probes):
    import numpy as np
    from parity import decoder, probe
    info = probe(video)
    if not probes:
        raise ValueError('observability: at least one authored nonblack sentinel is required per renderer')
    for expected in probes:
        x, y, width, height = expected['region']
        rgb, tolerance = expected['rgb'], expected.get('tolerance', 0)
        if (not all(isinstance(value, (int, float)) and math.isfinite(value) and 0 <= value <= 255 for value in rgb)
                or not isinstance(tolerance, (int, float)) or not math.isfinite(tolerance) or tolerance < 0
                or not rgb or max(rgb) <= tolerance):
            raise ValueError('observability: finite RGB/tolerance must exclude an entirely black result')
        if (min(x, y) < 0 or min(width, height) < 1 or x + width > info['width'] or y + height > info['height']
                or len(expected['rgb']) != 3 or max(expected['rgb']) <= 0):
            raise ValueError('observability: invalid/black sentinel or region outside captured frame')
    with decoder(video, info['width'], info['height']) as frames:
        for index, frame in enumerate(frames):
            for expected in probes:
                x, y, width, height = expected['region']
                error = np.abs(frame[y:y + height, x:x + width] - np.array(expected['rgb']))
                if float(error.max()) > expected.get('tolerance', 0):
                    raise ValueError(f'observability: authored sentinel absent at frame {index}')


def verify_repeats(controls, first, second):
    from parity import asset_digest, compare
    report = preflight(controls)
    report.update(controls_sha256=controls_digest(controls), videos={}, repeat_metrics={})
    blockers = report['blockers']
    runs = [Path(first).resolve(), Path(second).resolve()]
    if runs[0] == runs[1]:
        blockers.append('Independent repeats require distinct run directories')
    try:
        scenarios = [json.loads((run / 'scenario.json').read_text()) for run in runs]
        for run, scenario in zip(runs, scenarios):
            if sha256(run / 'scenario.json') != controls.get('scenario_sha256'):
                blockers.append('Scenario artifact does not match the reviewed control manifest')
            if any(scenario['item'].get(key) != value for key, value in controls['item'].items()):
                blockers.append('Run item identity differs from reviewed controls')
        if (runs[0] / 'binaries.json').read_bytes() != (runs[1] / 'binaries.json').read_bytes():
            blockers.append('Native/fork/base-asset binary identity changed between repeated runs')
        report['scenario_sha256'] = sha256(runs[0] / 'scenario.json')
        report['binaries_sha256'] = sha256(runs[0] / 'binaries.json')
        for role in ('reference', 'candidate'):
            metadata = []
            for label, run, scenario in zip(('a', 'b'), runs, scenarios):
                video = run / f'{role}.mkv'
                info = json.loads((run / f'{role}.json').read_text())
                metadata.append(info)
                if info.get('status') != 'captured' or info.get('video', {}).get('sha256') != sha256(video):
                    blockers.append(f'{role} {label}: capture status/video artifact binding invalid')
                if not info.get('mapped_artifacts'):
                    blockers.append(f'{role} {label}: loaded renderer/driver artifact identity missing')
                if (info.get('scenario_sha256') != sha256(run / 'scenario.json') or
                        not scenario.get('config_sha256') or info.get('config_sha256') != scenario['config_sha256'] or
                        info.get('input_asset_digest') != asset_digest(scenario.get('captured_files', [])) or
                        info.get('inputs_unchanged_after_capture') is not True):
                    blockers.append(f'{role} {label}: runtime input/config/scenario boundary binding invalid')
                report['videos'][f'{role}_{label}'] = dict(path=str(video), sha256=sha256(video))
                try:
                    check_observability(video, controls.get('observability', {}).get('probes', {}).get(role, []))
                except (ValueError, KeyError, TypeError) as error:
                    blockers.append(f'{role} {label}: {error}')
            if metadata[0].get('mapped_artifacts') != metadata[1].get('mapped_artifacts'):
                blockers.append(f'{role}: loaded renderer/driver artifact identity changed')
            for field in ('launch_monotonic_ns', 'acquisition_launch_monotonic_ns'):
                stamps = [entry.get(field) for entry in metadata]
                if any(not isinstance(stamp, int) or stamp <= 0 for stamp in stamps) or stamps[0] == stamps[1]:
                    blockers.append(f'{role}: independent {field} identities missing or reused')
            baseline = compare(runs[0] / f'{role}.mkv', runs[1] / f'{role}.mkv')
            report['repeat_metrics'][role] = baseline['metrics']
            if baseline['metrics']['rmse_rgb8'] != 0:
                blockers.append(f'{role}: exact captured behavior differs between runs')
            for blocker in baseline['blockers']:
                if blocker.startswith(('reference:', 'candidate:', 'Unmatched', 'Paired capture')):
                    blockers.append(f'{role} repeat integrity: {blocker}')
    except (OSError, ValueError, KeyError, TypeError) as error:
        blockers.append(f'Run artifacts unavailable/invalid: {error}')
    report.update(status='blocked' if blockers else 'repeatable_for_tested_scenario_window',
                  determinism_verified=not bool(blockers),
                  scope='Observed repeatability for these exact inputs, artifacts and capture windows only; not universal determinism, original excluded behavior, or cross-engine output parity.')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    make = commands.add_parser('template')
    make.add_argument('--item', type=Path, required=True)
    make.add_argument('--out', type=Path, required=True)
    check = commands.add_parser('preflight')
    check.add_argument('controls', type=Path)
    check.add_argument('--out', type=Path, required=True)
    verify = commands.add_parser('verify-repeats')
    verify.add_argument('controls', type=Path)
    verify.add_argument('--first', type=Path, required=True)
    verify.add_argument('--second', type=Path, required=True)
    verify.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'template':
        from capture import source_manifest
        from parity import asset_digest
        result = template(dict(id=args.item.resolve().name, project_sha256=sha256(args.item / 'project.json'),
                               asset_digest=asset_digest(source_manifest(args.item))))
    elif args.command == 'verify-repeats':
        result = verify_repeats(json.loads(args.controls.read_text()), args.first, args.second)
    else:
        result = preflight(json.loads(args.controls.read_text()))
    write_json(args.out, result)
    print(json.dumps(result, indent=2))
    return 1 if result.get('status') == 'blocked' else 0


if __name__ == '__main__':
    raise SystemExit(main())
