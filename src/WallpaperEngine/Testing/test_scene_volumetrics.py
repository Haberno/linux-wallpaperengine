"""Spot-volume GPU regressions; run inside an isolated display under the render lock."""
import argparse
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import time

from PIL import Image, ImageStat
from test_scene_resolution import write_json


def fixture(root, overrides=None):
    root.mkdir(parents=True, exist_ok=True)
    write_json(root / "project.json", {"title": "Spot volume regression", "type": "scene", "file": "scene.json"})
    light = {"id": 2, "name": "beam", "light": "lspot", "origin": "-4 0 0",
             "angles": "0 0 0", "radius": 9, "innercone": 15, "outercone": 25,
             "color": "1 0.5 0.25", "intensity": 8, "density": 1,
             "volumetricsexponent": 1, "castvolumetrics": True, "castshadow": False}
    light.update(overrides or {})
    scene = {"camera": {"eye": "0 0 12", "center": "0 0 0", "up": "0 1 0"},
             "general": {"orthogonalprojection": None, "nearz": 0.1, "farz": 100,
                         "fov": 45, "ambientcolor": "0 0 0", "skylightcolor": "0 0 0",
                         "clearcolor": "0 0 0", "clearenabled": True, "bloom": False, "hdr": False},
             "objects": [light, {"id": 3, "name": "camera", "camera": "default",
                                 "origin": "0 0 12", "angles": "0 0 0", "fov": 45}]}
    write_json(root / "scene.json", scene)
    return scene


