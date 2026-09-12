"""Screen-direction checks for stock particle geometry; requires Pillow and a desktop.

flock /tmp/lwe-render-fix-build.lock python3 \
    src/WallpaperEngine/Testing/test_particle_rotation.py build/output/linux-wallpaperengine
"""
import argparse
import json
import math
from pathlib import Path
import tempfile

from test_scene_reflections import capture, material, texture, write_json


def prepare(root, rotation=0, velocity=0, force=0, layer_roll=0, perspective=True, fixed=False, cull=False,
            marker_uv=(0.75, 0.5)):
    root.mkdir(parents=True)
    (root / "shaders").mkdir()
    # Keep the installed genericparticle vertex shader and common_particles.h.
    # An off-center green marker makes its orientation visible without a
    # symmetric flare texture or a texture animation hiding the direction.
    (root / "shaders/genericparticle.frag").write_text("""
varying vec2 v_TexCoord;
void main() {
    if (distance(v_TexCoord, vec2(0.75, 0.5)) < 0.06)
        gl_FragColor = vec4(0, 1, 0, 1);
    else if (distance(v_TexCoord, vec2(0.5, 0.5)) < 0.04)
        gl_FragColor = vec4(1, 0, 0, 1);
    else discard;
}
""".replace("vec2(0.75, 0.5)", f"vec2({marker_uv[0]}, {marker_uv[1]})"))
    write_json(root / "project.json", {"title": "Particle rotation regression", "type": "scene",
                                       "file": "scene.json"})
    texture(root / "materials/white.tex", [(255, 255, 255)])
    marker_material = material("genericparticle", ["white"])
    if cull:
        marker_material["passes"][0]["cullmode"] = "normal"
    write_json(root / "materials/marker.json", marker_material)
    initializers = [
        {"name": "lifetimerandom", "min": 20, "max": 20},
        {"name": "sizerandom", "min": 8 if perspective else 20,
         "max": 8 if perspective else 20},
        {"name": "rotationrandom", "min": f"0 0 {rotation}", "max": f"0 0 {rotation}"},
        {"name": "angularvelocityrandom", "min": f"0 0 {velocity}", "max": f"0 0 {velocity}"},
    ]
    write_json(root / "particles/marker.json", {
        "material": "materials/marker.json", "maxcount": 1,
        "emitter": [{"name": "sphererandom", "rate": 0, "instantaneous": 1,
                     "distancemin": 0, "distancemax": 0}],
        "initializer": initializers,
        "operator": [{"name": "angularmovement", "force": f"0 0 {force}"}],
        "renderer": [{"name": "sprite", **({"orientation": "fixed", "axis": "0 0 1"} if fixed else {})}],
    })
    return {
        "camera": {"eye": "0 0 10", "center": "0 0 0", "up": "0 1 0"},
        "general": {"orthogonalprojection": None if perspective else {"width": 64, "height": 40},
                    "nearz": 0.1, "farz": 100, "fov": 60,
                    "clearcolor": "0 0 0", "clearenabled": True, "bloom": False},
        "objects": [{"id": 1, "name": "marker", "particle": "particles/marker.json",
                     "origin": "0 0 0" if perspective else "32 20 0",
                     "angles": f"0 0 {layer_roll}"}],
    }


def direction(image):
    centers = []
    for channel in (0, 1):
        pixels = [(x, y) for y in range(image.height) for x in range(image.width)
                  if image.getpixel((x, y))[channel] > 200
                  and image.getpixel((x, y))[1 - channel] < 20]
        assert len(pixels) >= 8, f"Missing particle marker: channel {channel}, pixels {len(pixels)}"
        centers.append(tuple(sum(p[axis] for p in pixels) / len(pixels) for axis in (0, 1)))
    delta = tuple(centers[1][axis] - centers[0][axis] for axis in (0, 1))
    length = math.hypot(*delta)
    assert length > 10, f"Collapsed particle marker: {delta}"
    return tuple(v / length for v in delta)


def run(engine, root):
    results = []
    cases = [
        ("positive-angle", {"rotation": 0.6}, (math.cos(0.6), math.sin(0.6))),
        ("negative-angle", {"rotation": -0.6}, (math.cos(0.6), -math.sin(0.6))),
        ("angular-velocity", {"velocity": 0.3}, None),
        ("angular-force", {"force": 0.15}, None),
        ("layer-half-turn", {"layer_roll": math.pi}, (-1, 0)),
        ("orthographic-control", {"rotation": 0.6, "perspective": False},
         (math.cos(0.6), math.sin(0.6))),
        ("fixed-orientation-control", {"rotation": 0.6, "fixed": True},
         (math.cos(0.6), -math.sin(0.6))),
        ("perspective-front-face", {"cull": True}, (1, 0)),
        ("orthographic-front-face", {"cull": True, "perspective": False}, (1, 0)),
        ("fixed-front-face", {"cull": True, "fixed": True}, (1, 0)),
        ("orthographic-texture-up", {"perspective": False, "marker_uv": (0.5, 0.25)}, (0, -1)),
        ("orthographic-fixed-texture-up", {"perspective": False, "fixed": True,
                                           "cull": True, "marker_uv": (0.5, 0.25)}, (0, -1)),
        ("perspective-texture-up", {"marker_uv": (0.5, 0.25)}, (0, -1)),
    ]
    for name, arguments, expected in cases:
        case = root / name
        image, _ = capture(engine, case, prepare(case, **arguments))
        actual = direction(image)
        # Positive native Z rotates the UV-right marker down in screen space.
        # Dynamic cases check direction, allowing ordinary frame-time variation.
        passed = (actual[0] > 0.3 and actual[1] > 0.1) if expected is None else (
            max(abs(a - b) for a, b in zip(actual, expected)) < 0.06)
        results.append({"name": name, "actual": actual, "expected": expected or "right and down",
                        "passed": passed})
        print(f"{'PASS' if passed else 'FAIL'} {name}: {actual}", flush=True)
    (root / "checks.json").write_text(json.dumps(results, indent=2) + "\n")
    return all(result["passed"] for result in results)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        args.artifacts.mkdir(parents=True, exist_ok=True)
        success = run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-particle-rotation-") as temporary:
            success = run(args.engine.resolve(), Path(temporary))
    raise SystemExit(0 if success else 1)
