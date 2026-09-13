"""Exercise resizeScreen on an owned X11 window on a private display.

Run under /tmp/lwe-render-fix-build.lock with DISPLAY set to the private server
and LWE_HOST_DISPLAY set to the original display. Requires the screen fixture.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

from test_scene_script_screen import fixture as screen_fixture


MARKER = "LWE_RESIZE "
SIZES = [(320, 180), (180, 320), (240, 160), (220, 220)]


class X11:
    def __init__(self):
        self.lib = ctypes.CDLL("libX11.so.6")
        x = self.lib
        pointer = ctypes.POINTER
        # A disappearing startup window must not make Xlib exit the test before
        # its finally block can stop the owned renderer process.
        self.error_handler = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)(
            lambda display, event: 0)
        x.XSetErrorHandler.argtypes = [ctypes.c_void_p]
        x.XSetErrorHandler(self.error_handler)
        x.XOpenDisplay.argtypes = [ctypes.c_char_p]
        x.XOpenDisplay.restype = ctypes.c_void_p
        x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        x.XDefaultRootWindow.restype = ctypes.c_ulong
        x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                pointer(ctypes.c_ulong), pointer(ctypes.c_ulong),
                                pointer(pointer(ctypes.c_ulong)), pointer(ctypes.c_uint)]
        x.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        x.XInternAtom.restype = ctypes.c_ulong
        x.XGetWindowProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong,
                                        ctypes.c_long, ctypes.c_long, ctypes.c_int, ctypes.c_ulong,
                                        pointer(ctypes.c_ulong), pointer(ctypes.c_int),
                                        pointer(ctypes.c_ulong), pointer(ctypes.c_ulong),
                                        pointer(ctypes.c_void_p)]
        x.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, pointer(ctypes.c_void_p)]
        x.XGetGeometry.argtypes = [ctypes.c_void_p, ctypes.c_ulong, pointer(ctypes.c_ulong),
                                  pointer(ctypes.c_int), pointer(ctypes.c_int),
                                  *[pointer(ctypes.c_uint)] * 4]
        x.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint, ctypes.c_uint]
        x.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        x.XFree.argtypes = [ctypes.c_void_p]
        x.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.display = x.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError("Private X display unavailable")
        self.root = x.XDefaultRootWindow(self.display)
        self.pid_atom = x.XInternAtom(self.display, b"_NET_WM_PID", False)
        self.windows = {}

    def owner(self, window):
        kind, count, remaining = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.c_ulong()
        bits, data = ctypes.c_int(), ctypes.c_void_p()
        self.lib.XGetWindowProperty(self.display, window, self.pid_atom, 0, 1, False, 0,
                                   ctypes.byref(kind), ctypes.byref(bits), ctypes.byref(count),
                                   ctypes.byref(remaining), ctypes.byref(data))
        try:
            if bits.value == 32 and count.value == 1 and data.value:
                return ctypes.cast(data, ctypes.POINTER(ctypes.c_ulong))[0]
        finally:
            if data.value:
                self.lib.XFree(data)
        return None

    def title(self, window):
        name = ctypes.c_void_p()
        self.lib.XFetchName(self.display, window, ctypes.byref(name))
        try:
            return ctypes.string_at(name).decode(errors="replace") if name.value else ""
        finally:
            if name.value:
                self.lib.XFree(name)

    def size(self, window):
        root, x, y = ctypes.c_ulong(), ctypes.c_int(), ctypes.c_int()
        width, height, border, depth = [ctypes.c_uint() for _ in range(4)]
        ok = self.lib.XGetGeometry(self.display, window, ctypes.byref(root), ctypes.byref(x),
                                  ctypes.byref(y), ctypes.byref(width), ctypes.byref(height),
                                  ctypes.byref(border), ctypes.byref(depth))
        return (width.value, height.value) if ok else None

    def find(self, pid, window=None):
        window = self.root if window is None else window
        if self.owner(window) == pid:
            title, size = self.title(window), self.size(window)
            self.windows[hex(window)] = dict(pid=pid, title=title, size=size)
            # GLFW also owns small helper windows with the same PID.
            if title == "wallpaperengine" and size == SIZES[0]:
                return window
        root, parent, count = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.c_uint()
        data = ctypes.POINTER(ctypes.c_ulong)()
        self.lib.XQueryTree(self.display, window, ctypes.byref(root), ctypes.byref(parent),
                            ctypes.byref(data), ctypes.byref(count))
        children = [data[index] for index in range(count.value)]
        if data:
            self.lib.XFree(data)
        for child in children:
            found = self.find(pid, child)
            if found is not None:
                return found
        return None

    def resize(self, window, pid, size):
        assert self.owner(window) == pid, "Resize target must belong to the test process"
        self.lib.XResizeWindow(self.display, window, *size)
        self.lib.XSync(self.display, False)

    def close(self):
        self.lib.XCloseDisplay(self.display)


def script(name, extra=""):
    return """
