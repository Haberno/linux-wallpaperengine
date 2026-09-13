"""SceneScript device and screen queries through the actual scene runtime.

Run on an isolated display under /tmp/lwe-render-fix-build.lock. Requires Pillow.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

from PIL import Image

from test_scene_reflections import material, prepare, write_json


FRAGMENT = """uniform vec3 g_Color;
void main() { gl_FragColor = vec4(g_Color, 1); }
"""


def fixture(root, width, height):
    scene = prepare(root, FRAGMENT)
    write_json(root / "materials/probe.json", material("probe", ["single"]))
    # Calls made at module scope and through inherited/detached methods must
    # retain the owning scene. Canvas dimensions intentionally differ from output.
    script = """
const desktop = engine.isDesktopDevice;
const wallpaper = engine.isWallpaper;
const portrait = engine.isPortrait;
const landscape = engine.isLandscape;
const initial = engine.screenResolution;
const validDevice = desktop() === true && wallpaper() === true
    && engine.isMobileDevice() === false && engine.isScreensaver() === false
    && engine.isRunningInEditor() === false
    && initial.x === WIDTH && initial.y === HEIGHT
    && portrait() === PORTRAIT && landscape() === LANDSCAPE;
export function update(value) {
    const screen = engine.screenResolution;
    const canvas = engine.canvasSize;
    const valid = validDevice && screen.x === WIDTH && screen.y === HEIGHT
        && canvas.x === 64 && canvas.y === 40
        && portrait() === PORTRAIT && landscape() === LANDSCAPE;
    return valid ? new Vec3(0, 1, 0) : new Vec3(1, 0, 0);
}
""".replace("WIDTH", str(width)).replace("HEIGHT", str(height))
    script = script.replace("PORTRAIT", str(height > width).lower())
    script = script.replace("LANDSCAPE", str(width >= height).lower())
    scene["objects"][0]["color"] = {"value": "0 0 1", "script": script}
    scene["general"]["bloomstrength"] = {"value": 0, "script": script.replace(
        "return valid ? new Vec3(0, 1, 0) : new Vec3(1, 0, 0);",
        "if (!valid) throw new Error('Invalid initial screen queries'); return 0;")}
    write_json(root / "scene.json", scene)


def run(engine, root):
    display, host = os.environ.get("DISPLAY"), os.environ.get("LWE_HOST_DISPLAY")
    assert display and host is not None and display != host, "A distinct private DISPLAY and LWE_HOST_DISPLAY are required"
    results = []
    for name, width, height in (("landscape", 320, 180), ("portrait", 180, 320), ("square", 240, 240)):
        case = root / name
        fixture(case, width, height)
        picture = case / "result.png"
        with tempfile.TemporaryDirectory(prefix="lwe-screen-sock-") as runtime, (case / "output.log").open("w") as log:
            env = dict(os.environ, WPE_LOG_FILE=str(case / "engine.log"),
                       WPE_CONTROL_SOCKET=str(Path(runtime) / "control.sock"))
            env.pop("WAYLAND_DISPLAY", None)
            process = subprocess.Popen([
                str(engine), "--window", f"0x0x{width}x{height}", "--scaling", "stretch",
                "--fps", "15", "--silent", "--no-audio-processing", "--screenshot", str(picture),
                "--screenshot-delay", "15", str(case)], env=env, stdout=log,
                stderr=subprocess.STDOUT, start_new_session=True)
            try:
                deadline = time.monotonic() + 30
                while not picture.exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(.1)
                assert picture.exists(), (case / "output.log").read_text()
                with Image.open(picture) as image:
                    actual = image.convert("RGB").getpixel((image.width // 2, image.height // 2))
                passed = actual == (0, 255, 0)
                results.append(dict(name=name, actual=actual, passed=passed))
                print("PASS" if passed else "FAIL", results[-1], flush=True)
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGINT)
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()
        results[-1]["exit"] = process.returncode
        results[-1]["script_error"] = "ScriptEngine [" in (case / "output.log").read_text()
    (root / "checks.json").write_text(json.dumps(results, indent=2))
    assert all(item["passed"] and item["exit"] == 0 and not item["script_error"] for item in results), results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-script-screen-") as directory:
            run(args.engine.resolve(), Path(directory))
