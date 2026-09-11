# Native/fork temporal comparison

These tools inventory **every installed item**, capture paired lossless RGB
videos, and quantify spatial and temporal differences. They do not declare the
renderer equivalent or infer a renderer defect from an image difference.

Requirements: Python with NumPy/Pillow, FFmpeg/ffprobe. Local capture also needs
Gamescope, xprop, the installed native application/Proton, and the machine's
reviewed `reference_harness.py` adapter. Nothing is installed or downloaded.
The default adapter is `.claude/tools/native/reference_harness.py` (intentionally
machine-local); use `--adapter PATH` elsewhere. Its existing `prepare`, `X11`,
`validate_display`, `win`, `STEAM`, and `PROTON` interface is required. Native
capture is unavailable without this containment adapter. Importing Windows
recordings and the inventory/comparison tools remain usable independently.

Run CPU tests:

```bash
python -m unittest discover -s tools/parity -p 'test_*.py'
```

## Inventory and coverage

```bash
python tools/parity/parity.py inventory \
  /home/admin/.local/share/Steam/steamapps/workshop/content/431960 \
  --out /tmp/lwe-parity-inventory.json --hash-assets
```

Each directory remains in the inventory, including missing/broken metadata,
non-wallpaper workshop assets, scene/video/web items and unsupported types.
Root scene text is scanned for particles, control points, ropes/trails, scripts,
randomness, clocks, camera paths, pointer input, audio, media, video and HDR.
This is a conservative signal scan; referenced packed scripts/materials/particle
systems may add dependencies. The optional existing `tools/validate-corpus.py`
package reader supplies root scene inspection; if absent, packed scene inspection
is recorded as missing. Use `repkg` for deeper inspection. Use `--hash-assets` to bind every file by content; without it, the fast inventory cannot attach a comparable result to current assets. The inventory is CPU
only; it does not execute workshop content.

## Capture an item

Use new private `/tmp` paths. Preparation copies the installed application,
Proton prefix and entire wallpaper with reflinks where available. Copies can
consume substantial storage on filesystems without reflinks. Native application
assets/configuration and original workshop assets are preserved. The private
selector executable is omitted by the adapter.

```bash
python tools/parity/capture.py prepare \
  --root /tmp/lwe-parity-klonoa \
  --item /home/admin/.local/share/Steam/steamapps/workshop/content/431960/2885298446 \
  --width 960 --height 540 --fps 30 --post-processing enabled

python tools/parity/capture.py pair \
  --root /tmp/lwe-parity-klonoa \
  --binary /home/admin/Projects/repos/linux-wallpaperengine/build/output/linux-wallpaperengine \
  --out /tmp/lwe-parity-results/2885298446 --duration 8 --warmup 5 --diagnostic
```

For authored properties, pass `prepare --properties overrides.json`, containing
an object such as `{"cameratype":"1"}`. Keys must exist in the authored project.
Values become defaults in the **private project used by both renderers**. Both
original and captured asset hashes and overrides are recorded. This is a
controlled scenario change, not proof both applications applied the property;
verify the resulting camera/behavior and preserve the original control capture.

Captures run sequentially inside separate headless Gamescope displays under
`/tmp/lwe-render-fix-build.lock`. Do not wrap the command in another flock on that
lock. Host DISPLAY/WAYLAND_DISPLAY and injection variables are removed. Workers
reject a missing or host-identical display. Capture selects an exact named XID
and checks its PID/private Wine prefix and requested dimensions before and after
recording. Native and compositor cleanup are bounded and scoped to their private
prefix/process groups. A private control socket prevents replacing the live
engine's socket. There is no visible desktop or host-display fallback.

Each pair writes `reference.mkv`, `candidate.mkv`, role metadata/logs, scenario,
application identity, `comparison.json`, and worst-frame RGB/difference images.
Video is FFV1/BGR0, preserving RGB8 without chroma subsampling. FFmpeg samples the
X11 drawable at the requested capture FPS using passthrough timestamps; it does
not interpolate frames. Raw acquisition timestamps remain in `*-ffmpeg.log`
(`-debug_ts`); JSON records decoded PTS and process launch/completion clocks.
These are capture sample times, **not simulation ticks or presentation fences**.
Readiness means an owned correctly sized drawable (plus fork control socket),
not proven scene completion. Warmup is seconds after that boundary.

Only FPS, output dimensions, MSAA off, render scale 1, and the selected
post-processing mode have explicit paired intent. Native residual quality
settings are preserved in `capture-config.json`; fork uses fill framing. A fixed
private pointer and disconnected audio servers reduce uncontrolled input, but
native/fork handling and silence still require verification. Media integration,
web network/storage/clocks, video seek, independent monitor/span behavior and
input replay are not implemented by this capture scenario. Mark them unsupported
or exploratory instead of claiming behavioral coverage.

## Compare recordings, including Windows imports

```bash
python tools/parity/parity.py compare /path/native.mkv /path/fork.mkv \
  --item /path/original/workshop/item \
  --out /tmp/lwe-parity-results/ITEM_ID/comparison.json
```

