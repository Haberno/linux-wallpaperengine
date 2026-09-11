"""Stock generic3 light-version regressions using a real MDLV0013 plane.

flock /tmp/lwe-render-fix-build.lock python3 \
    src/WallpaperEngine/Testing/test_scene_legacy_lighting.py build-release/output/linux-wallpaperengine
Use --artifacts /tmp/name to retain fixtures, captures and logs. Requires Pillow/desktop.
"""
import argparse
import json
from pathlib import Path
import struct
import tempfile

from test_scene_reflections import capture, material, prepare
from test_scene_resolution import write_json


def fixture(root, light):
    root.mkdir(parents=True)
    write_json(root / "project.json", {"title": "Legacy model lighting regression",
                                       "type": "scene", "file": "scene.json"})
    write_json(root / "materials/plane.json", {"passes": [{
        "shader": "generic3", "textures": ["util/white"],
        "combos": {"LIGHTING": 1, "REFLECTION": 0, "SHADINGGRADIENT": 0,
                   "RIMLIGHTING": 0, "EMISSIVE_MAP": 0},
        "constantshadervalues": {"roughness": 1, "metallic": 0},
        "blending": "normal", "cullmode": "nocull",
        "depthtest": "enabled", "depthwrite": "enabled",
    }]})
    vertices = b"".join(struct.pack("<12f", x, y, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0)
                        for x, y in ((-1, -1), (1, -1), (1, 1), (-1, 1)))
    indices = struct.pack("<6H", 0, 1, 2, 0, 2, 3)
    model = (b"MDLV0013\0" + struct.pack("<3I", 15, 1, 1) + b"materials/plane.json\0"
             + struct.pack("<2I", 0, len(vertices)) + vertices
             + struct.pack("<I", len(indices)) + indices)
    (root / "models").mkdir()
    (root / "models/plane.mdl").write_bytes(model)
    return {
        "camera": {"eye": "0 0 3", "center": "0 0 0", "up": "0 1 0"},
        "general": {"orthogonalprojection": None, "nearz": 0.1, "farz": 100, "fov": 45,
                    "ambientcolor": "0 0 0", "skylightcolor": "0 0 0",
                    "clearcolor": "0 0 0", "clearenabled": True, "bloom": False, "hdr": False},
        "objects": [{"id": 1, "name": "lit plane", "model": "models/plane.mdl",
                     "origin": "0 0 0"}, {"id": 2, "name": "light", "color": "1 1 1",
                                          "intensity": 0.5, **light}],
    }


def run(engine, root):
    spot = {"light": "lspot", "origin": "0 0 1", "angles": "0 1.57079632679 0",
            "radius": 1, "innercone": 10, "outercone": 20}
    cases = {
        "on-axis": spot,
        "radius2": {**spot, "radius": 2},
        "off-cone": {**spot, "angles": "0 0 0"},
        "directional": {"light": "ldirectional", "angles": "0 1.57079632679 0"},
    }
    samples = {}
    for name, light in cases.items():
        probe = root / name
        scene = fixture(probe, light)
        image, _ = capture(engine, probe, scene)
        log = (probe / "engine.log").read_text()
        assert "Failed to setup object" not in log, log
        samples[name] = image.getpixel((160, 90))

    results = []

    def check(name, actual, passed):
        results.append({"name": name, "actual": actual, "passed": passed})
        print(f"{'PASS' if passed else 'FAIL'} {name}: {actual!r}", flush=True)

    on_axis, radius2 = samples["on-axis"], samples["radius2"]
    check("on-axis spot illuminates the model without ambient/emission", on_axis, min(on_axis) > 10)
    check("off-cone model receives no spot contribution", samples["off-cone"], max(samples["off-cone"]) <= 1)
    # The stock deprecated lighting branch scales energy by radius squared.
    # Keep both samples below saturation so a clipped image cannot pass this ratio.
    check("radius control stays measurable and unsaturated", (on_axis, radius2),
          min(on_axis) > 10 and max(radius2) < 245)
    ratio = radius2[0] / max(on_axis[0], 1)
    check("doubling spot radius produces four times the light", round(ratio, 3), abs(ratio - 4) < 0.35)
    check("directional lighting remains active with the selected shader version", samples["directional"],
          min(samples["directional"]) > 10)

    for name, prefix, expected in (("builtin-version", "", 69),
                                    ("source-version-override", "#define SHADERVERSION 61\n", 61)):
        probe = root / name
        scene = prepare(probe, prefix + f"""
void main() {{
#if SHADERVERSION == {expected}
    gl_FragColor = vec4(0, 1, 0, 1);
#else
    gl_FragColor = vec4(1, 0, 0, 1);
#endif
}}
""")
        write_json(probe / "materials/probe.json", material("probe", ["single"]))
        image, _ = capture(engine, probe, scene)
        log = (probe / "engine.log").read_text()
        assert "Failed to setup object" not in log, log
        pixel = image.getpixel((160, 90))
        check(f"{name} selects shader revision{expected}", pixel, pixel == (0, 255, 0))

    (root / "checks.json").write_text(json.dumps({"samples": samples, "checks": results}, indent=2))
    failed = [result["name"] for result in results if not result["passed"]]
    assert not failed, f"{len(failed)} legacy-lighting regressions failed: {', '.join(failed)}"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-legacy-lighting-") as directory:
            run(args.engine.resolve(), Path(directory))
