"""Particle scene draws preserve alpha used by later HDR color-blend layers."""
import argparse
from pathlib import Path
import tempfile

from test_particle_rotation import prepare
from test_scene_reflections import capture, material, texture, write_json


def fixture(root, hdr=True, particle=True):
    scene = prepare(root, perspective=False)
    (root / "shaders/genericparticle.frag").unlink()
    texture(root / "materials/particle.tex", [(26, 26, 26)])
    particle_material = material("genericparticle", ["particle"])
    particle_material["passes"][0]["blending"] = "additive"
    write_json(root / "materials/marker.json", particle_material)
    for name, blend in (("background", "normal"), ("tint", "translucent")):
        image_material = material("genericimage2", ["util/white"])
        image_material["passes"][0].update(blending=blend, combos={"VERSION": 1})
        write_json(root / f"materials/{name}.json", image_material)
        write_json(root / f"models/{name}.json",
                   {"material": f"materials/{name}.json", "width": 64, "height": 40})
    image = {"origin": "32 20 0", "size": "64 40"}
    scene["objects"] = [dict(image, id=2, image="models/background.json", color=".3 .3 .3")] + (
        scene["objects"] if particle else []) + [
        dict(image, id=3, image="models/tint.json", color=".25 .5 .75", colorBlendMode=30)]
    scene["general"].update(hdr=hdr, bloom=True, bloomstrength=0, bloomhdrstrength=0)
    return scene


def run(engine, root):
    checks = []
    for name, hdr, particle, expected in (("hdr", True, True, (26, 51, 77)),
            ("ldr-control", False, True, (26, 51, 77)),
            ("no-particle-control", True, False, (19, 38, 57))):
        case = root / name
        image, _ = capture(engine, case, fixture(case, hdr, particle),
                           ["--post-processing", "ultra", "--noautomute"])
        actual = image.getpixel((160, 90))
        passed = max(abs(a-b) for a, b in zip(actual, expected)) <= 2
        checks.append(passed)
        print(f"{'PASS' if passed else 'FAIL'} {name}: {actual}, expected {expected}", flush=True)
    case = root / "composition-control"
    scene = fixture(case)
    particle = next(obj for obj in scene["objects"] if "particle" in obj)
    scene["objects"] = [dict(particle, parent=10, origin="0 0 0"),
        {"id": 10, "image": "models/util/composelayer.json", "origin": "32 20 0",
         "size": "64 40", "copybackground": False}]
    image, _ = capture(engine, case, scene, ["--post-processing", "ultra", "--noautomute"])
    actual = image.getpixel((160, 90))
    passed = max(abs(c-26) for c in actual) <= 2
    checks.append(passed)
    print(f"{'PASS' if passed else 'FAIL'} composition-control: {actual}, expected (26, 26, 26)",
          flush=True)
    return all(checks)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        success = run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-particle-alpha-") as directory:
            success = run(args.engine.resolve(), Path(directory))
    raise SystemExit(0 if success else 1)