Recordings must contain at least two timestamped frames with the same geometry.
There is no implicit resize, crop, registration, color correction, tone mapping,
frame interpolation, or automatic content-selected offset. `--offset 0.2` means
candidate content appears 0.2 seconds later in its recording. Establish that
single offset from an independent synchronization marker; selecting the offset
that minimizes the difference can conceal a real animation timing error.
Timestamp pairing uses a maximum skew of 0.51 times the smaller median frame
interval and reports every unmatched frame and the maximum actual skew.

Metrics use RGB levels 0..255: MAE, RMSE, PSNR, fraction of pixels whose channel
error exceeds `--tolerance` (default 2), adjacent-frame delta error, each clip's
motion/identical-sample fraction, and temporal mean/standard-deviation image
errors. Adjacent-frame metrics use only consecutive matched samples. Cadence
reports sample intervals and gaps; repeated pixels can indicate a static scene
or undersampling as well as a stall. They are not measured renderer frame times.
The tolerance describes changed pixels; it is not a pass/fail threshold. Exact
PSNR infinity is represented by `psnr_db: null, psnr_infinite: true`.

Determinism is checked first for each wallpaper. `prepare` writes an unresolved `controls.json`; `pair` refuses a blocked preflight unless `--diagnostic` is explicitly selected for discovery. Two recordings cannot synchronize independent random simulation. Particles,
random frames (including allocation-address seeds), random camera paths, control
point histories, and rope/trail state can differ even at equal FPS and elapsed
time. Avoid per-frame defect claims until seeds, initial state, simulation time,
inputs, and histories are demonstrably equivalent. To measure independent-run
variability, record a second pair into a new output directory and supply:

```bash
python tools/parity/parity.py compare first/reference.mkv first/candidate.mkv \
  --reference-repeat second/reference.mkv --candidate-repeat second/candidate.mkv \
  --item /path/original/workshop/item --out /tmp/item/comparison.json
```

Within-renderer baselines and temporal distributions help triage stochastic
motion. They cannot replace phasing out or matching the actual random inputs. They do not prove statistical equivalence, identical particle trajectories,
rope physics, or control-point behavior. Longer recordings and representative
input scenarios are needed for those questions. Missing random-state evidence
continues to block an evidence-backed comparison even with repeat runs.

`--evidence evidence.json` accepts one object per gate listed in `parity.GATES`:

```json
{"scene_time":{"verified":true,"evidence":"Path/hash of independently recorded clock-marker trace, capture offset, and residual phase bound"}}
```

A successful input preflight and artifact-bound `verify-repeats` result are also required; manually checked evidence gates alone cannot qualify a comparison. All gates require an explicit evidence description: asset identity, scene time,
random state, input, audio, properties, camera, ordered camera sequence, quality, framing, color pipeline,
video seek, reference environment, and capture integrity. Record why a gate is
inapplicable for a controlled static fixture instead of omitting it. These are
reviewer assertions with provenance, not automatically verified declarations.
A bare `true` is insufficient. Do not fill the gates from requested settings alone.
Lossy/unknown codecs, non-RGB8 formats, recording gaps and unmatched frames also
block the comparison. Import genuinely lossless RGB Windows footage; transcoding
lossy footage to FFV1 cannot restore the lost source information.

Statuses are `exploratory`, `comparable_difference`, or
`no_difference_in_capture`; the last two require all evidence gates. They describe
only the measured footage. A WineD3D/Gamescope comparison does not establish
Windows/DWM color transfer or physical HDR parity. Full-size XID geometry does
not rule out native clipping/scissor margins. Calibrate the complete image bounds
and color ramps independently. Black clips or missing layers require checking
scene/log readiness before using any numerical difference as renderer evidence.

## Library report

Keep one current `comparison.json` under `RESULTS/ITEM_ID/`. Capture failures are
written with their reason. For an unsupported scenario, write a report with
`status: "unsupported"`, its reason in `blockers`, and an `item` object containing
the inventory's `id` and `project_sha256`.

```bash
python tools/parity/parity.py report /tmp/lwe-parity-inventory.json \
  /tmp/lwe-parity-results --out /tmp/lwe-parity-report.json
```

The report retains every inventory item and validates item/project/content identity so
a misplaced comparison cannot certify another wallpaper. Missing recordings stay
pending; invalid reports are explicit. Inventory file sizes/mtimes are not full
asset identity proof; capture preparation hashes each copied asset. There is no
aggregate parity percentage. Re-run inventory after library updates and retain
separate scenario/repeat artifacts. Capture scene/video/web items incrementally;
no unrestricted full-library GPU batch starts implicitly.

## Determinism preflight and observable control example

`python tools/parity/determinism.py template --item ITEM --out controls.json`
creates the per-item source inventory. `preflight controls.json --out preflight.json`
validates the reviewed input controls. Every source begins unresolved: shader time
and random/procedural inputs, particle RNG, control-point/trail history, camera
sequence, script clocks/RNG, audio/input/media, video seek, property events, and
simulation timesteps. Complete referenced-source coverage, observed shader/scene
execution, and cross-engine **known input** provenance must also be recorded.
States are `absent`, `fixed`, or `matched`, with evidence for both engines.

