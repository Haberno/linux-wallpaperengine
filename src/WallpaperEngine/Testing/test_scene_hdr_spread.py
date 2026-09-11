"""Compare native HDR bloom spread with an independent CPU reference.

Requires Pillow and a graphics session. Run under the shared render lock:
flock /tmp/lwe-render-fix-build.lock python3 \
    src/WallpaperEngine/Testing/test_scene_hdr_spread.py build-new/output/linux-wallpaperengine
Use --artifacts /tmp/name to retain captures, reference images, stats and checks.
"""
import argparse
import json
import math
import os
from pathlib import Path
import re
import signal
import socket
import struct
import subprocess
import tempfile
import time

from PIL import Image

from test_scene_reflections import material, prepare, write_json


WIDTH, HEIGHT = 65, 37
EXPECTED_LEVEL_SIZES = ((32, 18), (16, 9), (8, 4), (4, 2), (2, 2))
SPOTS = (
    ((16.5, 10.5), (4.0, 1.5, 0.25)),
    ((48.5, 18.5), (0.25, 2.5, 6.0)),
)
HALF_EXTENT = 1.1
STRENGTH = 2.0
THRESHOLD = 1.0
FEATHER = 0.2
TINT = (1.0, 0.75, 0.5)
CASES = (
    ("two-level", 2, 1.0),
    ("five-level", 5, 1.0),
    ("five-level-wide", 5, 1.6),
)
FRAGMENT = """
void main() {
    vec2 p = gl_FragCoord.xy;
    vec3 color = vec3(0.0);
    if (all(lessThan(abs(p - vec2(16.5, 10.5)), vec2(1.1)))) color = vec3(4.0, 1.5, 0.25);
    if (all(lessThan(abs(p - vec2(48.5, 18.5)), vec2(1.1)))) color = vec3(0.25, 2.5, 6.0);
    gl_FragColor = vec4(color, 1.0);
}
"""
EDGE_COLOR = (5.0, 0.5, 2.0)
EDGE_FRAGMENT = """
void main() {
    bool bright = gl_FragCoord.x < 2.0 && abs(gl_FragCoord.y - 18.5) < 1.1;
    gl_FragColor = vec4(bright ? vec3(5.0, 0.5, 2.0) : vec3(0.0), 1.0);
}
"""
COORDINATE_FRAGMENT = """
void main() {
    vec2 coordinate = floor(gl_FragCoord.xy);
    float checker = mod(coordinate.x + coordinate.y, 2.0);
    gl_FragColor = vec4(coordinate / 255.0, checker, 1.0);
}
"""


def add(left, right):
    return tuple(a + b for a, b in zip(left, right))


def scale(value, factor):
    return tuple(channel * factor for channel in value)


def mix(left, right, factor):
    return tuple(a * (1.0 - factor) + b * factor for a, b in zip(left, right))


def half(value):
    """Round once to IEEE binary16, as each RGBA16F target write does."""
    return struct.unpack("<e", struct.pack("<e", value))[0]


def half3(value):
    return tuple(half(channel) for channel in value)


def unorm8(value):
    return int(min(1.0, max(0.0, value)) * 255.0 + 0.5)


def bilinear(pixels, u, v):
    """Normalized GL texel-center coordinates with clamp-to-edge sampling."""
    height, width = len(pixels), len(pixels[0])
    x, y = u * width - 0.5, v * height - 0.5
    left, top = math.floor(x), math.floor(y)
    fx, fy = x - left, y - top

    def texel(column, row):
        row = min(height - 1, max(0, row))
        column = min(width - 1, max(0, column))
        return pixels[row][column]

    upper = mix(texel(left, top), texel(left + 1, top), fx)
    lower = mix(texel(left, top + 1), texel(left + 1, top + 1), fx)
    return mix(upper, lower, fy)