const initialScreen = engine.screenResolution;
const initialPortrait = engine.isPortrait(), initialLandscape = engine.isLandscape();
let events = 0, frames = 0, previous = '', lastEvent = null;
function report(kind, size) {
    const screen = engine.screenResolution;
    console.log('LWE_RESIZE ' + JSON.stringify({name: NAME, kind: kind,
        events: events, size: [size.x, size.y], screen: [screen.x, screen.y],
        vector: size instanceof Vec2, canvas: [engine.canvasSize.x, engine.canvasSize.y],
        portrait: engine.isPortrait(), landscape: engine.isLandscape(),
        initialScreen: [initialScreen.x, initialScreen.y],
        initialPortrait: initialPortrait, initialLandscape: initialLandscape,
        layer: thisLayer === undefined ? null : thisLayer.name,
        lastEvent: lastEvent}));
}
export function init() { report('init', engine.screenResolution); }
export function resizeScreen(size) {
    events++;
    lastEvent = [size.x, size.y];
    report('resize', size);
    EXTRA
}
export function update(value) {
    const size = engine.screenResolution, key = size.x + 'x' + size.y;
    if (key !== previous) {
        previous = key;
        frames = 0;
        report('update', size);
    }
    if (++frames === 8) report('stable', size);
    return value;
}
""".replace("NAME", json.dumps(name)).replace("EXTRA", extra)


def fixture(root):
    screen_fixture(root, *SIZES[0])
    scene = json.loads((root / "scene.json").read_text())
    child = dict(name="child", image="models/probe.json", origin="32 20 0",
                 color=dict(value="1 1 1", script=script("child", "thisScene.destroyLayer(thisLayer);")))
    # The new layer must not receive the resize that created it. Its later
    # callback removes itself; deferred destruction must preserve traversal.
    extra = "if (events === 1) thisScene.createLayer(" + json.dumps(child) + ");"
    scene["objects"][0]["color"] = dict(value="0 1 0", script=script("layer", extra))
    # Throw after reporting to prove the frame update and later resize events
    # survive a handler exception, including scripts without a thisLayer.
    scene["general"]["bloomstrength"] = dict(value=0, script=script(
        "scene", "if (events === 1) throw Error('expected resize exception');"))
    (root / "scene.json").write_text(json.dumps(scene))


def observations(log):
    return [json.loads(line.split(MARKER, 1)[1]) for line in log.read_text().splitlines()
            if MARKER in line]


def wait_for(process, predicate, log, label="runtime observation"):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and process.poll() is None:
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    raise AssertionError("Timed out waiting for " + label + ":\n" + log.read_text())


def verify(records):
    for record in records:
        width, height = record["size"]
        assert record["screen"] == [width, height] and record["vector"], record
        assert record["canvas"] == [64, 40], record
        assert record["portrait"] == (height > width), record
        assert record["landscape"] == (width >= height), record
        assert record["layer"] == {"layer": "probe", "scene": None, "child": "child"}[record["name"]], record
    for name in ("layer", "scene", "child"):
        selected = [record for record in records if record["name"] == name]
        initial = SIZES[1] if name == "child" else SIZES[0]
        assert [(item["size"], item["events"]) for item in selected if item["kind"] == "init"] == [(list(initial), 0)], selected
        expected = SIZES[2:3] if name == "child" else SIZES[1:]
        resized = [item for item in selected if item["kind"] == "resize"]
        assert [item["size"] for item in resized] == list(map(list, expected)), selected
        assert [item["events"] for item in resized] == list(range(1, len(expected) + 1)), selected
        updates = [item for item in selected if item["kind"] == "update"]
        assert [item["size"] for item in updates] == list(map(list, [initial, *expected])), selected
        for index, item in enumerate(updates):
            assert item["events"] == index, item
            assert item["lastEvent"] == (None if index == 0 else item["size"]), item
        for item in selected:
            assert item["initialScreen"] == list(initial), item
            assert item["initialPortrait"] == (initial[1] > initial[0]), item
            assert item["initialLandscape"] == (initial[0] >= initial[1]), item


def run(engine, root):
    display, host = os.environ.get("DISPLAY"), os.environ.get("LWE_HOST_DISPLAY")
    assert display and host is not None and display != host, "A distinct private DISPLAY and LWE_HOST_DISPLAY are required"
    root.mkdir(parents=True, exist_ok=True)
    scene = root / "scene"
    fixture(scene)
    # Artifact paths may exceed the Unix-domain socket path limit.
    control = tempfile.TemporaryDirectory(prefix="lwe-resize-sock-")
    env = dict(os.environ, WPE_LOG_FILE=str(root / "engine.log"),
               WPE_CONTROL_SOCKET=str(Path(control.name) / "control.sock"))
    env.pop("WAYLAND_DISPLAY", None)
    x11, process = X11(), None
    resizes = []
    log_path = root / "output.log"
    try:
        with log_path.open("w") as log:
            process = subprocess.Popen([str(engine), "--window", "0x0x320x180", "--scaling", "stretch",
                                        "--fps", "15", "--silent", "--no-audio-processing", str(scene)],
                                       env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            window = wait_for(process, lambda: x11.find(process.pid), log_path,
                              "owned wallpaperengine window at 320x180; see windows.json")
            for size in SIZES:
                x11.resize(window, process.pid, size)
                resize = dict(window=hex(window), requested=size)
                resizes.append(resize)
                def configured():
                    resize["actual"] = x11.size(window)
                    return resize["actual"] == size
                wait_for(process, configured, log_path,
                         f"X11 geometry {size}; see windows.json")
                wait_for(process, lambda: all(any(item["name"] == name and item["kind"] == "stable"
                         and item["size"] == list(size) for item in observations(log_path))
                         for name in ("layer", "scene")), log_path)
                # A redundant configure must not emit another event.
                x11.resize(window, process.pid, size)
                time.sleep(.2)
    finally:
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGINT)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        x11.close()
        control.cleanup()
        (root / "windows.json").write_text(json.dumps(dict(
            display=display, windows=x11.windows, resizes=resizes), indent=2))
    records = observations(log_path)
    (root / "observations.json").write_text(json.dumps(records, indent=2))
    verify(records)
    assert process.returncode == 0, log_path.read_text()
    print("PASS resizeScreen startup, resize, Vec2, ordering, exception, dynamic layer lifecycle", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    if args.artifacts:
        run(args.engine.resolve(), args.artifacts.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="lwe-script-resize-") as directory:
            run(args.engine.resolve(), Path(directory))