def capture(engine, root, extra=()):
    picture = root / "frame.png"
    with tempfile.TemporaryDirectory(prefix="lwe-vol-") as runtime, (root / "engine.log").open("w") as log:
        env = dict(os.environ, WPE_CONTROL_SOCKET=runtime + "/control.sock", WPE_LOG_FILE="off")
        env.pop("WAYLAND_DISPLAY", None)
        process = subprocess.Popen([str(engine), "--window", "0x0x320x180", "--fps", "30",
                                    "--silent", "--noautomute", "--no-fullscreen-pause",
                                    "--screenshot", str(picture), "--screenshot-delay", "30",
                                    *extra, str(root)], env=env, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            deadline = time.monotonic() + 45
            while process.poll() is None and not picture.exists() and time.monotonic() < deadline:
                time.sleep(.1)
            assert picture.exists(), (root / "engine.log").read_text()
            return Image.open(picture).convert("RGB")
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()


def dynamic_light(engine, root):
    scene = fixture(root)
    light = scene["objects"].pop(0)
    light["castshadow"] = True
    script = "let created=false; export function update(value) { if(!created) { created=true; " \
             "thisScene.createLayer(" + json.dumps(light) + "); } return value; }"
    scene["objects"].insert(0, {"id": 1, "name": "runtime light creator", "solid": True,
                              "visible": {"value": False, "script": script}})
    write_json(root / "scene.json", scene)
    return ImageStat.Stat(capture(engine, root)).mean


def hidden_composite(engine, root, hidden_parent=False, volume=True):
    scene = fixture(root, {"castvolumetrics": volume})
    (root / "shaders").mkdir()
    vertex = """
attribute vec3 a_Position;
attribute vec2 a_TexCoord;
uniform mat4 g_ModelViewProjectionMatrix;
varying vec2 v_TexCoord;
void main() {
    v_TexCoord = a_TexCoord;
    gl_Position = mul(vec4(a_Position, 1.0), g_ModelViewProjectionMatrix);
}
"""
    for name, expression in (("source", "vec4(texSample2D(g_Texture1,vec2(0.5)).r > 0.01 ? "
                              "vec3(0,1,0) : vec3(1,0,0),1)"),
                             ("copy", "texSample2D(g_Texture0,v_TexCoord)")):
        (root / f"shaders/{name}.vert").write_text(vertex)
        (root / f"shaders/{name}.frag").write_text("uniform sampler2D g_Texture0;\n"
            "uniform sampler2D g_Texture1;\nvarying vec2 v_TexCoord;\n"
            "void main() { gl_FragColor = " + expression + "; }\n")
    for name, shader, textures in (("source", "source", ["util/white", "_rt_FullFrameBuffer"]),
                                  ("consumer", "copy", ["_rt_imageLayerComposite_10_a"]),
                                  ("black", "copy", ["util/black"])):
        write_json(root / f"materials/{name}.json", {"passes": [{"shader": shader,
                   "textures": textures, "blending": "normal", "depthtest": "disabled",
                   "depthwrite": "disabled", "cullmode": "nocull"}]})
        write_json(root / f"models/{name}.json", {"material": f"materials/{name}.json",
                   "width": 256, "height": 256})
    def layer(id, material):
        return {"id": id, "name": material, "image": f"models/{material}.json", "origin": "0 0 8",
                "size": "256 256", "scale": "0.04 0.04 0.04", "visible": True, "solid": True}
    light, camera = scene["objects"]
    source = layer(10, "source")
    source.update(visible=hidden_parent, parent=5)
    consumer = layer(11, "consumer")
    consumer["dependencies"] = [10]
    # The final black overlay prevents a later volume flush from disguising a
    # missing beam in the hidden source's earlier framebuffer read.
    scene["objects"] = [{"id": 5, "name": "parent", "solid": True, "visible": not hidden_parent},
                        camera, light, source, layer(12, "black"), consumer]
    write_json(root / "scene.json", scene)
    return capture(engine, root).getpixel((160, 90))


def run(engine, root):
    samples = {}
    cases = {"enabled": {}, "disabled": {"castvolumetrics": False}, "density0": {"density": 0},
             "density-half": {"density": .5}, "hidden": {"visible": False},
             "exponent3": {"volumetricsexponent": 3}, "shadow-clear": {"castshadow": True}}
    for name, overrides in cases.items():
        probe = root / name
        fixture(probe, overrides)
        image = capture(engine, probe)
        samples[name] = ImageStat.Stat(image).mean
    checks = []
    def check(name, passed):
        checks.append({"name": name, "passed": bool(passed)})
        print(f"{'PASS' if passed else 'FAIL'} {name}", flush=True)
    beam = samples["enabled"][0]
    check("authored spot volume draws through empty space", beam > 2)
    for name in ("disabled", "density0", "hidden"):
        check(name + " contributes no light", max(samples[name]) == 0)
    check("density linearly scales integrated light", .42 < samples["density-half"][0] / max(beam, .001) < .58)
    check("volumetric exponent controls radial falloff", 0 < samples["exponent3"][0] < beam * .8)
    check("shadowed volume remains visible in an empty shadow atlas", samples["shadow-clear"][0] > 2)
    for before in (False, True):
        probe = root / ("overlay-before" if before else "overlay-after")
        scene = fixture(probe)
        write_json(probe / "models/overlay.json", {"material": "materials/overlay.json", "width": 40, "height": 40})
        write_json(probe / "materials/overlay.json", {"passes": [{"shader": "genericimage4",
                   "textures": ["util/white"], "blending": "normal", "depthwrite": "disabled",
                   "depthtest": "disabled", "cullmode": "nocull"}]})
        scene["objects"].insert(0 if before else 1, {"id": 4, "name": "opaque overlay",
                                  "image": "models/overlay.json", "origin": "0 0 8",
                                  "size": "40 40", "color": "0 0 0"})
        write_json(probe / "scene.json", scene)
        samples[probe.name] = ImageStat.Stat(capture(engine, probe)).mean
        check("ordinary overlay " + ("before" if before else "after") + " light preserves draw order",
              samples[probe.name][0] > 2 if before else max(samples[probe.name]) == 0)
    for msaa in (0, 8):
        probe = root / ("occluded-msaa" + str(msaa))
        scene = fixture(probe)
        write_json(probe / "models/blocker.json", {"material": "materials/blocker.json", "width": 40, "height": 40})
        write_json(probe / "materials/blocker.json", {"passes": [{"shader": "genericimage4",
                   "textures": ["util/white"], "blending": "normal", "depthwrite": "enabled",
                   "depthtest": "enabled", "cullmode": "nocull"}]})
        scene["objects"].insert(0, {"id": 4, "name": "foreground depth blocker",
                                  "image": "models/blocker.json", "origin": "0 0 8",
                                  "size": "40 40", "color": "0 0 0"})
        write_json(probe / "scene.json", scene)
        image = capture(engine, probe, ("--msaa", str(msaa) if msaa else "off"))
        samples[probe.name] = ImageStat.Stat(image).mean
        check(f"foreground geometry occludes volume with MSAA {msaa}", max(samples[probe.name]) <= 1)

    probe = root / "shadow-blocked"
    scene = fixture(probe, {"castshadow": True})
    write_json(probe / "materials/blocker.json", {"passes": [{"shader": "generic3",
               "textures": ["util/black"], "blending": "normal", "depthwrite": "enabled",
               "depthtest": "enabled", "cullmode": "nocull"}]})
    vertices = b"".join(struct.pack("<12f", -1, y, z, -1, 0, 0, 0, 1, 0, 1, 0, 0)
                        for y, z in ((-3, -3), (3, -3), (3, 3), (-3, 3)))
    indices = struct.pack("<6H", 0, 1, 2, 0, 2, 3)
    (probe / "models").mkdir()
    (probe / "models/blocker.mdl").write_bytes(
        b"MDLV0013\0" + struct.pack("<3I", 15, 1, 1) + b"materials/blocker.json\0"
        + struct.pack("<2I", 0, len(vertices)) + vertices + struct.pack("<I", len(indices)) + indices)
    scene["objects"].insert(0, {"id": 4, "name": "light-space blocker", "model": "models/blocker.mdl"})
    write_json(probe / "scene.json", scene)
    image = capture(engine, probe)
    shadow_region = ImageStat.Stat(image.crop((165, 70, 240, 110))).mean[0]
    with Image.open(root / "shadow-clear/frame.png") as clear:
        unshadowed_region = ImageStat.Stat(clear.convert("RGB").crop((165, 70, 240, 110))).mean[0]
    samples["shadow-region"] = [shadow_region, unshadowed_region]
    check("a light-space blocker removes the beam behind it", shadow_region < unshadowed_region * .3)

    probe = root / "camera-inside"
    scene = fixture(probe)
    scene["objects"][-1].update(origin="-2 0 0", angles="0 -1.57079632679 0")
    write_json(probe / "scene.json", scene)
    samples[probe.name] = ImageStat.Stat(capture(engine, probe)).mean
    check("camera inside the cone still receives its volume", samples[probe.name][0] > 2)
    samples["runtime-created"] = dynamic_light(engine, root / "runtime-created")
    check("a runtime-created light without an allocated shadow slot draws safely", samples["runtime-created"][0] > 2)
    for name, parent, volume in (("hidden-composite", False, True),
                                 ("hidden-parent-composite", True, True),
                                 ("hidden-composite-disabled", False, False)):
        samples[name] = hidden_composite(engine, root / name, parent, volume)
        check(name + " reads the scene at the native volume boundary",
              samples[name] == ((0, 255, 0) if volume else (255, 0, 0)))
    (root / "checks.json").write_text(json.dumps({"samples": samples, "checks": checks}, indent=2))
    assert all(c["passed"] for c in checks), checks


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    run(args.engine.resolve(), args.artifacts.resolve())