def cubic_weights(value):
    n = tuple(component - value for component in (1.0, 2.0, 3.0, 4.0))
    powers = tuple(component ** 3 for component in n)
    x = powers[0]
    y = powers[1] - 4.0 * powers[0]
    z = powers[2] - 4.0 * powers[1] + 6.0 * powers[0]
    w = 6.0 - x - y - z
    return tuple(component / 6.0 for component in (x, y, z, w))


def bicubic(pixels, u, v, nominal_width, nominal_height):
    """Stock four-bilinear-tap cubic filter using native nominal texSize."""
    x, y = u * nominal_width - 0.5, v * nominal_height - 0.5
    base_x, base_y = math.floor(x), math.floor(y)
    wx, wy = cubic_weights(x - base_x), cubic_weights(y - base_y)
    sx = (wx[0] + wx[1], wx[2] + wx[3])
    sy = (wy[0] + wy[1], wy[2] + wy[3])
    offsets = (
        (base_x - 0.5 + wx[1] / sx[0]) / nominal_width,
        (base_x + 1.5 + wx[3] / sx[1]) / nominal_width,
        (base_y - 0.5 + wy[1] / sy[0]) / nominal_height,
        (base_y + 1.5 + wy[3] / sy[1]) / nominal_height,
    )
    sample0 = bilinear(pixels, offsets[0], offsets[2])
    sample1 = bilinear(pixels, offsets[1], offsets[2])
    sample2 = bilinear(pixels, offsets[0], offsets[3])
    sample3 = bilinear(pixels, offsets[1], offsets[3])
    blend_x = sx[0] / (sx[0] + sx[1])
    blend_y = sy[0] / (sy[0] + sy[1])
    return mix(mix(sample3, sample2, blend_x), mix(sample1, sample0, blend_x), blend_y)


def four_taps(pixels, u, v, offset_x, offset_y, sampler=bilinear):
    return scale(add(add(sampler(pixels, u + offset_x, v + offset_y),
                         sampler(pixels, u - offset_x, v + offset_y)),
                     add(sampler(pixels, u + offset_x, v - offset_y),
                         sampler(pixels, u - offset_x, v - offset_y))), 0.25)


def source_pixels(clamp_hdr=False):
    result = []
    for y in range(HEIGHT):
        row = []
        for x in range(WIDTH):
            color = (0.0, 0.0, 0.0)
            for (center_x, center_y), candidate in SPOTS:
                if abs(x + 0.5 - center_x) < HALF_EXTENT and abs(y + 0.5 - center_y) < HALF_EXTENT:
                    color = candidate
            if clamp_hdr:
                color = tuple(min(1.0, channel) for channel in color)
            row.append(half3(color))
        result.append(row)
    return result


def edge_source_pixels(clamp_hdr=False):
    color = tuple(min(1.0, channel) for channel in EDGE_COLOR) if clamp_hdr else EDGE_COLOR
    return [[half3(color if x < 2 and abs(y + 0.5 - 18.5) < HALF_EXTENT else (0.0, 0.0, 0.0))
             for x in range(WIDTH)] for y in range(HEIGHT)]


def coordinate_pixels():
    return [[(x, y, 255 if (x + y) % 2 else 0) for x in range(WIDTH)] for y in range(HEIGHT)]


