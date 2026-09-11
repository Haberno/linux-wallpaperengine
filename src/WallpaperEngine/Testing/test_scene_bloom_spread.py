"""Compare native LDR bloom dimensions and blur spread with a CPU reference.

Requires Pillow and a graphics session. Run under the shared render lock:
flock /tmp/lwe-render-fix-build.lock python3 \
    src/WallpaperEngine/Testing/test_scene_bloom_spread.py build-release/output/linux-wallpaperengine
Use --artifacts /tmp/name to retain captures, reference images and checks.
"""
import argparse
import json
import math
from pathlib import Path
import re
import tempfile

from PIL import Image

from test_scene_reflections import capture, material, prepare, write_json


WIDTH, HEIGHT = 320, 180
# Installed downsample_eighth_blur_v.frag and blur_h_bloom.frag use this
# symmetric 13-tap kernel. Its offsets are eight *scene* texels, not input texels.
WEIGHTS = (0.006299, 0.017298, 0.039533, 0.075189, 0.119007, 0.156756,
           0.171834, 0.156756, 0.119007, 0.075189, 0.039533, 0.017298, 0.006299)
FRAGMENT = """
void main() {
    vec2 distance = abs(gl_FragCoord.xy - vec2(160, 90));
    float gray = all(lessThan(distance, vec2(16))) ? 0.8 : 0.0;
    gl_FragColor = vec4(vec3(gray), 1);
}
"""


def unorm8(value):
    return int(min(1.0, max(0.0, value)) * 255 + 0.5) / 255


def bilinear(pixels, u, v):
    """Normalized GL texel-center coordinates with clamp-to-edge sampling."""
    height, width = len(pixels), len(pixels[0])
    x, y = u * width - 0.5, v * height - 0.5
    left, top = math.floor(x), math.floor(y)
    fx, fy = x - left, y - top

    def texel(column, row):
        return pixels[min(height - 1, max(0, row))][min(width - 1, max(0, column))]

    upper = texel(left, top) * (1 - fx) + texel(left + 1, top) * fx
    lower = texel(left, top + 1) * (1 - fx) + texel(left + 1, top + 1) * fx
    return upper * (1 - fy) + lower * fy


def cpu_reference():
    """Evaluate the verified native filters numerically, without executing shaders.

    The input is neutral gray, so its three color channels are identical. The
    saturation step still includes the stock luminance coefficients' 0.9999 sum.
    Every intermediate is quantized to RGBA8 before the next filter samples it.
    """
    source = [[0.8 if abs(x + 0.5 - WIDTH / 2) < 16 and abs(y + 0.5 - HEIGHT / 2) < 16
               else 0.0 for x in range(WIDTH)] for y in range(HEIGHT)]
    quarter_width, quarter_height = max(2, WIDTH // 4), max(2, HEIGHT // 4)
    eighth_width, eighth_height = max(2, WIDTH // 8), max(2, HEIGHT // 8)
    quarter = []
    for y in range(quarter_height):
        row = []
        for x in range(quarter_width):
            u, v = (x + 0.5) / quarter_width, (y + 0.5) / quarter_height
            gray = sum(bilinear(source, u + dx / WIDTH, v + dy / HEIGHT)
                       for dx, dy in ((-1, -1), (1, 1), (-1, 1), (1, -1))) / 4
            bright = gray * max(0.0, min(1.0, gray - 0.65))
            saturated = bright * (2 - (0.2989 + 0.5870 + 0.1140))
            row.append(unorm8(saturated * 2))
        quarter.append(row)

    def blur(pixels, horizontal):
        return [[unorm8(sum(weight * bilinear(
            pixels, (x + 0.5) / eighth_width + (tap * 8 / WIDTH if horizontal else 0),
            (y + 0.5) / eighth_height + (0 if horizontal else tap * 8 / HEIGHT))
            for tap, weight in zip(range(-6, 7), WEIGHTS)))
            for x in range(eighth_width)] for y in range(eighth_height)]

    horizontal = blur(quarter, True)
    bloom = blur(horizontal, False)
    final = [[unorm8(source[y][x] + bilinear(bloom, (x + 0.5) / WIDTH, (y + 0.5) / HEIGHT))
              for x in range(WIDTH)] for y in range(HEIGHT)]
    return source, quarter, horizontal, bloom, final


def gray_image(pixels):
    image = Image.new("RGB", (len(pixels[0]), len(pixels)))
    image.putdata([(round(value * 255),) * 3 for row in pixels for value in row])
    return image


def run(engine, root):
    root.mkdir(parents=True, exist_ok=True)
    reference = cpu_reference()
    for name, pixels in zip(("source", "quarter", "horizontal", "bloom", "final"), reference):
        gray_image(pixels).save(root / f"reference-{name}.png")
    expected = gray_image(reference[-1])
    probe = root / "spread"
    scene = prepare(probe, FRAGMENT)
    scene["general"].update(bloom=True, bloomstrength=2, bloomthreshold=0.65,
                            bloomtint="1 1 1", hdr=False)
    write_json(probe / "materials/probe.json", material("probe", ["single"]))
    actual, stats = capture(engine, probe, scene)
    results = []

    def check(name, observed, wanted, passed=None):
        passed = observed == wanted if passed is None else passed
        results.append(dict(name=name, actual=observed, expected=wanted, passed=passed))
        print(f"{'PASS' if passed else 'FAIL'} {name}: {observed!r} (expected {wanted!r})", flush=True)

    for target, size in (("_rt_4FrameBuffer", (80, 45)), ("_rt_8FrameBuffer", (40, 22)),
                         ("_rt_Bloom", (40, 22))):
        match = re.search(re.escape(target) + r"@(\d+)x(\d+)x", stats)
        dimensions = tuple(map(int, match.groups())) if match else None
        check(f"{target} uses native floor-divided dimensions", dimensions, size)

    offsets = (0, 8, 15, 16, 24, 32, 40, 48, 56, 64)
    for axis in ("horizontal", "vertical"):
        points = [(160 + offset, 90) if axis == "horizontal" else (160, 90 + offset)
                  for offset in offsets]
        observed = [actual.getpixel(point)[0] for point in points]
        wanted = [expected.getpixel(point)[0] for point in points]
        check(f"{axis} blur profile matches independent CPU reference", observed, wanted,
              all(abs(a - b) <= 2 for a, b in zip(observed, wanted)))

    max_error = max(abs(a - b) for y in range(HEIGHT) for x in range(WIDTH)
                    for a, b in zip(actual.getpixel((x, y)), expected.getpixel((x, y))))
    check("full image stays within two RGBA8 levels of the reference", max_error, "<= 2", max_error <= 2)
    symmetry = max(max(abs(actual.getpixel((x, y))[0] - actual.getpixel((WIDTH - 1 - x, y))[0]),
                       abs(actual.getpixel((x, y))[0] - actual.getpixel((x, HEIGHT - 1 - y))[0]))
                   for y in range(HEIGHT) for x in range(WIDTH))
    check("centered bloom remains symmetric on both axes", symmetry, "<= 1", symmetry <= 1)
    halo = actual.getpixel((184, 90))[0]
    check("bloom reaches beyond the bright square without becoming a flat wash", halo, "1..31", 1 <= halo <= 31)
    check("distant background remains black", actual.getpixel((16, 16)), (0, 0, 0))
    (root / "checks.json").write_text(json.dumps(results, indent=2))
    failures = [result["name"] for result in results if not result["passed"]]
    assert not failures, f"{len(failures)} bloom spread regressions failed: {', '.join(failures)}"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-bloom-spread-") as directory:
            run(args.engine.resolve(), Path(directory))
