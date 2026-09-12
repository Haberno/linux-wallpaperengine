"""Particle instance brightness is applied once in HDR and ignored in LDR."""
import argparse
import json
from pathlib import Path
import tempfile

from test_particle_rotation import prepare
from test_scene_reflections import capture, write_json


def run(engine, root):
    results = []
    for name, hdr, dynamic in (("ldr-control", False, False), ("hdr", True, False),
                                ("hdr-live", True, True), ("hdr-instance", True, False)):
        case = root / name
        scene = prepare(case, perspective=False)
        (case / "shaders/genericparticle.frag").unlink()
        material = json.loads((case / "materials/marker.json").read_text())
        material["passes"][0]["textures"] = ["util/white"]
        write_json(case / "materials/marker.json", material)
        scene["general"].update(hdr=hdr, bloom=True, bloomhdrstrength=0, bloomstrength=0)
        brightness = {"value": 1, "script": "export function update() { return .25; }"} if dynamic else .25
        scene["objects"] = [
            dict(scene["objects"][0], id=1, origin="16 20 0", instanceoverride={"brightness": 1}),
            dict(scene["objects"][0], id=2, origin="48 20 0", instanceoverride={"brightness": brightness}),
        ]
        if name == "hdr-instance":
            scene["objects"][1]["instanceoverride"]["brightness"] = 1
            scene["objects"][1]["visible"] = {
                "value": True,
                "script": "export function update() { thisLayer.instance.brightness = .25; return true; }",
            }
        image, _ = capture(engine, case, scene, ["--post-processing", "ultra", "--noautomute"])
        actual = [image.getpixel((x, 90)) for x in (80, 240)]
        expected = [(255, 255, 255), (64, 64, 64) if hdr else (255, 255, 255)]
        passed = all(max(abs(a-b) for a, b in zip(pixel, want)) <= 1
                     for pixel, want in zip(actual, expected))
        results.append(passed)
        print(f"{'PASS' if passed else 'FAIL'} {name}: {actual}, expected {expected}", flush=True)
    return all(results)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        success = run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-particle-brightness-") as directory:
            success = run(args.engine.resolve(), Path(directory))
    raise SystemExit(0 if success else 1)
