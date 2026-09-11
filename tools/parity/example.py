#!/usr/bin/env python3
"""Observable synthetic shader-clock/hash input-control example; no installed assets edited."""
import argparse
import json
from pathlib import Path
import struct

from capture import adapter_at, DEFAULT_ADAPTER, private_root
from determinism import check_observability, template
from parity import compare, sha256, write_json

DYNAMIC = 'float t = g_Time;\n float h = frac(sin(t * 12.9898 + 17.0) * 43758.5453);'
FIXED = 'float t = 1.25;\n float h = 0.375;'


def generate(path, width, height):
    if path.exists():
        raise ValueError('Use a new private source directory')
    adapter_at(DEFAULT_ADAPTER).fixture(path, width, height)
    scene = json.loads((path / 'scene.json').read_text())
    scene['general'].update(hdr=False, bloom=False)
    scene['objects'][0]['color'] = '1 1 1'
    write_json(path / 'scene.json', scene)
    write_json(path / 'project.json', dict(title='Parity clock/hash control fixture', type='scene', file='scene.json'))
    (path / 'shaders/probe.frag').write_text('''varying vec2 v_TexCoord;
uniform float g_Time;
void main(){
 ''' + DYNAMIC + '''
 vec3 c = vec3(0.5 + 0.25 * sin(t), h, 0.25);
 if(v_TexCoord.x > 0.5) c = (t == 1.25 && h == 0.375) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 1.0);
 gl_FragColor = vec4(c, 1.0);
}
''')
    # Full-sized white source avoids source-sized effect-FBO confounds.
    pixels = b'\xff' * (width * height * 4)
    (path / 'materials/single.tex').write_bytes(b'TEXV0005\0TEXI0001\0' +
        struct.pack('<7I', 0, 2, width, height, width, height, 0) + b'TEXB0001\0' +
        struct.pack('<5I', 1, 1, width, height, len(pixels)) + pixels)
    write_json(path / 'fixture.json', dict(version=1, width=width, height=height,
        description='Left half: changing g_Time and deterministic sin/fract hash. Right half: magenta when uncontrolled, green only when t==1.25 and h==0.375.',
        controlled_values=dict(time=1.25, hash_output=.375),
        exclusions=['Original clock progression and procedural hash numerical behavior after fixed substitution'],
        reference_limits='Native scissor margins and Wine/Gamescope color transfer remain uncalibrated; inspect central regions.'))


def controls_for(root, dynamic_run):
    scenario = json.loads((root / 'scenario.json').read_text())
    if sha256(dynamic_run / 'scenario.json') != sha256(root / 'scenario.json'):
        raise ValueError('Dynamic baseline does not belong to the current prepared fixture')
    dynamic_observations = {}
    for role in ('reference', 'candidate'):
        video = dynamic_run / f'{role}.mkv'
        metadata = json.loads((dynamic_run / f'{role}.json').read_text())
        if metadata['status'] != 'captured' or metadata['video']['sha256'] != sha256(video):
            raise ValueError('Dynamic baseline recording failed or changed')
        check_observability(video, [dict(region=[scenario['width'] * 3 // 4, scenario['height'] // 2, 4, 4],
                                        rgb=[255, 0, 255], tolerance=1)])
        motion = compare(video, video)['metrics']['reference_motion_mae_rgb8']
        if motion is None or motion <= 0:
            raise ValueError(f'{role} uncontrolled clock/hash output did not vary')
        dynamic_observations[role] = dict(path=str(video), sha256=sha256(video), motion_mae_rgb8=motion)
    controls = template({key: scenario['item'][key] for key in ('id', 'project_sha256', 'asset_digest')})
    for name in controls['sources']:
        evidence = 'Synthetic fixture has one self-contained image shader, static clip-space geometry, no particle/model/video/SceneScript/audio/input/camera-shot dependencies; source tree is hashed in scenario.json.'
        controls['sources'][name] = dict(state='absent', native={'evidence': evidence}, fork={'evidence': evidence})
    for name, value in (('shader_clock', 1.25), ('shader_random', .375)):
        controls['sources'][name] = dict(state='fixed', native=dict(value=value,
            evidence='Same private loose shader literal substitution; green marker explicitly tests t==1.25 and h==0.375 in the executing shader.'),
            fork=dict(value=value, evidence='Same private loose shader literal substitution; green marker explicitly tests t==1.25 and h==0.375 in the executing shader.'))
    controls.update(scope='altered_controls', coverage_exclusions=[
        'Original clock progression and sin/fract procedural hash output. Identical inputs can produce different deterministic floating/trigonometric hash results across shader backends; this example excludes that branch.',
        'Original installed wallpaper behavior, particle RNG/initializers, control-point/rope history and native/fork randomized camera queue behavior are not exercised.'])
    controls['observed_uncontrolled_baseline'] = dynamic_observations
    controls['dependency_coverage'] = dict(verified=True, evidence='Self-contained synthetic fixture source and full-size white texture; no stochastic stock shader includes. Simulation timestep does not affect the fixed shader/geometry.')
    controls['cross_engine_known_inputs'] = dict(verified=True, evidence='Both renderers load the same hashed private project/GLSL shader with literals time=1.25 and procedural-hash-output=0.375; green marker directly checks those binary-exact values in the executing shader. This controls shader inputs, not cross-engine displayed RGB equivalence.')
    controls['observability'] = dict(verified=True, evidence='Green only if t==1.25 and h==0.375; uncontrolled time/hash footage must separately demonstrate variation and magenta before accepting this example.',
        probes={role: [dict(region=[scenario['width'] * 3 // 4, scenario['height'] // 2, 4, 4],
                           rgb=[0, 255, 0], tolerance=1)] for role in ('reference', 'candidate')})
    controls['substitutions'] = [dict(path='shaders/probe.frag', sha256=sha256(root / 'item/shaders/probe.frag'),
        old=DYNAMIC, new=FIXED, count=1, input_binding='Fix shader clock input and procedural hash output; preserve downstream color expression and sentinel.')]
    return controls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    generate_parser = commands.add_parser('generate')
    generate_parser.add_argument('--out', type=Path, required=True)
    generate_parser.add_argument('--width', type=int, default=320)
    generate_parser.add_argument('--height', type=int, default=180)
    control_parser = commands.add_parser('controls')
    control_parser.add_argument('--root', type=Path, required=True)
    control_parser.add_argument('--out', type=Path, required=True)
    control_parser.add_argument('--dynamic-run', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'generate':
        generate(private_root(args.out), args.width, args.height)
    else:
        write_json(args.out, controls_for(private_root(args.root), args.dynamic_run.resolve()))


if __name__ == '__main__':
    main()
