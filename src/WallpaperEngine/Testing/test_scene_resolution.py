"""Output-resolution scene regressions; requires a desktop and Pillow.

flock /tmp/lwe-render-fix-build.lock python3 \
    src/WallpaperEngine/Testing/test_scene_resolution.py build/output/linux-wallpaperengine
Use --artifacts /tmp/name to retain scenes, captures, socket replies and logs.
"""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import tempfile
import time

from PIL import Image


VERTEX = """
uniform mat4 g_ModelViewProjectionMatrix;
attribute vec3 a_Position;
attribute vec2 a_TexCoord;
varying vec2 v_TexCoord;
void main() {
    gl_Position = mul(vec4(a_Position, 1.0), g_ModelViewProjectionMatrix);
    v_TexCoord = a_TexCoord;
}
"""
FRAGMENT = """
uniform sampler2D g_Texture0;
uniform vec3 g_Color;
varying vec2 v_TexCoord;
void main() { gl_FragColor = texSample2D(g_Texture0, v_TexCoord) * vec4(g_Color, 1.0); }
"""
IDENTITY = """
uniform sampler2D g_Texture0;
uniform vec4 g_Texture0Resolution;
varying vec2 v_TexCoord;
void main() {
    bool sourceSize = all(equal(g_Texture0Resolution, vec4(8.0)));
    gl_FragColor = sourceSize ? texSample2D(g_Texture0, v_TexCoord) : vec4(1, 0, 1, 1);
}
"""
RED, BLUE, GREEN, BLACK = (255, 0, 0), (0, 0, 255), (0, 255, 0), (0, 0, 0)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value))


def layer(identifier, origin, size, **extra):
    return dict(id=identifier, name=f"probe {identifier}", image=f"models/{identifier}.json",
                origin=origin, size=size, **extra)


def render(engine, root, mode, objects, textures, effect=False, check_source_size=True):
    root.mkdir(parents=True)
    write_json(root / "project.json", {"title": "Scene resolution regression", "type": "scene",
                                       "file": "scene.json"})
    write_json(root / "scene.json", {
        "camera": {"eye": "0 0 1", "center": "0 0 0", "up": "0 1 0"},
        "general": {"orthogonalprojection": {"width": 64, "height": 40},
                    "clearcolor": "0 0 0", "clearenabled": True, "bloom": False},
        "objects": objects,
    })
    (root / "shaders").mkdir()
    (root / "shaders/probe.vert").write_text(VERTEX)
    (root / "shaders/probe.frag").write_text(FRAGMENT)
    for identifier, texture in textures.items():
        write_json(root / f"models/{identifier}.json", {
            "material": f"materials/{identifier}.json", "width": texture.width, "height": texture.height})
        write_json(root / f"materials/{identifier}.json", {"passes": [{
            "shader": "probe", "textures": [str(identifier)], "blending": "normal",
            "cullmode": "nocull", "depthtest": "disabled", "depthwrite": "disabled"}]})
        texture.save(root / f"materials/{identifier}.png")
        write_json(root / f"materials/{identifier}.tex-json", {
            "format": "rgba8888", "nointerpolation": True, "nomip": True, "clampuvs": True})
    if effect:
        (root / "shaders/identity.vert").write_text(VERTEX)
        (root / "shaders/identity.frag").write_text(IDENTITY)
        write_json(root / "effects/identity/effect.json", {
            "passes": [{"material": "materials/identity.json"}]})
        write_json(root / "materials/identity.json", {"passes": [{
            "shader": "identity" if check_source_size else "passthrough", "combos": {"TRANSFORM": 1},
            "blending": "normal", "cullmode": "nocull",
            "depthtest": "disabled", "depthwrite": "disabled"}]})

    capture, control = root / "result.png", root / "control.sock"
    env = dict(os.environ, WPE_LOG_FILE=str(root / "engine.log"), WPE_CONTROL_SOCKET=str(control))
    with (root / "output.log").open("w") as log:
        process = subprocess.Popen([
            str(engine), "--window", "100x100x320x180", "--scaling", mode, "--clamp", "border",
            "--fps", "15", "--volume", "0", "--screenshot", str(capture),
            "--screenshot-delay", "30", str(root)], env=env, stdout=log,
            stderr=subprocess.STDOUT, start_new_session=True)
        try:
            deadline = time.monotonic() + 30
            while not capture.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.1)
            assert capture.exists(), (root / "output.log").read_text()
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(3)
                client.connect(str(control))
                client.sendall(b"fbostats\n")
                chunks = []
                while chunk := client.recv(65536):
                    chunks.append(chunk)
            stats = b"".join(chunks).decode()
            (root / "fbostats.txt").write_text(stats)
            with Image.open(capture) as image:
                result = image.convert("RGB")
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
    return result, stats


