"""MSAA scene coverage, post-processing and current-scene read regressions.

Run with flock /tmp/lwe-render-fix-build.lock; requires Pillow and a desktop.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import tempfile

from test_scene_reflections import capture
from test_scene_resolution import write_json


def fixture(root, bloom=False, read_scene=False, orthographic=False):
    root.mkdir(parents=True)
    write_json(root / "project.json", {"title": "MSAA scene regression", "type": "scene", "file": "scene.json"})
    write_json(root / "materials/edge.json", {"passes": [{"shader": "edge", "blending": "normal",
        "depthtest": "enabled", "depthwrite": "enabled", "cullmode": "nocull"}]})
    (root / "shaders").mkdir()
    (root / "shaders/edge.vert").write_text("""
uniform mat4 g_ModelViewProjectionMatrix;
attribute vec3 a_Position;
void main() { gl_Position = mul(vec4(a_Position, 1), g_ModelViewProjectionMatrix); }
""")
    (root / "shaders/edge.frag").write_text("void main() { gl_FragColor = vec4(1); }")
    vertices = b"".join(struct.pack("<12f", x, y, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0)
                        for x, y in ((-1, -0.83), (1.07, -0.69), (0.13, 0.94)))
    indices = struct.pack("<3H", 0, 1, 2)
    (root / "models").mkdir()
    (root / "models/edge.mdl").write_bytes(b"MDLV0013\0" + struct.pack("<3I", 15, 1, 1)
        + b"materials/edge.json\0" + struct.pack("<2I", 0, len(vertices)) + vertices
        + struct.pack("<I", len(indices)) + indices)
    scene = {"camera": {"eye": "0 0 3", "center": "0 0 0", "up": "0 1 0"},
             "general": {"orthogonalprojection": {"width": 360, "height": 210} if orthographic else None,
                         "nearz": 0.1, "farz": 100, "fov": 45, "bloom": bloom,
                         "bloomstrength": 0, "bloomthreshold": 0.65,
                         "clearcolor": "0 0 0", "clearenabled": True},
             "objects": [{"id": 1, "name": "diagonal edges", "model": "models/edge.mdl",
                          "origin": "180 105 0" if orthographic else "0 0 0",
                          "scale": "100 100 100" if orthographic else "1 1 1"}]}
    if read_scene:
        write_json(root / "models/read.json", {"material": "materials/read.json", "fullscreen": True})
        write_json(root / "materials/read.json", {"passes": [{"shader": "read", "blending": "normal",
            "depthtest": "disabled", "depthwrite": "disabled", "cullmode": "nocull",
            "textures": ["_rt_FullFrameBuffer"]}]})
        (root / "shaders/read.vert").write_text("""
attribute vec3 a_Position;
uniform mat4 g_ModelViewProjectionMatrix;
void main() { gl_Position = mul(vec4(a_Position, 1), g_ModelViewProjectionMatrix); }
""")
        (root / "shaders/read.frag").write_text("""
uniform sampler2D g_Texture0;
uniform vec4 g_Texture0Resolution;
void main() { gl_FragColor = vec4(1.0 - texSample2D(g_Texture0, gl_FragCoord.xy / g_Texture0Resolution.xy).rgb, 1); }
""")
        scene["objects"].append({"id": 2, "name": "current scene inversion", "image": "models/read.json"})
    return scene


def run(engine, root, baseline=False):
    results, images = [], {}
    cases = [("baseline", (), False, False, False)] if baseline else [
        ("off", ("--msaa", "off"), False, False, False),
        ("2x", ("--msaa", "2"), False, False, False),
        ("4x", ("--msaa", "4"), False, False, False),
        ("8x", ("--msaa", "8"), False, False, False),
        ("4x-bloom-zero", ("--msaa", "4"), True, False, False),
        ("4x-scene-read", ("--msaa", "4"), False, True, False),
        ("4x-orthographic", ("--msaa", "4"), False, False, True)]
    for name, options, bloom, read_scene, orthographic in cases:
        probe = root / name
        image, stats = capture(engine, probe, fixture(probe, bloom, read_scene, orthographic), options)
        log = (probe / "engine.log").read_text()
        assert "Failed to setup object" not in log, log
        images[name] = image
        pixels = image.tobytes()
        partial = sum(1 for r, g, b in zip(pixels[0::3], pixels[1::3], pixels[2::3])
                      if 0 < r < 255 and r == g == b)
        sample_match = re.search(r"Scene MSAA: requested=(\d+) actual=(\d+)", log)
        actual = int(sample_match[2]) if sample_match else (2 if baseline else 0)
        passed = partial > 40 if actual > 1 else actual == 1 and partial == 0
        results.append({"name": name, "samples": actual, "partial_edge_pixels": partial, "passed": passed})
        print(f"{'PASS' if passed else 'FAIL'} {name}: samples={actual}, partial edges={partial}", flush=True)
    if not baseline:
        # Bloom strength zero must preserve the resolved coverage; it must not
        # restore a stale MSAA color buffer over the post-process result.
        error = max(abs(a - b) for a, b in zip(images["4x"].tobytes(), images["4x-bloom-zero"].tobytes()))
        results.append({"name": "zero bloom preserves resolved scene", "error": error, "passed": error <= 1})
        # A scene-reading shader must see resolved coverage during geometry
        # rendering, and its single output must survive the final resolve.
        error = max(abs(255 - a - b) for a, b in zip(images["4x"].tobytes(), images["4x-scene-read"].tobytes()))
        results.append({"name": "scene read samples current resolved color", "error": error, "passed": error <= 1})
    (root / "checks.json").write_text(json.dumps(results, indent=2))
    assert all(result["passed"] for result in results), "MSAA scene regression"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--baseline", action="store_true")
    args = parser.parse_args()
    if args.artifacts:
        run(args.engine.resolve(), args.artifacts.resolve(), args.baseline)
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-scene-msaa-") as directory:
            run(args.engine.resolve(), Path(directory), args.baseline)
