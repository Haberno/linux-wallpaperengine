"""Render lighting and blended-depth regressions; requires a desktop and Pillow.

python3 src/WallpaperEngine/Testing/test_scene_rendering.py build/output/linux-wallpaperengine
"""
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time

from PIL import Image


def render(engine, root, scene, material, sample=(160, 160), expected=None):
    for name, value in {
        "project.json": {"title": "Lighting regression", "type": "scene", "file": "scene.json"},
        "scene.json": scene,
        "models/lit.json": {"material": "materials/lit.json", "width": 64, "height": 64},
        "materials/lit.json": {"passes": [material]},
    }.items():
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(value))
    capture = root / "result.png"
    capture.unlink(missing_ok=True)
    env = dict(os.environ, WPE_LOG_FILE=str(root / "engine.log"),
               WPE_CONTROL_SOCKET=str(root / "control.sock"))
    with (root / "output.log").open("w") as log:
        process = subprocess.Popen(
            [str(engine), "--window", "100x100x320x320", "--fps", "15", "--volume", "0",
             "--screenshot", str(capture), "--screenshot-delay", "30", str(root)],
            env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            deadline = time.monotonic() + 30
            while not capture.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.1)
            assert capture.exists(), (root / "output.log").read_text()
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
    pixel = Image.open(capture).convert("RGB").getpixel(sample)
    if expected is None:
        assert min(pixel) > 40, f"{root.name}: expected illuminated center, got {pixel}"
    else:
        assert pixel == expected, f"{root.name}: expected {expected}, got {pixel}"
    print(root.name, pixel)
    return pixel