Equal RNG seeds do not establish the same algorithm, distribution, consumption
order or initial state. For example, native random camera selection consumes a
remaining-shot pool with multiply-high/rejection sampling; the fork currently
samples modulo while only excluding the previous shot. A native A,B,C sequence
and fork A,B,A sequence must not be automatically rearranged to reduce error.
Random camera playlists remain blocked until matched ordered inputs are established.
Deterministic shader `fract(sin(...))` hashes can amplify backend floating-point
and trigonometric differences even with identical inputs; this is a numerical
shader behavior question, not necessarily an uncontrolled RNG source.

`capture.py apply-controls --root ROOT --controls controls.json` applies only
exact hash/count-checked substitutions to private loose shader/script/JSON inputs
and records before/after hashes. All substitutions validate before any write.
Packed inputs require separately reviewed extraction and loader-precedence evidence.
Fixed inputs require `scope: "altered_controls"` and explicit coverage exclusions.
Removing an effect or replacing its noise generator cannot certify the omitted
original behavior. No universal native scene seed/fixed timestep has been verified;
font `random-seed` and particle `timescale` fields are not such a control.

This synthetic example demonstrates a visible changing shader clock/hash output,
then replaces its two inputs with tracked constants while retaining the downstream
color calculation. The right half is magenta while uncontrolled and green only when both exact fixed inputs reach the executing shader. It uses a
full-sized source texture, HDR/bloom off and clip-space orthographic geometry;
known native scissor margins and color transfer remain separate limitations.

```bash
python tools/parity/example.py generate --out /tmp/parity-clock-source
python tools/parity/capture.py prepare --root /tmp/parity-clock \
  --item /tmp/parity-clock-source --width 320 --height 180 --fps 15
python tools/parity/capture.py pair --root /tmp/parity-clock --binary /path/to/linux-wallpaperengine \
  --out /tmp/parity-clock-dynamic --duration 3 --warmup 3 --diagnostic
# The controls command requires both baseline videos to vary and contain the magenta marker.
python tools/parity/example.py controls --root /tmp/parity-clock --dynamic-run /tmp/parity-clock-dynamic --out /tmp/clock-controls.json
python tools/parity/capture.py apply-controls --root /tmp/parity-clock --controls /tmp/clock-controls.json
python tools/parity/capture.py pair --root /tmp/parity-clock --binary /path/to/linux-wallpaperengine \
  --out /tmp/parity-clock-first --duration 3 --warmup 3
python tools/parity/capture.py pair --root /tmp/parity-clock --binary /path/to/linux-wallpaperengine \
  --out /tmp/parity-clock-second --duration 3 --warmup 3
python tools/parity/determinism.py verify-repeats /tmp/parity-clock/controls.json \
  --first /tmp/parity-clock-first --second /tmp/parity-clock-second --out /tmp/clock-repeat-verification.json
```

`verify-repeats` checks distinct launch/acquisition identities, matching run/scenario/
input/config/base-asset/loaded-renderer hashes, all video content hashes, complete
lossless RGB timing coverage, exact within-renderer samples, and nonblack authored
sentinel regions in **every frame**. Sentinel RGB/tolerances must be finite and
exclude black. Identical black/error captures cannot pass. Success means only
`repeatable_for_tested_scenario_window`; it is not universal absence of randomness,
original excluded-effect correctness, or cross-engine pixel parity. The manifest
still contains reviewed source/runtime assertions, so its evidence must be inspected.

To carry that bound result into comparison, use `parity.py compare` with `--controls`,
`--repeat-verification`, both repeat recordings, and `--item`. Cross-engine output
is then measured with the remaining quality/framing/color/etc. gates still required.
Missing/contradictory encoding metadata stays blocked even if RGB numbers match.
The same exact prepared scenario may be reused for independent fresh-process repeats;
copying a run or comparing a run with itself is rejected.

Retain result directories independently from disposable private app/prefix copies. On this machine `/tmp` is a 16GiB tmpfs and each private native app/prefix uses about 1.7GiB. Finish one item and retire its private copies before accumulating many preparations; preserve clips, sources and manifests needed for review. The input guard records exact newly created `shaders/blobsSM40/*.dxs` cache artifacts and removes only those generated files before a new run or input substitution. Originally present cache files remain hashed inputs. Every other added or changed item file aborts capture.

The local 320x180/15 FPS example was exercised with 45-frame, three-second clips.
Uncontrolled native/fork footage both varied; an initial native `fract` compilation
failure produced a red fallback and failed the marker check, so the fixture uses
the stock native `frac` spelling. With exact fixed inputs, two fresh recordings
per engine matched every repeated RGB pixel and both cross-engine recordings also
had MAE/RMSE 0. Every frame showed the input-dependent green marker. The result
remains specific to this altered shader scenario on Wine/Gamescope; original noise,
particles, camera ordering and Windows/display color parity remain unverified.
