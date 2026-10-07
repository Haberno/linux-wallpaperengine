# Windows HDR frame-server measurement

This finite diagnostic reads Windows MediaEngine frames into a shared DXGI
RGBA16F texture. It records float pixels and available source/display metadata
without assuming Microsoft's tone-mapping curve. **Windows HDR parity remains
unverified.** A successful capture is a measurement of this API path on the
recorded machine, not proof of original Wallpaper Engine or physical HDR parity.

No proprietary DLLs, executable, clips, or results are distributed here. Nothing
installs software, changes display settings, or launches Wallpaper Engine.

## Build and generate inputs

Use an existing Python3 installation. On Windows, use an x64 Native Tools
Command Prompt with an existing MSVC/Windows10-or-later SDK:

```bat
cd tools\parity\windows-hdr
build-windows.cmd
```

Alternatively, `bash build-cross.sh` uses already installed clang/lld,
llvm-readobj, ripgrep, and the Wine SDK-compatible headers/import libraries at
the paths in that script. It compiles a Windows PE; it never invokes Wine or
executes the result. The Windows build recipe has source review only.

Both recipes require a new `build` directory and create
`build/build-manifest.json` only after successful compilation. This binds the
executable hash to the exact source/runner/analyzer/reference inputs, checks that
those inputs did not change during the build, and records the build-log hash.
It is local provenance, **not a signed attestation** that a binary is trustworthy.
Build from a reviewed checkout; do not manufacture a manifest for an unrelated
binary. A rebuild needs its own manifest. Preserve or move an old build before
starting another; the recipes never overwrite it.

Generate the three calibrated clips on a machine with existing FFmpeg/ffprobe
and libx265:

```bash
python prepare_fixtures.py fixtures
```

The generator creates a new directory, verifies every decoded sample and stream
tag, then writes `fixtures/fixture-manifest.json`. It retains320×180,10fps,
40frames, ten neutral bands, a checker strip and four temporal marker phases.
The SDR/PQ/HLG companions share encoded values, **not equal physical light**.
Encoder versions may produce different file bytes; each validated file gets its
own hash. Windows HEVC admission is untested; the lossless Main10 recipe may
declare level255. Do not silently re-encode a rejected fixture or install a codec
as part of this measurement.

For Windows runs, copy this source directory, the matching `build` directory,
and the generated fixture directory together. Preserve file bytes/line endings:
the runner validates their identities against the local manifests.

## Capture and analyze

Use the SDR admission control first, from PowerShell:

```powershell
powershell -NoProfile -File .\run-windows.ps1 -Clip .\fixtures\sdr10\source.mp4 -FixtureManifest .\fixtures\fixture-manifest.json -Output C:\hdr-results\sdr01
```

If it succeeds, repeat with `pq10`/`pq01` and `hlg10`/`hlg01`. Every output
directory must be new. `-Adapter 1` selects a different DXGI adapter if needed;
the chosen adapter and all its observed outputs are recorded. No display HDR or
white-level setting is changed. Wine is rejected by the probe.

The runner checks the executable and current tool sources against the build
manifest, binds the clip to its validated fixture kind, copies both manifests
into the result, and rechecks identities afterward. It bounds the process to
45seconds and kills only its owned process on timeout. Timeout, decoder error,
missing marker, or incomplete shutdown is failed capture evidence to diagnose,
not an accepted color measurement.

Analyze returned results with this same source revision and Python3:

```bash
python analyze.py /path/to/pq01 pq10 --output /path/to/pq01-analysis.json
python -m unittest -v test_analysis
```

Use `sdr10` or `hlg10` for the other captures. Analysis validates the copied
manifests, source revision, fixture label, raw/CSV bit agreement, PTS order,
checker orientation, and all marker phases. It reports band medians/ranges and
explicit standard-reference residuals. It never fits a curve or sets
`windows_policy_acceptance=true`. The CPU tests use generated transport data;
the half-float conversion test compiles a tiny host C program, not the Windows
probe or a renderer.

## Recorded evidence and limits

- `run.json`: exit/timeout, input identities and versions/signature status/hashes
  of system components observed loaded before MediaEngine shutdown.
- `capture/metadata.jsonl`: source attributes with actual query results;
  adapter/LUID, before/after DXGI output color space/primaries/luminance and
  active-display color/SDR-white queries; actual destination description; frame
  PTS/pitch, errors, statistics and shutdown status.
- `capture/frames.rgba16f`: tight top-down320×180 RGBA binary16, little endian,
 460800bytes/frame, concatenated in the recorded frame-index order.
- `capture/samples.csv`: exact half bits and float values for all bands/markers.
- Copied build/fixture manifests: bind the measurements to their inputs.

The probe mirrors the recovered provider contract: MFStartup lite, muted
stable-state loading with flags0x16, startup BGRA8 output attribute, protected
D3D11 device manager, two same-adapter devices and a shared RGBA16F RT/SRV
texture. It acquires key0, transfers/flushed submits/releases, then the owner
acquires key0, copies and performs blocking staging Map before CPU readback.
It checks mutex success exactly, releases only owned locks, and shuts down the
engine before releasing devices/MF/COM. The legacy shared handle is not
CloseHandle-owned. The inner deadline cannot interrupt a stalled driver call;
the external process timeout covers that failure.

This standalone tool uses a local file URL and finite playback, without WPE's
private source extension, desktop surface, display association or final shader.
Public source-attribute getters do not expose the selected decoder MFT,
hardware processor/shader, internal output media type or exact sink-selected
display metadata. Loaded-module presence and advertised output capabilities
must not be treated as that missing route evidence.

Primary contracts: [frame transfer](https://learn.microsoft.com/en-us/windows/win32/api/mfmediaengine/nf-mfmediaengine-imfmediaengine-transfervideoframe),
[source attributes](https://learn.microsoft.com/en-us/windows/win32/api/mfmediaengine/nf-mfmediaengine-imfmediaengineex-getstreamattribute),
[events](https://learn.microsoft.com/en-us/windows/win32/api/mfmediaengine/ne-mfmediaengine-mf_media_engine_event),
[keyed mutex](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgikeyedmutex-acquiresync),
[shared-handle lifetime](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiresource-getsharedhandle).
`reference.json` cites the source standards and explicitly separates its HLG
reference display from Windows policy.