engine = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="lwe-lighting-") as temporary:
    root = Path(temporary)
    scene = {
        "camera": {"eye": "0 0 1", "center": "0 0 0", "up": "0 1 0"},
        "general": {"orthogonalprojection": {"width": 64, "height": 64},
                    "ambientcolor": "0 0 0", "skylightcolor": "0 0 0", "clearcolor": "0 0 0"},
        "objects": [{"id": 1, "name": "lit plane", "image": "models/lit.json",
                     "origin": "32 32 0", "size": "64 64"},
                    {"id": 2, "name": "sun", "light": "ldirectional",
                     "angles": "0 1.5707963 0", "intensity": 2, "color": "1 1 1"}],
    }
    material = {"shader": "genericimage4", "textures": ["util/white"],
                "combos": {"LIGHTING": 1}, "blending": "normal", "cullmode": "nocull",
                "depthtest": "disabled", "depthwrite": "disabled"}
    render(engine, root / "orthographic", scene, material)

    scene["objects"][1] = {"id": 2, "name": "point", "light": "lpoint",
                           "origin": "32 32 10", "radius": 15,
                           "intensity": 10, "color": "1 1 1"}
    render(engine, root / "orthographic-point", scene, material)

    # An offscreen lighting pass must retain the offset layer's world position.
    # Centering both the image and light would hide a normalized-coordinate bug.
    scene["objects"][0].update(origin="16 32 0", size="24 24")
    scene["objects"][1]["origin"] = "16 32 10"
    bare = render(engine, root / "offset-bare", scene, material, (80, 160))
    scene["objects"][0]["effects"] = [{
        "id": 3, "file": "effects/tint/effect.json",
        "passes": [{"id": 4, "constantshadervalues": {"alpha": 0, "color": "1 1 1"}}],
    }]
    effected = render(engine, root / "offset-effect", scene, material, (80, 160))
    assert max(abs(a - b) for a, b in zip(bare, effected)) <= 3, (bare, effected)
    scene["objects"][0]["effects"][0]["visible"] = False
    hidden = render(engine, root / "offset-disabled-effect", scene, material, (80, 160))
    assert max(abs(a - b) for a, b in zip(bare, hidden)) <= 3, (bare, hidden)
    del scene["objects"][0]["effects"]
    scene["objects"][0].update(origin="32 32 0", size="64 64")

    probe = root / "legacy"
    (probe / "shaders").mkdir(parents=True)
    (probe / "shaders/probe.vert").write_text("""
uniform mat4 g_ModelViewProjectionMatrix;
attribute vec3 a_Position;
void main() { gl_Position = mul(vec4(a_Position, 1.0), g_ModelViewProjectionMatrix); }
""")
    (probe / "shaders/probe.frag").write_text("""
uniform vec3 g_LightsPosition[4];
uniform vec4 g_LightsColorRadius[4];
void main() {
    bool valid = all(equal(g_LightsPosition[0], vec3(1, 2, 3)))
        && all(equal(g_LightsPosition[3], vec3(10, 11, 12)))
        && all(equal(g_LightsColorRadius[3], vec4(1, 0, 0, 5)));
    gl_FragColor = vec4(vec3(valid ? 1.0 : 0.0), 1.0);
}
""")
    scene["objects"] = scene["objects"][:1] + [
        {"id": i + 2, "name": "point", "light": "lpoint",
         "origin": f"{32 + i * 3 + 1} {32 - (i * 3 + 2)} {i * 3 + 3}",
         "radius": i + 2, "intensity": 1, "color": "1 0 0"} for i in range(4)]
    material.update(shader="probe", combos={})
    render(engine, probe, scene, material)

    # A blended foreground must not prevent a later opaque model from drawing.
    # Opaque/coverage foregrounds still write depth, including across model layers.
    probe = root / "depth"
    for directory in ("models", "materials", "shaders"):
        (probe / directory).mkdir(parents=True)
    scene = {
        "camera": {"eye": "0 0 3", "center": "0 0 0", "up": "0 1 0"},
        "general": {"nearz": 0.1, "farz": 100, "fov": 45, "orthogonalprojection": None,
                    "clearcolor": "0 0 0", "transparentsorting": False},
        "objects": [{"id": 1, "name": "near green", "model": "models/green.mdl", "origin": "0 0 1"},
                    {"id": 2, "name": "far red", "model": "models/red.mdl", "origin": "0 0 0"}],
    }
    vertex_shader = """
uniform mat4 g_ModelViewProjectionMatrix;
attribute vec3 a_Position;
void main() { gl_Position = mul(vec4(a_Position, 1.0), g_ModelViewProjectionMatrix); }
"""
    for name, color in (("green", "0, 1, 0, 1"), ("red", "1, 0, 0, 1")):
        (probe / f"shaders/{name}.vert").write_text(vertex_shader)
        (probe / f"shaders/{name}.frag").write_text(f"void main() {{ gl_FragColor = vec4({color}); }}")
        material = {"passes": [{"shader": name, "blending": "normal", "cullmode": "nocull",
                                "depthtest": "enabled", "depthwrite": "enabled"}]}
        (probe / f"materials/{name}.json").write_text(json.dumps(material))
        # Minimal MDLV0013 quad: position, normal, tangent and UV, 16-bit indices.
        vertices = b"".join(struct.pack("<12f", x, y, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0)
                            for x, y in ((-1, -1), (1, -1), (1, 1), (-1, 1)))
        indices = struct.pack("<6H", 0, 1, 2, 0, 2, 3)
        model = (b"MDLV0013\0" + struct.pack("<3I", 15, 1, 1)
                 + f"materials/{name}.json".encode() + b"\0"
                 + struct.pack("<2I", 0, len(vertices)) + vertices
                 + struct.pack("<I", len(indices)) + indices)
        (probe / f"models/{name}.mdl").write_bytes(model)
    material_path = probe / "materials/green.json"
    material = json.loads(material_path.read_text())
    for blend, write, expected in (("additive", "enabled", (255, 0, 0)),
                                    ("translucent", "enabled", (255, 0, 0)),
                                    ("normal", "enabled", (0, 255, 0)),
                                    ("alphatocoverage", "enabled", (0, 255, 0)),
                                    ("normal", "disabled", (255, 0, 0))):
        material["passes"][0].update(blending=blend, depthwrite=write)
        material_path.write_text(json.dumps(material))
        print("Depth policy:", blend, write)
        render(engine, probe, scene, {}, expected=expected)
print("Scene lighting, effect transforms, legacy uniforms and blended depth passed")
