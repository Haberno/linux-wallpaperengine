---
type: Investigation
title: Audio Capture Silence Investigation
description: Open investigation — PulseAudio-layer monitor capture delivers silence while native PipeWire capture works, so every audio visualizer renders flat. Evidence chain, ruled-out causes, and next steps.
resource: file:///home/admin/repos/linux-wallpaperengine
tags: [linux-wallpaperengine, audio, visualizer, pipewire, pulseaudio, capture, bug]
timestamp: 2026-07-08T19:55:00-04:00
---

# Audio Capture Silence Investigation (2026-07-08, OPEN)

> **DSP update (2026-07-16):** the recorder no longer uses the auto-gain/noise-gate
> implementation described below. It now publishes raw 64-band stereo data for web
> wallpapers and native-normalized, smoothed left/right/average spectra for scenes.
> The PulseAudio-shim silence evidence remains relevant when the captured samples
> themselves are zero.

**Symptom:** audio-visualizer content ("audio visualizer stuff") does not render
at all — bars/audio-responsive layers stay flat/invisible on audio-responsive
wallpapers. Reported while Shin Godzilla `3094637759` was live. Playback audio
itself is **fine** (user confirms music is audible on the AirPods).

**Status: root cause NOT yet found.** The engine is a *victim*, not the culprit:
a plain `parec` client shows the identical silence. Everything below is the
evidence chain so the next session doesn't re-derive it.

## TL;DR

Any capture client going through the **PulseAudio shim** (`pipewire-pulse`) and
reading the default sink's monitor gets **pure zeros**, even while uncorked
streams play into that sink at 100% and the user hears them. The **same monitor
captured natively** (`pw-record`) carries full signal. The engine's noise gate
correctly zeroes all FFT bands on that silence → visualizers flat everywhere.
Restarting `pipewire-pulse` does **not** fix it. User recollection: it broke
around the automute-ignore / "auto unmute for specific window" work.

## Environment at time of investigation

- Engine PID 55996: `--layer background --screen-root HDMI-A-1 --scaling fill
  -bg .../3094637759 --noautomute --no-full-screen-pause --volume 78 --fps 140
  --screen-root DP-1 ... -bg .../3094637759` (both monitors, same scene).
  stdout/stderr → `/dev/null` (spawned via waypaper-fork changer → wrapper), so
  there is **no engine log** for the live instance.
- Default sink: `bluez_output.70_AE_2A_81_D7_7C.1` — "Tirth's AirPods Pro"
  (A2DP sink, codec AAC), PipeWire node id **75**, `pactl` sink index 142.
- PipeWire 1.6.7, single instance (`pipewire` 1907, `wireplumber` 1910,
  `pipewire-pulse` 3707). `pactl info` server "PulseAudio (on PipeWire 1.6.7)".

## Evidence chain

1. **Engine capture stream is healthy on paper.** `pactl list source-outputs`
   shows `wallpaperengine-audioprocessing` (owner PID verified = 55996),
   `Corked: no`, `Mute: no`, 100%, attached to source 142 =
   `bluez_output.70_AE_2A_81_D7_7C.1.monitor` — the *correct* source (monitor
   of the current default sink, state RUNNING).
2. **The wallpaper is genuinely audio-responsive.** `project.json` has
   `supportsaudioprocessing: true`; `scene.pkg` shaders use the
   `AUDIOPROCESSING` combo and `g_AudioSpectrum{16,32,64}{Left,Right}` +
   `audioamount/audiobounds/audioexponent` properties.
3. **Render wiring is intact.** `CPass::setupUniforms` binds
   `g_AudioSpectrum16/32/64 Left+Right` to the recorder's independent left and
   right scene spectra, using the same live-pointer mechanism as `g_Time` etc.
4. **Historical recorder behavior.** At the time of this investigation,
   `PulseAudioPlaybackRecorder.cpp` had an RMS noise gate (`WPE_AUDIO_GATE`)
   that could return before debug logging. That gate has since been removed;
   `WPE_AUDIO_DEBUG` now reports RMS and raw bands even for silent frames.
5. **Windowed test instance with `WPE_AUDIO_DEBUG=1` (9 s):** no debug log →
   capture callback sees silence. Engine log otherwise benign (GLEW/GLX
   warning under Wayland, `Simple_Audio_Bars` zcompat shader replacement for
   `2084198056`).
6. **PA-layer monitor capture is silent.** `parec -d
   bluez_output.70_AE_2A_81_D7_7C.1.monitor --raw` → **peak sample 0**, even
   while `pactl list sink-inputs` simultaneously showed uncorked, unmuted
   streams into sink 142: Brave at 100%, a generated 440/880/1760 Hz test tone
   via `paplay` at 100%, engine's own stream at 65%.
7. **Native PipeWire capture of the SAME monitor works.** `pw-record -P
   stream.capture.sink=true --target bluez_output.70_AE_2A_81_D7_7C.1` during
   the same tone → **peak 24038 over 188416 samples**. Signal exists on the
   node's monitor ports; only the pulse-shim path loses it.
8. **Node is processing.** `pw-top`: node 75 RUNNING, S16LE 2ch 48000,
   quantum 256, real busy time. Playback path confirmed healthy (user hears
   audio).
9. **`pipewire-pulse` restart did NOT help.** After `systemctl --user restart
   pipewire-pulse` + fresh tone: parec peak still 0. Rules out a transient
   shim desync/zombie.