def level_sizes():
    depth = min(8, int(math.log2(min(WIDTH, HEIGHT))))
    return [(max(2, WIDTH // (2 ** (level + 1))), max(2, HEIGHT // (2 ** (level + 1))))
            for level in range(depth)]


def cpu_reference(iterations, scatter, clamp_hdr=False, edge=False):
    """Evaluate stock taps and quantize every main/pyramid write independently."""
    scene = edge_source_pixels(clamp_hdr) if edge else source_pixels(clamp_hdr)
    sizes = level_sizes()
    count = max(1, min(int(iterations), len(sizes)))
    knee = THRESHOLD * FEATHER
    bloom_strength = STRENGTH / (1.0 + scatter ** (max(2, count) - 2))
    levels = []

    for level in range(count):
        target_width, target_height = sizes[level]
        source = scene if level == 0 else levels[level - 1]
        offset = 1 if level == 0 else 2 ** level
        rows = []
        for y in range(target_height):
            row = []
            for x in range(target_width):
                u, v = (x + 0.5) / target_width, (y + 0.5) / target_height
                value = four_taps(source, u, v, offset / WIDTH, offset / HEIGHT)
                if level == 0:
                    value = tuple(max(0.0, channel) for channel in value)
                    brightness = max(value)
                    soft = min(2.0 * knee, max(0.0, brightness - THRESHOLD + knee))
                    soft = soft * soft * (0.25 / (knee + 0.00001))
                    contribution = max(soft, brightness - THRESHOLD) / max(brightness, 0.00001)
                    value = tuple(channel * contribution * bloom_strength * tint
                                  for channel, tint in zip(value, TINT))
                row.append(half3(value))
            rows.append(row)
        levels.append(rows)

    for level in range(count - 1, 0, -1):
        destination = levels[level - 1]
        target_height, target_width = len(destination), len(destination[0])
        offset = 2 ** level
        use_cubic = level >= count - 2
        if use_cubic:
            nominal_width = WIDTH / (2.0 * offset)
            nominal_height = HEIGHT / (2.0 * offset)
            sampler = lambda pixels, u, v, nw=nominal_width, nh=nominal_height: bicubic(pixels, u, v, nw, nh)
        else:
            sampler = bilinear
        rows = []
        for y in range(target_height):
            row = []
            for x in range(target_width):
                u, v = (x + 0.5) / target_width, (y + 0.5) / target_height
                reconstructed = four_taps(
                    levels[level], u, v, offset / WIDTH, offset / HEIGHT, sampler
                )
                row.append(half3(add(destination[y][x], scale(reconstructed, scatter))))
            rows.append(row)
        levels[level - 1] = rows

    result = []
    for y in range(HEIGHT):
        row = []
        for x in range(WIDTH):
            u, v = (x + 0.5) / WIDTH, (y + 0.5) / HEIGHT
            bloom = four_taps(levels[0], u, v, 1.0 / WIDTH, 1.0 / HEIGHT)
            row.append(tuple(unorm8(channel + glow) for channel, glow in zip(scene[y][x], bloom)))
        result.append(row)
    return result


def rgb_image(pixels):
    image = Image.new("RGB", (len(pixels[0]), len(pixels)))
    image.putdata([pixel for row in pixels for pixel in row])
    return image


def capture(engine, root, scene, clamp="clamp"):
    write_json(root / "scene.json", scene)
    picture, control = root / "result.png", root / "control.sock"
    env = dict(os.environ, WPE_LOG_FILE=str(root / "engine.log"), WPE_CONTROL_SOCKET=str(control))
    env.pop("LD_PRELOAD", None)
    env.pop("HDR_DUMP_DIR", None)
    with (root / "output.log").open("w") as log:
        process = subprocess.Popen([
            str(engine), "--window", f"100x100x{WIDTH}x{HEIGHT}", "--scaling", "stretch", "--fps", "15",
            "--clamp", clamp, "--msaa", "off", "--volume", "0",
            "--screenshot", str(picture), "--screenshot-delay", "30",
            "--post-processing", "ultra", str(root),
        ], env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            deadline = time.monotonic() + 30
            while not picture.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.1)
            assert picture.exists(), (root / "output.log").read_text()
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(3)
                client.connect(str(control))
                client.sendall(b"fbostats\n")
                chunks = []
                while chunk := client.recv(65536):
                    chunks.append(chunk)
            stats = b"".join(chunks).decode()
            (root / "fbostats.txt").write_text(stats)
            with Image.open(picture) as image:
                result = image.convert("RGB")
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
    return result, stats


def run(engine, root):
    root.mkdir(parents=True, exist_ok=True)
    checks = []
    actual_images = {}
    expected_images = {}

    def check(name, actual, expected, passed=None):
        passed = actual == expected if passed is None else passed
        checks.append(dict(name=name, actual=actual, expected=expected, passed=passed))
        print(f"{'PASS' if passed else 'FAIL'} {name}: {actual!r} (expected {expected!r})", flush=True)

    coordinate_expected = rgb_image(coordinate_pixels())
    for name, hdr in (("coordinate-ldr", False), ("coordinate-hdr-zero", True)):
        probe = root / name
        scene = prepare(probe, COORDINATE_FRAGMENT)
        scene["general"].update(
            hdr=hdr, bloom=hdr, bloomhdrstrength=0.0, bloomtint="1 1 1",
        )
        write_json(probe / "materials/probe.json", material("probe", ["single"]))
        actual, _ = capture(engine, probe, scene)
        coordinate_expected.save(probe / "reference.png")
        max_error = max(abs(a - b) for observed, wanted in zip(actual.getdata(), coordinate_expected.getdata())
                        for a, b in zip(observed, wanted))
        check(f"{name} screenshot preserves the odd one-pixel coordinate grid",
              max_error, "<= 1", max_error <= 1)
        columns = [actual.getpixel((x, HEIGHT // 2))[0] for x in range(WIDTH)]
        rows = [actual.getpixel((WIDTH // 2, y))[1] for y in range(HEIGHT)]
        check(f"{name} screenshot loses or duplicates no column", columns, list(range(WIDTH)))
        check(f"{name} screenshot loses or duplicates no row", rows, list(range(HEIGHT)))

    for name, iterations, scatter in CASES:
        probe = root / name
        scene = prepare(probe, FRAGMENT)
        scene["general"].update(
            hdr=True, bloom=True, bloomhdrstrength=STRENGTH, bloomhdrthreshold=THRESHOLD,
            bloomhdrfeather=FEATHER, bloomhdrscatter=scatter, bloomhdriterations=iterations,
            bloomtint=" ".join(map(str, TINT)),
        )
        write_json(probe / "materials/probe.json", material("probe", ["single"]))
        actual, stats = capture(engine, probe, scene)
        expected_pixels = cpu_reference(iterations, scatter)
        expected = rgb_image(expected_pixels)
        expected.save(probe / "reference.png")
        actual_images[name], expected_images[name] = actual, expected

        check(f"{name} capture keeps the odd output dimensions", actual.size, (WIDTH, HEIGHT))
        max_error = max(abs(a - b) for observed, wanted in zip(actual.getdata(), expected.getdata())
                        for a, b in zip(observed, wanted))
        check(f"{name} full image matches native taps and half-float intermediates",
              max_error, "<= 2", max_error <= 2)

        profile_points = ((0, 10), (8, 10), (12, 10), (16, 10), (20, 10), (24, 10), (32, 10))
        observed_profile = [actual.getpixel(point) for point in profile_points]
        wanted_profile = [expected.getpixel(point) for point in profile_points]
        check(f"{name} uses native full-raster radius offsets", observed_profile, wanted_profile,
              all(max(abs(a - b) for a, b in zip(observed, wanted)) <= 2
                  for observed, wanted in zip(observed_profile, wanted_profile)))

        if name == "five-level":
            clipped = rgb_image(cpu_reference(iterations, scatter, clamp_hdr=True))
            clipped.save(probe / "reference-clamped-input.png")
            candidates = [(x, y) for y in range(HEIGHT) for x in range(WIDTH)
                          if all(abs(x + 0.5 - center_x) >= HALF_EXTENT
                                 or abs(y + 0.5 - center_y) >= HALF_EXTENT
                                 for (center_x, center_y), _ in SPOTS)]
            point = max(candidates, key=lambda p: max(abs(a - b) for a, b in zip(
                expected.getpixel(p), clipped.getpixel(p))))
            hdr_pixel, clipped_pixel, observed = expected.getpixel(point), clipped.getpixel(point), actual.getpixel(point)
            separation = max(abs(a - b) for a, b in zip(hdr_pixel, clipped_pixel))
            observed_error = max(abs(a - b) for a, b in zip(observed, hdr_pixel))
            check("values above 1 survive extraction into the off-source halo",
                  dict(point=point, pixel=observed, clipped_input=clipped_pixel),
                  dict(pixel=hdr_pixel, minimum_clipped_separation=8),
                  separation >= 8 and observed_error <= 2)

        if name == "two-level":
            for level, size in enumerate(EXPECTED_LEVEL_SIZES):
                match = re.search(rf"(?:^|[ ;])_rt_HDRBloom_{level}@(\d+)x(\d+)x", stats)
                observed = tuple(map(int, match.groups())) if match else None
                check(f"odd pyramid level {level} keeps floor division and minimum two", observed, size)
            match = re.search(r"(?:^|[ ;])_rt_HDROutput@(\d+)x(\d+)x", stats)
            observed = tuple(map(int, match.groups())) if match else None
            check("HDR combine target keeps the odd full-raster dimensions", observed, (WIDTH, HEIGHT))

    shallow, deep, wide = (actual_images[name] for name, _, _ in CASES)
    expected_shallow, expected_deep, expected_wide = (expected_images[name] for name, _, _ in CASES)
    # These are immediately outside the 3x3 source spots. Deep reconstruction
    # changes them strongly while leaving the authored source itself out of the comparison.
    spread_points = ((18, 10), (20, 10), (44, 18), (46, 18), (50, 18), (48, 16))
    for label, left, right, expected_left, expected_right in (
        ("iteration count changes the broad halo", shallow, deep, expected_shallow, expected_deep),
        ("scatter changes five-level reconstruction spread", deep, wide, expected_deep, expected_wide),
    ):
        observed_delta = [tuple(b - a for a, b in zip(left.getpixel(point), right.getpixel(point)))
                          for point in spread_points]
        wanted_delta = [tuple(b - a for a, b in zip(expected_left.getpixel(point), expected_right.getpixel(point)))
                        for point in spread_points]
        largest_change = max(abs(channel) for pixel in wanted_delta for channel in pixel)
        check(label, observed_delta, wanted_delta,
              largest_change >= 3 and all(max(abs(a - b) for a, b in zip(observed, wanted)) <= 3
                                          for observed, wanted in zip(observed_delta, wanted_delta)))

    edge_expected = rgb_image(cpu_reference(5, 1.0, edge=True))
    edge_expected.save(root / "reference-edge-clamp.png")
    edge_images = {}
    for clamp in ("clamp", "border", "repeat"):
        probe = root / f"edge-{clamp}"
        scene = prepare(probe, EDGE_FRAGMENT)
        scene["general"].update(
            hdr=True, bloom=True, bloomhdrstrength=STRENGTH, bloomhdrthreshold=THRESHOLD,
            bloomhdrfeather=FEATHER, bloomhdrscatter=1.0, bloomhdriterations=5,
            bloomtint=" ".join(map(str, TINT)),
        )
        write_json(probe / "materials/probe.json", material("probe", ["single"]))
        actual, _ = capture(engine, probe, scene, clamp)
        edge_images[clamp] = actual
        max_error = max(abs(a - b) for observed, wanted in zip(actual.getdata(), edge_expected.getdata())
                        for a, b in zip(observed, wanted))
        check(f"edge impulse uses framebuffer clamp sampling with --clamp {clamp}",
              max_error, "<= 2", max_error <= 2)

    baseline = edge_images["clamp"].tobytes()
    for clamp in ("border", "repeat"):
        check(f"edge HDR bloom is byte-identical for clamp and {clamp} presentation modes",
              edge_images[clamp].tobytes() == baseline, True)

    (root / "checks.json").write_text(json.dumps(checks, indent=2))
    failures = [result["name"] for result in checks if not result["passed"]]
    assert not failures, f"{len(failures)} HDR bloom spread regressions failed: {', '.join(failures)}"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-hdr-spread-") as directory:
            run(args.engine.resolve(), Path(directory))
