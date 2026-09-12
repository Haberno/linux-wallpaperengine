"""Native-reference checks for particle sequence coordinates; requires Pillow/GPU."""
import argparse
import json
from pathlib import Path
import tempfile

from test_particle_rotation import prepare
from test_scene_reflections import capture, write_json


def run(engine, root):
    results = []
    # The oblique-axis position was checked against the native renderer with
    # CP1=(100,50,20), a zero-radius emitter, zero speed, and sequence phase 0.
    for name, axis, expected in (
        ("oblique-axis", "1 1 2", "116.233692877 66.233692877 -91.233692877"),
        ("z-axis-control", "0 0 1", "100 161.803398875 0"),
    ):
        case = root / name
        scene = prepare(case)
        (case / "shaders/genericparticle.frag").unlink()
        material = json.loads((case / "materials/marker.json").read_text())
        material["passes"][0].update(textures=["util/white"], blending="additive")
        write_json(case / "materials/marker.json", material)
        scene["camera"]["eye"] = "0 0 300"
        scene["general"]["farz"] = 1000
        base = json.loads((case / "particles/marker.json").read_text())
        base["operator"] = []
        for kind, color in (("mapped", "0 255 0"), ("control", "255 0 0")):
            particle = dict(base, initializer=[
                {"name": "lifetimerandom", "min": 100, "max": 100},
                {"name": "sizerandom", "min": 24, "max": 24},
                {"name": "colorrandom", "min": color, "max": color},
            ])
            if kind == "mapped":
                particle["controlpoint"] = [
                    {"id": i, "offset": "100 50 20" if i == 1 else "0 0 0", "flags": 0}
                    for i in range(8)]
                particle["initializer"].append({"name": "mapsequencearoundcontrolpoint",
                                                "axis": axis, "controlpoint": 1, "count": 4})
            write_json(case / f"particles/{kind}.json", particle)
        scene["objects"] = [
            {"id": 1, "name": "mapped", "particle": "particles/mapped.json", "origin": "0 0 0",
             "instanceoverride": {"controlpoint1": "100 50 20"}},
            {"id": 2, "name": "control", "particle": "particles/control.json", "origin": expected},
        ]
        image, _ = capture(engine, case, scene, ["--noautomute"])
        yellow = red = green = 0
        for r, g, _ in image.get_flattened_data():
            yellow += r > 200 and g > 200
            red += r > 200 and g < 20
            green += g > 200 and r < 20
        passed = yellow >= 8 and red <= 2 and green <= 2
        results.append(passed)
        print(f"{'PASS' if passed else 'FAIL'} {name}: yellow={yellow}, red={red}, green={green}", flush=True)
    return all(results)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        success = run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-particle-sequence-") as directory:
            success = run(args.engine.resolve(), Path(directory))
    raise SystemExit(0 if success else 1)
