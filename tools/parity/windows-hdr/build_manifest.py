#!/usr/bin/env python3
"""Bind a local build to its inputs. This is provenance, not signed attestation."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
INPUTS = ('mediaengine_readback.c', 'run-windows.ps1', 'analyze.py',
          'build_manifest.py', 'build-cross.sh', 'build-windows.cmd',
          'prepare_fixtures.py', 'reference.json')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes(root=ROOT):
    return {name: digest(root / name) for name in INPUTS}


def validate(manifest, root=ROOT):
    if manifest.get('schema') != 1 or manifest.get('sources') != source_hashes(root):
        raise ValueError('Build manifest does not match this tool source revision')
    if manifest.get('executable', {}).get('name') != 'mediaengine_readback.exe':
        raise ValueError('Unexpected executable identity')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('begin', 'finish'))
    parser.add_argument('directory', type=Path)
    parser.add_argument('--recipe', choices=('cross', 'msvc'), required=True)
    args = parser.parse_args()
    if args.mode == 'begin':
        args.directory.mkdir()  # A new build; never bless or overwrite an old binary.
        data = {'schema': 1, 'recipe': args.recipe, 'sources': source_hashes()}
        path = args.directory / 'source-inputs.json'
    else:
        data = json.loads((args.directory / 'source-inputs.json').read_text())
        if data['sources'] != source_hashes() or data['recipe'] != args.recipe:
            raise ValueError('Inputs changed during the build')
        executable = args.directory / 'mediaengine_readback.exe'
        if executable.read_bytes()[:2] != b'MZ':
            raise ValueError('Expected a Windows PE executable')
        data['executable'] = {'name': executable.name, 'sha256': digest(executable)}
        data['build_log_sha256'] = digest(args.directory / 'build.log')
        data['windows_runtime_tested'] = False
        data['attestation'] = 'Local build provenance only; no signature or remote build attestation.'
        path = args.directory / 'build-manifest.json'
    with path.open('x') as output:
        json.dump(data, output, indent=2)
        output.write('\n')


if __name__ == '__main__':
    main()
