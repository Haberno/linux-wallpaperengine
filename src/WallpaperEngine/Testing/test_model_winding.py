"""Authored negative scales retain their effect on model face winding.

Run under flock /tmp/lwe-render-fix-build.lock on an isolated display.
"""
import argparse
import json
from pathlib import Path
import tempfile

from test_model_blend_order import fixture
from test_scene_reflections import capture
from test_scene_resolution import write_json


def run(engine, root):
    results = []
    for orthographic in (False, True):
        for scale, visible in (((1, 1, 1), True), ((-1, -1, -1), True), ((-1, 1, 1), False)):
            name = f"{'ortho' if orthographic else 'perspective'}-{'-'.join(map(str, scale))}"
            probe = root / name
            scene = fixture(probe, ["body"])
            material_path = probe / "materials/body.json"
            material = json.loads(material_path.read_text())
            material["passes"][0]["cullmode"] = "normal"
            write_json(material_path, material)
            model = scene["objects"][0]
            factor = 10 if orthographic else 1
            model["scale"] = " ".join(str(component * factor) for component in scale)
            if orthographic:
                scene["general"]["orthogonalprojection"] = {"width": 64, "height": 40}
                model["origin"] = "32 20 0"
            picture, _ = capture(engine, probe, scene, ("--noautomute", "--silent", "--no-fullscreen-pause"))
            actual = picture.getpixel((160, 90))
            expected = (51, 26, 0) if visible else (0, 0, 0)
            passed = max(abs(a - b) for a, b in zip(actual, expected)) <= 1
            results.append({"name": name, "actual": actual, "expected": expected, "passed": passed})
            print(f"{'PASS' if passed else 'FAIL'} {name}: {actual}, expected {expected}", flush=True)
    (root / "checks.json").write_text(json.dumps(results, indent=2))
    assert all(result["passed"] for result in results), "Model winding regression"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-model-winding-") as directory:
            run(args.engine.resolve(), Path(directory))