10. **Links are ACTIVE yet deliver zeros.** With parec running, `pw-dump`
    shows its stream node (id 146 in that run) with **two active links from
    node 75** (`bluez_output...` → `parec`). Samples are being lost *despite*
    an active link — or the link carries the monitor ports of something that
    mixes to silence.
11. **WirePlumber persisted state is clean.**
    `~/.local/state/wireplumber/stream-properties`: every `Input/Audio` entry
    (`Quickshell Spectrum`, `wallpaperengine-audioprocessing`, `parec`,
    `pw-record`, OBS, discord, …) is `volume 1.0, mute false`; no zero
    channelVolumes anywhere in the file. No `restore-stream` file (WP 0.5
    naming). `~/.config/pipewire/` and `~/.config/wireplumber/` don't exist
    (no stray user configs found by the quick `ls`).
12. **User datapoint:** regression believed to have appeared around the
    automute-ignore work ("targetting the auto unmute for specific window").
    That work touched the engine's `PulseAudioPlayingDetector` / automute
    plumbing and involved a lot of live `pactl`/`wpctl` mute experiments, plus
    the waypaper-fork engine-settings dialog (dotfiles commits `8c95abc`,
    `2baa752`). Engine-side automute only mutes the engine's *own output*
    stream, so no engine code path found yet that could silence a third-party
    `parec` — but ambient state changed during that era is a real suspect.

## Ruled out

- Wrong capture source / capture not running (correct monitor, uncorked).
- `--no-audio-processing` accidentally set (not in cmdline; stream exists).
- Engine gate/FFT/scale regression (gate behaves as designed; *input* is 0).
- Uniform wiring to shaders (CPass binds live recorder arrays).
- stream-restore / stream-properties mute or 0-volume (all 1.0 / unmuted).
- Zombie `pipewire-pulse` needing restart (restarted; unchanged).
- Bluetooth/AirPods playback path (audible; node processing; native monitor
  capture carries signal). **Explicitly per user: BT/audio output is fine.**
- Duplicate PipeWire instances (single daemon set).

## Open leads / next steps

1. **Is Quickshell Spectrum (noctalia bar) also flat?** It captures the same
   monitor via PA (`source-output` "Quickshell Spectrum"). If yes → confirms
   system-level PA-capture breakage independent of the engine.
2. **Try a non-BT sink**: set default to HDMI/IEC958, play tone, `parec` its
   monitor. Distinguishes "all PA monitor captures broken" vs "bluez monitor
   + PA shim specifically".
3. **Format negotiation test**: `parec --format=s16le --rate=48000
   --channels=2 -d <monitor>` (match node format exactly) — rules out a
   channelmix/resampler path producing zeros in the shim's stream node.
4. **Inspect the shim's stream node Props/ports** while capturing (compare
   against working `pw-record` node): `stream.monitor`, `capture.sink`, port
   links, `channelVolumes`, `monitorVolumes`, `softMute`.
5. **Audit the automute-era changes** (user's regression window): engine
   `PulseAudioPlayingDetector` rework, `--automute-ignore` defaults in the
   wrapper (`~/.dotfiles/local/bin/linux-wallpaperengine` line ~318), waypaper
   fork engine-audio knobs (`8c95abc`, `2baa752`), and any leftover skwd /
   quickshell mute automation from that session.
6. **Full audio stack bounce** as a blunt bisect step: `systemctl --user
   restart wireplumber pipewire pipewire-pulse` (brief audio drop), retest.
   If that fixes it → state corruption in WirePlumber/graph, not config.
7. If fixed, restart the engine (its recorder does not survive a PA server
   restart) and verify with `WPE_AUDIO_DEBUG=1` bands > 0.

## Repro / verification toolbox

```bash
# 8s stereo test tone
python3 - <<'EOF'
import wave,math,struct
w=wave.open("/tmp/tone.wav","w");w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000)
w.writeframes(b"".join(struct.pack("<hh",*[int(12000*sum(math.sin(2*math.pi*f*t/48000) for f in (440,880,1760))/3)]*2) for t in range(48000*8)))
EOF
paplay /tmp/tone.wav &

# PA-layer monitor peak (broken path — currently prints 0)
timeout 2 parec -d bluez_output.70_AE_2A_81_D7_7C.1.monitor --raw \
  | od -An -td2 | awk '{for(i=1;i<=NF;i++){v=$i<0?-$i:$i;if(v>max)max=v}}END{print max+0}'

# Native PW monitor peak (working path — prints real peaks)
timeout 2 pw-record -P stream.capture.sink=true \
  --target bluez_output.70_AE_2A_81_D7_7C.1 /tmp/cap.wav

# Engine-side view: bands logged ~1/s once capture is non-silent
WPE_AUDIO_DEBUG=1 ./build/output/linux-wallpaperengine --window 100x100x640x360 \
  -bg ~/.steam/root/steamapps/workshop/content/431960/3094637759 --noautomute
tail -f /tmp/we-audio-debug.log   # remember: silent capture logs NOTHING (gate early-return)
```

Related: [[Known Issues]] (issue entry), [[Current Status]] (dashboard),
[[Debugging Workflow]], `PulseAudioPlaybackRecorder.cpp`, `CPass.cpp:880,926`.
