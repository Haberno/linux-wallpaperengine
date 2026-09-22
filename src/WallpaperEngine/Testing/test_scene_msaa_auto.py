"""Automatic scene MSAA policy regression; requires a graphics context.

python test_scene_msaa_auto.py /absolute/path/to/engine --artifacts /absolute/new/dir
Run under the repository render/build lock once GPU ownership is available.
Uses existing scene-MSAA fixtures and screenshot/control-socket capture helpers.
Requires hardware supporting each requested count; CFBO tests cover fallback.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import sys

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "src/WallpaperEngine/Testing"))
from test_scene_msaa import fixture
from test_scene_reflections import capture, texture
from test_particle_rotation import prepare as particle_fixture


def run(engine, root, samples=(1, 2, 4, 8), projections=(False, True),
        kinds=("model", "hidden-model", "empty", "image", "image-bloom", "puppet", "particle")):
    results = []
    for requested in samples:
        for perspective in projections:
            for kind in kinds:
                name = f"{requested}-{'perspective' if perspective else 'ortho'}-{kind}"
                probe = root / name
                scene = (particle_fixture(probe, perspective=perspective) if kind == "particle"
                         else fixture(probe, bloom=kind == "image-bloom", orthographic=not perspective))
                model = scene["objects"][0]
                if kind == "hidden-model":
                    model["visible"] = False
                elif kind == "empty":
                    scene["objects"] = []
                elif kind in ("image", "image-bloom", "puppet"):
                    texture(probe / "materials/white.tex", [(255, 255, 255)])
                    material = json.loads((probe / "materials/edge.json").read_text())
                    material["passes"][0]["textures"] = ["white"]
                    (probe / "materials/edge.json").write_text(json.dumps(material))
                    description = {"material": "materials/edge.json", "width": 2, "height": 2}
                    if kind == "puppet":
                        vertices = b"".join(struct.pack("<3f4I6f", x, y, 0, 0, 0, 0, 0,
                                                       1, 0, 0, 0, u, v)
                                            for x, y, u, v in ((-1, -1, 0, 0), (1, -1, 1, 0), (0, 1, .5, 1)))
                        (probe / "models/puppet.mdl").write_bytes(
                            b"MDLV0013\0" + struct.pack("<3I", 0, 1, 1) + b"materials/edge.json\0"
                            + struct.pack("<2I", 0, len(vertices)) + vertices + struct.pack("<I3H", 6, 0, 1, 2))
                        description["puppet"] = "models/puppet.mdl"
                    (probe / "models/image.json").write_text(json.dumps(description))
                    del model["model"]
                    model["image"] = "models/image.json"
                    model["size"] = "2 2"
                options = ("--msaa", "off" if requested == 1 else str(requested))
                capture(engine, probe, scene, options)
                log = (probe / "engine.log").read_text()
                assert "Failed to setup object" not in log, name
                if kind == "puppet":
                    assert "Loaded puppet mesh models/puppet.mdl" in log, name
                match = re.search(r"Scene MSAA: requested=(\d+) actual=(\d+)", log)
                assert match, name
                configured, actual = map(int, match.groups())
                assert configured == requested, (name, configured)
                expected = requested if kind in ("model", "hidden-model") else 1
                assert actual == expected, (name, actual, expected)
                result = dict(name=name, configured=configured, actual=actual, expected=expected)
                results.append(result)
                (root / "checks.json").write_text(json.dumps(results, indent=2))
                print(result, flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    args = parser.parse_args()
    run(args.engine.resolve(), args.artifacts.resolve())