def run(engine, root):
    failures = []

    def check(name, actual, expected):
        passed = actual == expected
        print(f"{'PASS' if passed else 'FAIL'} {name}: {actual!r} (expected {expected!r})", flush=True)
        if not passed:
            failures.append(name)

    # Literal border/interior markers catch double cropping and lost fit margins.
    framed = Image.new("RGB", (64, 40), GREEN)
    for y in (0, 1, 38, 39):
        for x in range(64):
            framed.putpixel((x, y), RED)
    for mode, samples in {
        "fill": [((160, 2), GREEN), ((2, 90), GREEN)],
        "fit": [((8, 90), BLACK), ((20, 90), GREEN), ((160, 2), RED)],
        "stretch": [((2, 90), GREEN), ((160, 2), RED)],
    }.items():
        image, stats = render(engine, root / mode, mode,
                              [layer(1, "32 20 0", "64 40")], {1: framed})
        match = re.search(r"(?:^|[ ;])_rt_FullFrameBuffer@(\d+)x(\d+)x", stats)
        dimensions = tuple(map(int, match.groups())) if match else stats
        check(f"{mode}: main FBO uses output pixels", dimensions, (320, 180))
        for point, expected in samples:
            check(f"{mode}: framing at {point}", image.getpixel(point), expected)

    # Fullscreen stack effects and isolated composition subtrees use scene
    # pixels, unlike the ordinary 8x8 source-image effect checked below.
    for composition, mode in ((False, "stretch"), (True, "stretch"), (True, "fill"), (True, "fit")):
        name = f"composition-{mode}" if composition else "fullscreen"
        parent = {"id": 10, "name": name, "origin": "32 20 0", "size": "64 40",
                  "image": "models/util/composelayer.json" if composition
                  else "models/util/fullscreenlayer.json",
                  "effects": [{"id": 11, "file": "effects/identity/effect.json"}]}
        background = layer(1, "0 0 0" if composition else "32 20 0", "64 40")
        if composition:
            background["parent"] = 10
        image, stats = render(engine, root / name, mode, [background, parent],
                              {1: framed}, effect=True, check_source_size=False)
        targets = ["_rt_imageLayerComposite_10_a", "_rt_imageLayerComposite_10_b"]
        if composition:
            targets.append("_rt_compositionLayer_10")
        for target in targets:
            match = re.search(re.escape(target) + r"@(\d+)x(\d+)x", stats)
            dimensions = tuple(map(int, match.groups())) if match else stats
            check(f"{name}: {target} uses scene pixels", dimensions, (320, 180))
        check(f"{name}: identity retains center and border",
              [image.getpixel((160, 90)), image.getpixel((160, 2))],
              [GREEN, GREEN if mode == "fill" else RED])
        if mode == "fit":
            check("composition-fit: identity retains fit margin", image.getpixel((8, 90)), BLACK)

    # Eight source columns span eight output pixels under stretch. An authored
    # 64-wide scene FBO cannot preserve them, even if screenshot scaling is nearest.
    stripes = Image.new("RGB", (8, 8))
    for y in range(8):
        for x in range(8):
            stripes.putpixel((x, y), RED if x % 2 == 0 else BLUE)
    images = []
    for index, x in enumerate((16, 16.2)):
        image, _ = render(engine, root / f"fractional-{index}", "stretch",
                          [layer(1, f"{x} 20 0", "1.6 2")], {1: stripes})
        images.append(image)
    expected = [RED, BLUE] * 4
    check("nearest source texels retain one-output-pixel edges",
          [images[0].getpixel((x, 90)) for x in range(76, 84)], expected)
    check("0.2 canvas-unit movement shifts exactly one output pixel",
          [images[1].getpixel((x, 90)) for x in range(77, 85)], expected)
    bounds = []
    for image in images:
        occupied = [x for x in range(320) if image.getpixel((x, 90)) != BLACK]
        bounds.append((min(occupied), max(occupied)) if occupied else None)
    check("fractional layer geometry does not snap to authored pixels", bounds, [(76, 83), (77, 84)])

    # Without an authored size, composition geometry still uses canvas units.
    # Accidentally using the 320x180 source texture as logical size can preserve
    # broad framing while erasing these fine columns in the intermediate pass.
    parent = {"id": 10, "name": "implicit composition", "origin": "32 20 0",
              "image": "models/util/composelayer.json",
              "effects": [{"id": 11, "file": "effects/identity/effect.json"}]}
    image, _ = render(engine, root / "composition-implicit", "stretch",
                      [layer(1, "-16 0 0", "1.6 2", parent=10), parent],
                      {1: stripes}, effect=True, check_source_size=False)
    check("omitted composition size preserves one-output-pixel detail",
          [image.getpixel((x, 90)) for x in range(76, 84)], expected)

    # Growing scene storage must not grow a source effect or change SceneScript units.
    canvas_script = """export function update(value) {
        const size = engine.canvasSize;
        return size.x === 64 && size.y === 40 ? new Vec3(0, 1, 0) : new Vec3(1, 0, 0);
    }"""
    image, _ = render(engine, root / "logical-controls", "stretch", [
        layer(1, "16 20 0", "8 8", effects=[{"id": 3, "file": "effects/identity/effect.json"}]),
        layer(2, "48 20 0", "8 8", color={"value": "1 1 1", "script": canvas_script}),
    ], {1: stripes, 2: Image.new("RGB", (8, 8), "white")}, effect=True)
    check("identity effect preserves its 8x8 source and texture content",
          [image.getpixel((62, 90)), image.getpixel((67, 90))], [RED, BLUE])
    check("SceneScript canvasSize stays authored 64x40", image.getpixel((240, 90)), GREEN)
    if failures:
        raise AssertionError(f"{len(failures)} scene-resolution regressions failed: {', '.join(failures)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-resolution-") as directory:
            run(args.engine.resolve(), Path(directory))
