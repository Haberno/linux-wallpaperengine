"""LDR bloom regressions using the installed stock shaders and isolated GPU draws.

python3 src/WallpaperEngine/Testing/test_scene_bloom.py build-release/output/linux-wallpaperengine
"""
import argparse
import json
from pathlib import Path
import tempfile

from test_scene_reflections import capture, material, prepare, write_json


def quantize(value):
    return round(max(0.0, min(1.0, value)) * 255) / 255


def uniform_expected(color, strength, threshold, tint):
    # Stock LDR extraction operates on the already quantized RGBA8 scene,
    # thresholds max RGB, doubles saturation, and stores another RGBA8 target.
    source = [quantize(c) for c in color]
    scale = max(0.0, min(1.0, max(source) - threshold))
    extracted = [c * scale for c in source]
    grey = sum(c * weight for c, weight in zip(extracted, (0.2989, 0.5870, 0.1140)))
    bloom = [quantize((2 * c - grey) * strength * t) for c, t in zip(extracted, tint)]
    return tuple(round(255 * min(1.0, c + b)) for c, b in zip(source, bloom))


def run(engine, root):
    root.mkdir(parents=True, exist_ok=True)
    checks = []
    def check(name, actual, expected, tolerance=2):
        passed = max(abs(a - b) for a, b in zip(actual, expected)) <= tolerance
        checks.append(dict(name=name, actual=actual, expected=expected, passed=passed))
        print(('PASS' if passed else 'FAIL'), name, actual, 'expected', expected, flush=True)

    cases = [
        ('disabled', (0.75, 0.75, 0.75), {'bloom': False}, 0, .65, (1, 1, 1)),
        ('native-defaults', (0.75, 0.75, 0.75), {}, 2, .65, (1, 1, 1)),
        ('below-threshold', (.5, .4, .3), {'bloomstrength': 2, 'bloomthreshold': .65}, 2, .65, (1, 1, 1)),
        ('explicit-zero', (.75, .75, .75), {'bloomstrength': 0, 'bloomthreshold': 0}, 0, 0, (1, 1, 1)),
        ('white-strength', (.75, .75, .75), {'bloomstrength': 2, 'bloomthreshold': .65}, 2, .65, (1, 1, 1)),
        ('colored-tint', (.65, .35, .2), {'bloomstrength': 1.2, 'bloomthreshold': .3, 'bloomtint': '.25 .5 1'}, 1.2, .3, (.25, .5, 1)),
        ('scripted-controls', (.7, .7, .7), {
            'bloomstrength': {'value': 0, 'script': 'export function update(value) { return 1.5; }'},
            'bloomthreshold': {'value': 1, 'script': 'export function update(value) { return 0.4; }'},
            'bloomtint': {'value': '1 1 1', 'script': 'export function update(value) { return new Vec3(0.2, 0.5, 1); }'},
        }, 1.5, .4, (.2, .5, 1)),
        ('scripted-disable', (.75, .75, .75), {
            'bloom': {'value': True, 'script': 'export function update(value) { return false; }'},
            'bloomstrength': 2, 'bloomthreshold': .65,
        }, 0, .65, (1, 1, 1)),
        ('scripted-enable', (.75, .75, .75), {
            'bloom': {'value': False, 'script': 'export function update(value) { return true; }'},
            'bloomstrength': 2, 'bloomthreshold': .65,
        }, 2, .65, (1, 1, 1)),
    ]
    for name, color, settings, strength, threshold, tint in cases:
        probe = root / name
        rgb = ','.join(str(c) for c in color)
        scene = prepare(probe, f'void main() {{ gl_FragColor = vec4({rgb},1); }}')
        scene['general'].update(bloom=True, hdr=False)
        scene['general'].update(settings)
        write_json(probe/'materials/probe.json', material('probe', ['single']))
        image, stats = capture(engine, probe, scene)
        check(name, image.getpixel((160, 90)), uniform_expected(color, strength, threshold, tint))
        # A fullscreen bloom pass must retain spatially constant input all the way to the edges.
        check(name+' edge', image.getpixel((2, 2)), image.getpixel((160, 90)))

    # Bloom with zero strength must preserve the completed scene pixel for pixel,
    # even when the logical scene canvas is much smaller than the output.
    for enabled in (False, True):
        name = 'pixel-detail-bloom-' + str(enabled).lower()
        probe = root / name
        scene = prepare(probe, """void main() {
            float stripe = mod(floor(gl_FragCoord.x), 2.0);
            gl_FragColor = vec4(vec3(stripe), 1);
        }""")
        scene['general'].update(bloom=enabled, bloomstrength=0, bloomthreshold=.65)
        write_json(probe/'materials/probe.json', material('probe', ['single']))
        image, stats = capture(engine, probe, scene)
        actual = tuple(image.getpixel((x, 90))[0] for x in range(140, 148))
        check(name, actual, (0, 255, 0, 255, 0, 255, 0, 255))

    (root/'checks.json').write_text(json.dumps(checks, indent=2))
    assert all(c['passed'] for c in checks), 'LDR bloom regressions failed; see checks.json'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine', type=Path)
    parser.add_argument('--artifacts', type=Path)
    args = parser.parse_args()
    if args.artifacts:
        run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='lwe-bloom-') as directory:
            run(args.engine.resolve(), Path(directory))
