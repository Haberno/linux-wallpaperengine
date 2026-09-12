"""High-drag particle movement regression, checked against the native renderer."""
import argparse
import json
from pathlib import Path
import tempfile

from test_particle_rotation import prepare
from test_scene_reflections import capture, write_json


def run(engine, root):
    scene = prepare(root, perspective=False)
    (root / "shaders/genericparticle.frag").unlink()
    material = json.loads((root / "materials/marker.json").read_text())
    material["passes"][0].update(textures=["util/white"], blending="additive")
    write_json(root / "materials/marker.json", material)
    scene["general"]["orthogonalprojection"] = {"width": 200, "height": 100}
    base = json.loads((root / "particles/marker.json").read_text())
    for name, color, gravity in (("moving", "0 255 0", "1 0 0"),
                                  ("control", "255 0 0", "0 0 0")):
        particle = dict(base, initializer=[
            {"name": "lifetimerandom", "min": 1000, "max": 1000},
            {"name": "sizerandom", "min": 8, "max": 8},
            {"name": "colorrandom", "min": color, "max": color},
        ], operator=[{"name": "movement", "gravity": gravity, "drag": 1000000}])
        write_json(root / f"particles/{name}.json", particle)
    scene["objects"] = [
        {"id": i + 1, "name": name, "particle": f"particles/{name}.json", "origin": "80 50 0",
         "instanceoverride": {"rate": 10}}
        for i, name in enumerate(("moving", "control"))]
    image, _ = capture(engine, root, scene, ["--noautomute"])
    centers = []
    for channel in (0, 1):
        points = [(x, y) for y in range(image.height) for x in range(image.width)
                  if image.getpixel((x, y))[channel] > 200]
        assert len(points) >= 8, f"Missing particle channel {channel}"
        centers.append(tuple(sum(p[i] for p in points) / len(points) for i in (0, 1)))
    # Native gravity moves the particle even when drag removes virtually all
    # velocity each frame. Exact distance varies with the rendering frame time.
    passed = centers[1][0] > centers[0][0] + 5 and abs(centers[1][1] - centers[0][1]) <= 1
    print(f"{'PASS' if passed else 'FAIL'} force precedes position: {centers}", flush=True)
    return passed


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        success = run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-particle-movement-") as directory:
            # prepare() creates the directory itself.
            success = run(args.engine.resolve(), Path(directory) / "case")
    raise SystemExit(0 if success else 1)
