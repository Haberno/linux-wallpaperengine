# CLion setup

Open the repository directory in CLion and select the `debug` or `release`
CMake configure preset. The presets use the host toolchain and Unix Makefiles;
install the dependencies listed in README.md before configuring.

| Preset | Build directory | Executable |
| --- | --- | --- |
| `debug` | `build/` | `build/output/linux-wallpaperengine` |
| `release` | `build-release/` | `build-release/output/linux-wallpaperengine` |

For a run/debug configuration, select the `linux-wallpaperengine` target, use
its `output/` directory as the working directory, and supply a wallpaper path
in the program arguments. For example:

```text
--window 0x0x1280x720 --silent /absolute/path/to/workshop/wallpaper
```

Use the Debug preset for source breakpoints. IDE run configurations are local
under `.idea/` and are not required by the presets. IntelliJ IDEA needs C/C++
and CMake support before it can use this workflow.

The equivalent terminal commands run from the repository root:

```bash
cmake --preset debug
cmake --build --preset debug --parallel 8

cmake --preset release
cmake --build --preset release --parallel 8
```

Enable the regression suite when configuring a test build:

```bash
cmake --preset debug -DBUILD_TESTING=ON
cmake --build --preset debug --parallel 8
./build/output/tests
```
