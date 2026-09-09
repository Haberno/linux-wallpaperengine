---
type: Rendering Concept
title: Audio Spectrum
description: Captured frequency bands, scene normalization, and pure-tone regression evidence.
tags: [linux-wallpaperengine, audio, rendering]
---

# Audio Spectrum

`WallpaperEngineSpectrumAnalyzer` converts captured float PCM into independent
64-band left/right spectra. Web wallpapers receive these raw values.
`PlaybackRecorder::setRawSpectrum()` normalizes and smooths them for scene
shaders and SceneScript, then derives the 32/16-band buffers.

## Normalization across frequency groups

Each channel has eight envelopes, each covering eight bands. Their targets
share a reference across **both** channels:

```
globalPeak = max(all 128 raw left/right bands)
target[channel][group] = max(groupPeak, globalPeak * 0.333)
```

The envelopes approach these targets at the existing native rise/fall rates.
The divisor is at least 0.001. The intermediate interpolation and output slew
rates remain 20 and 40 per second. On audio resuming after silence, if the first
envelope is at most 0.0001 and the global peak reaches 0.0001, all envelopes
restart at 1 before following the new targets.

The shared target floor was missing from the original port. Quiet groups could
therefore amplify tiny FFT leakage independently until unrelated bars reached
nearly full height. An isolated raw FFT peak was insufficient regression
coverage: the fault became visible after the scene envelopes settled.

This affects the common scene buffers, including the Simple Audio Bars effect
used by both **3094637759 — Shin Godzilla [Audio Responsive + Puppet Warp]** and
**3644280276 — A Solitary Reflection [4K]**.

## Reference evidence

Installed `wallpaper64.exe`, SHA-256
`40e2ce021e9352324fadb3b8f72b8ba2a7ee95b71cc571d5b9f84be75cd993b0`:

- `FUN_140110630` computes sixteen eight-band maxima, reduces them to a shared
  stereo peak, and floors all sixteen targets at that peak times 0.333.
  The multiply is at `0x140111c7d`; its float constant is at `0x140492698`.
- The same routine resets the sixteen envelopes to 1 after silence, then
  applies the existing envelope, interpolation, and slew operations.
- `FUN_1400d02b0` contains the unusual reciprocal component of the FFT input.
  That behavior and the raw FFT frequency/magnitude mapping were retained;
  removing the reciprocal is not the normalization fix.

## Verification and remaining work

- [x] Reproduce the reported spread with a captured 2000 Hz output tone.
  Measured 4–14 kHz harmonics in the 48 kHz capture were at least 99 dB below
  its main peak.
  After settling, 58 of 64 processed left-channel bands exceeded 0.1 before
  the fix; only two adjacent bands exceeded it afterward. The strongest distant
  band afterward was below 0.004.
- [x] Add regressions for sustained 2000 Hz tones at 44.1/48 kHz and amplitudes
  0.1/0.9, quiet groups in both channels, reduced-resolution buffers, and audio
  returning after silence. The quiet-group and sustained-tone tests failed
  before the implementation change. All 989 assertions in 123 default test
  cases pass afterward.
- [ ] Confirm the two wallpapers' live appearance with the user while the
  2000 Hz tone continues. Expect a localized peak and small neighboring bars;
  authored shader interpolation can widen that peak.
- [ ] Recover or independently validate exact native 64-to-32/16 reduction.
  Peak-preserving pooling remains inferred.
- [ ] Investigate full-scale PCM endpoint behavior separately: the retained
  reciprocal FFT input is singular at exactly -1.0. Synthetic full-scale
  tones can corrupt the raw spectrum; the captured report was far below this
  endpoint (peak amplitude about 0.178).

See [[Audio Capture Silence Investigation]] for the older, resolved capture
silence issue and [[Known Issues]] for the broader parity status.
