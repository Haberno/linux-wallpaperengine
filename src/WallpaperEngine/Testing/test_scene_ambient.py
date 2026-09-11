"""Scene ambient/skylight updates reaching actual shader draws.

Run under flock /tmp/lwe-render-fix-build.lock; requires Pillow and a desktop.
"""
import argparse
import json
from pathlib import Path
import tempfile

from test_scene_reflections import capture, material, prepare, write_json


def animated(initial, updated):
    return {"value": initial, "script": """
let frames = 0;
export function update(value) {
    frames++;
    return frames > 3 ? new Vec3(%s) : value;
}
""" % updated}


def run(engine, root):
    root.mkdir(parents=True, exist_ok=True)
    checks = []
    cases = [
        ("static", "1 0 0", "0 0 1", (255, 0, 0), (0, 0, 255)),
        ("scripted-colors", animated("1 0 0", "0, 1, 0"),
         animated("0 0 1", "1, 0.5, 0"), (0, 255, 0), (255, 128, 0)),
        ("scripted-black", animated("1 0 0", "0, 0, 0"),
         animated("0 0 1", "0, 0, 0"), (0, 0, 0), (0, 0, 0)),
    ]
    for name, ambient, skylight, expected_ambient, expected_skylight in cases:
        probe = root / name
        scene = prepare(probe, """
uniform vec3 g_LightAmbientColor;
uniform vec3 g_LightSkylightColor;
void main() {
    gl_FragColor = vec4(gl_FragCoord.x < 160.0 ? g_LightAmbientColor : g_LightSkylightColor, 1);
}
""")
        scene["general"].update(ambientcolor=ambient, skylightcolor=skylight)
        write_json(probe / "materials/probe.json", material("probe", ["single"]))
        image, _ = capture(engine, probe, scene)
        log = (probe / "engine.log").read_text()
        assert not any(error in log for error in ("SyntaxError", "ReferenceError", "TypeError", "Failed to setup object")), log
        for label, x, expected in (("ambient", 80, expected_ambient), ("skylight", 240, expected_skylight)):
            actual = image.getpixel((x, 90))
            passed = max(abs(a - b) for a, b in zip(actual, expected)) <= 1
            checks.append(dict(name=f"{name} {label}", actual=actual, expected=expected, passed=passed))
            print("PASS" if passed else "FAIL", checks[-1], flush=True)
    (root / "checks.json").write_text(json.dumps(checks, indent=2))
    assert all(check["passed"] for check in checks), "Scene lighting colors did not reach the shader"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-scene-ambient-") as directory:
            run(args.engine.resolve(), Path(directory))
