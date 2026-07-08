#include "PulseAudioPlaybackRecorder.h"
#include "WallpaperEngine/Logging/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glm/common.hpp>

float movetowards (float current, float target, float maxDelta) {
    if (abs (target - current) <= maxDelta) {
	return target;
    }

    return current + glm::sign (target - current) * maxDelta;
}

/**
 * Per-band gain replicating Wallpaper Engine's spectrum output. WE's raw g_AudioSpectrum
 * values are NOT normalized or equalized by the engine (authors do that in wallpapers);
 * on a pink-noise input its 64 bands average the curve measured by the community
 * ("pink noise" reference array, Steam guide 837435817 — the calibration standard every
 * WE audio emulator matches). This table maps OUR pipeline (44.1kHz U8 mono capture,
 * Hann-windowed 1024-point FFT, bins band*2) onto that measured response: gain[band] =
 * table[band] / mean|X_pink(band*2)|, derived numerically by pushing -20dBFS pink noise
 * through the exact capture math. Shape is exact; absolute scale assumes the reference
 * measurement used the standard -20dBFS test level.
 */
constexpr float kWeBandGain[64] = {
    0.039542f, 0.110015f, 0.130629f, 0.149603f, 0.148387f, 0.151763f, 0.154256f, 0.158409f,
    0.162823f, 0.171196f, 0.178758f, 0.182072f, 0.188283f, 0.186852f, 0.196723f, 0.199302f,
    0.200487f, 0.212511f, 0.229720f, 0.233533f, 0.231954f, 0.245645f, 0.244336f, 0.254547f,
    0.251100f, 0.278948f, 0.280204f, 0.283992f, 0.286617f, 0.369209f, 0.493948f, 0.505885f,
    0.573950f, 0.577447f, 0.678270f, 0.703267f, 0.786335f, 0.832643f, 0.894425f, 0.947850f,
    1.012235f, 1.078655f, 1.170370f, 1.229459f, 1.347654f, 1.376703f, 1.491941f, 1.585113f,
    1.662624f, 1.758022f, 1.850683f, 1.954022f, 2.037420f, 2.113416f, 2.223025f, 2.256487f,
    2.336662f, 2.435359f, 2.443985f, 2.488910f, 2.579635f, 2.558157f, 2.574051f, 2.525312f,
};

namespace WallpaperEngine::Audio::Drivers::Recorders {
void pa_stream_notify_cb (pa_stream* stream, void* /*userdata*/) {
    switch (pa_stream_get_state (stream)) {
	case PA_STREAM_FAILED:
	    sLog.error ("Cannot open stream for capture. Audio processing is disabled");
	    break;
	case PA_STREAM_READY:
	    sLog.debug ("Capture stream ready");
	    break;
	default:
	    break;
    }
}

void pa_stream_read_cb (pa_stream* stream, const size_t /*nbytes*/, void* userdata) {
    auto* recorder = static_cast<PulseAudioPlaybackRecorder::PulseAudioData*> (userdata);

    // Careful when to pa_stream_peek() and pa_stream_drop()!
    // c.f. https://www.freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#ac2838c449cde56e169224d7fe3d00824
    const uint8_t* data = nullptr;
    size_t currentSize;
    if (pa_stream_peek (stream, reinterpret_cast<const void**> (&data), &currentSize) != 0) {
	sLog.error ("Failed to peek at stream data...");
	return;
    }

    if (data == nullptr && currentSize == 0) {
	// No data in the buffer, ignore.
	return;
    }

    if (data == nullptr && currentSize > 0) {
	// Hole in the buffer. We must drop it.
	if (pa_stream_drop (stream) != 0) {
	    sLog.error ("Failed to drop a hole while capturing!");
	    return;
	}
    } else if (currentSize > 0 && data) {
	const size_t dataToCopy = std::min (currentSize, WAVE_BUFFER_SIZE - recorder->currentWritePointer);

	// depending on the amount of data available, we might want to read one or multiple frames
	const size_t end = recorder->currentWritePointer + dataToCopy;

	// this packet will fill the buffer, perform some extra checks for extra full buffers and get the latest one
	if (end == WAVE_BUFFER_SIZE) {
	    if (const size_t numberOfFullBuffers = (currentSize - dataToCopy) / WAVE_BUFFER_SIZE;
		numberOfFullBuffers > 0) {
		// calculate the start of the last block (we need the end of the previous block, hence the - 1)
		const size_t startOfLastBuffer = std::max (
		    dataToCopy + (numberOfFullBuffers - 1) * WAVE_BUFFER_SIZE, currentSize - WAVE_BUFFER_SIZE
		);
		// copy directly into the final buffer
		memcpy (recorder->audioBuffer, &data[startOfLastBuffer], WAVE_BUFFER_SIZE * sizeof (uint8_t));
		// copy whatever is left to the read/write buffer
		recorder->currentWritePointer = currentSize - startOfLastBuffer - WAVE_BUFFER_SIZE;
		memcpy (
		    recorder->audioBufferTmp, &data[startOfLastBuffer + WAVE_BUFFER_SIZE],
		    recorder->currentWritePointer * sizeof (uint8_t)
		);
	    } else {
		// okay, no full extra packets available, copy the rest of the data and flip the buffers
		memcpy (&recorder->audioBufferTmp[recorder->currentWritePointer], data, dataToCopy * sizeof (uint8_t));
		uint8_t* tmp = recorder->audioBuffer;
		recorder->audioBuffer = recorder->audioBufferTmp;
		recorder->audioBufferTmp = tmp;
		// reset write pointer
		recorder->currentWritePointer = 0;
	    }

	    // signal a new frame is ready
	    recorder->fullFrameReady = true;
	} else {
	    // copy over available data to the tmp buffer and everything should be set
	    memcpy (&recorder->audioBufferTmp[recorder->currentWritePointer], data, dataToCopy * sizeof (uint8_t));
	    recorder->currentWritePointer += dataToCopy;
	}
    }

    if (pa_stream_drop (stream) != 0) {
	sLog.error ("Failed to drop data after peeking");
    }
}

void pa_server_info_cb (pa_context* ctx, const pa_server_info* info, void* userdata) {
    if (info == nullptr) {
	return;
    }

    auto* recorder = static_cast<PulseAudioPlaybackRecorder::PulseAudioData*> (userdata);

    pa_sample_spec spec;
    spec.format = PA_SAMPLE_U8;
    spec.rate = 44100;
    spec.channels = 1;

    if (recorder->captureStream) {
	// this callback re-runs on every sink/source event (volume change, bluetooth
	// connect); without an explicit disconnect the old capture stream stays alive
	// server-side until the context dies, piling monitor streams onto the server
	pa_stream_disconnect (recorder->captureStream);
	pa_stream_unref (recorder->captureStream);
    }

    recorder->captureStream = pa_stream_new (ctx, "output monitor", &spec, nullptr);

    pa_stream_set_state_callback (recorder->captureStream, &pa_stream_notify_cb, userdata);
    pa_stream_set_read_callback (recorder->captureStream, &pa_stream_read_cb, userdata);

    std::string monitor_name (info->default_sink_name);
    monitor_name += ".monitor";

    // setup latency
    pa_buffer_attr attr {};

    // 10 = latency msecs, 750 = max msecs to store. Milliseconds means dividing by 1000 —
    // the previous /100 requested 100ms fragments, which made audio visualizers update at
    // ~10Hz and visibly lag behind the music.
    size_t bytesPerSec = pa_bytes_per_second (&spec);
    attr.fragsize = bytesPerSec * 10 / 1000;
    attr.maxlength = attr.fragsize + bytesPerSec * 750 / 1000;

    if (pa_stream_connect_record (recorder->captureStream, monitor_name.c_str (), &attr, PA_STREAM_ADJUST_LATENCY)
	!= 0) {
	sLog.error ("Failed to connect to input for recording");
    }
}

void pa_context_subscribe_cb (pa_context* ctx, pa_subscription_event_type_t t, uint32_t idx, void* userdata) {
    // sink changes mean re-take the stream
    pa_operation* o = pa_context_get_server_info (ctx, &pa_server_info_cb, userdata);
    if (o) {
	pa_operation_unref (o);
    }
}

void pa_context_notify_cb (pa_context* ctx, void* userdata) {
    switch (pa_context_get_state (ctx)) {
	case PA_CONTEXT_READY:
	    {
		// set callback
		pa_context_set_subscribe_callback (ctx, pa_context_subscribe_cb, userdata);
		// set events mask and enable event callback.
		pa_operation* o = pa_context_subscribe (
		    ctx, static_cast<pa_subscription_mask_t> (PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE),
		    nullptr, nullptr
		);

		if (o) {
		    pa_operation_unref (o);
		}

		// context being ready means to fetch the sink too
		pa_operation* o2 = pa_context_get_server_info (ctx, &pa_server_info_cb, userdata);

		if (o2) {
		    pa_operation_unref (o2);
		}

		break;
	    }
	case PA_CONTEXT_FAILED:
	    sLog.error ("PulseAudio context initialization failed. Audio processing is disabled");
	    break;
	default:
	    break;
    }
}

PulseAudioPlaybackRecorder::PulseAudioPlaybackRecorder () :
    m_captureData (
	{ .kisscfg = kiss_fftr_alloc (WAVE_BUFFER_SIZE, 0, nullptr, nullptr),
	  // value-initialize ("()") so silence/failed capture reads as zeros, not
	  // uninitialized noise (which renders as random audio-reactive motion)
	  .audioBuffer = new uint8_t[WAVE_BUFFER_SIZE](),
	  .audioBufferTmp = new uint8_t[WAVE_BUFFER_SIZE]() }
    ) {
    for (int i = 0; i < WAVE_BUFFER_SIZE; i++) {
	this->m_hannWindow[i]
	    = 0.5f * (1.0f - std::cos (2.0f * static_cast<float> (M_PI) * i / (WAVE_BUFFER_SIZE - 1)));
    }

    this->m_mainloop = pa_mainloop_new ();
    this->m_mainloopApi = pa_mainloop_get_api (this->m_mainloop);
    this->m_context = pa_context_new (this->m_mainloopApi, "wallpaperengine-audioprocessing");

    pa_context_set_state_callback (this->m_context, &pa_context_notify_cb, &this->m_captureData);

    if (pa_context_connect (this->m_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
	sLog.error ("PulseAudio connection failed! Audio processing is disabled");
	return;
    }

    // wait until the context is ready
    while (pa_context_get_state (this->m_context) != PA_CONTEXT_READY) {
	pa_mainloop_iterate (this->m_mainloop, 1, nullptr);
    }
}

PulseAudioPlaybackRecorder::~PulseAudioPlaybackRecorder () {
    if (m_captureData.captureStream) {
	pa_stream_unref (m_captureData.captureStream);
    }

    delete[] this->m_captureData.audioBufferTmp;
    delete[] this->m_captureData.audioBuffer;
    free (this->m_captureData.kisscfg);

    pa_context_disconnect (this->m_context);
    pa_context_unref (this->m_context);
    pa_mainloop_free (this->m_mainloop);
}

void PulseAudioPlaybackRecorder::update () {
    // Drain everything the server has queued instead of dispatching a single event: one
    // non-blocking iterate consumes at most one ~10ms fragment per render frame (16.6ms at
    // 60fps), which is structurally slower than realtime — the monitor stream backed up to
    // maxlength and pipewire-pulse spammed "overrun recover"/skip, feeding the FFT stale,
    // gappy audio. The loop ends once the event queue is empty; the guard caps a frame's
    // work if the server keeps queueing while we dispatch (~76 fragments fit maxlength).
    // ponytail: single-threaded pump on the render thread; a capture thread only becomes
    // worth it if sub-frame audio latency ever matters
    for (int guard = 0; guard < 256 && pa_mainloop_iterate (this->m_mainloop, 0, nullptr) > 0; guard++) {
    }

    // Interpolate published values toward the FFT destinations with WE's visual feel:
    // near-instant attack, gradual exponential release (bars snap up on a beat, fall away
    // smoothly). The old symmetric linear step also crawled toward >1 peaks for ~10 frames.
    // ponytail: attack/release factors tuned by eye against WE footage; per render frame
    const auto follow = [] (float current, float target) {
	constexpr float kAttack = 0.7f;
	constexpr float kRelease = 0.12f;
	return current + (target - current) * (target > current ? kAttack : kRelease);
    };

    for (int i = 0; i < 64; i++) {
	this->audio64[i] = follow (this->audio64[i], this->m_FFTdestination64[i]);
	if (i >= 32) {
	    continue;
	}
	this->audio32[i] = follow (this->audio32[i], this->m_FFTdestination32[i]);
	if (i >= 16) {
	    continue;
	}
	this->audio16[i] = follow (this->audio16[i], this->m_FFTdestination16[i]);
    }

    if (!this->m_captureData.fullFrameReady) {
	return;
    }

    this->m_captureData.fullFrameReady = false;

    // convert audio data to deltas so the fft library can properly handle it
    for (int i = 0; i < WAVE_BUFFER_SIZE; i++) {
	this->m_audioFFTbuffer[i] = (this->m_captureData.audioBuffer[i] - 128) / 128.0f;
    }

    // Noise gate (ported from beingsuz): a monitor source is never perfectly silent, and
    // auto-gain below would amplify that floor into full-height bars. RMS in 0-128 sample
    // units; tunable via WPE_AUDIO_GATE (0 disables).
    double sumSquares = 0.0;
    for (int i = 0; i < WAVE_BUFFER_SIZE; i++) {
	const double d = static_cast<int> (this->m_captureData.audioBuffer[i]) - 128;
	sumSquares += d * d;
    }
    const float rms = static_cast<float> (std::sqrt (sumSquares / WAVE_BUFFER_SIZE));

    float gate = 2.0f;
    if (const char* g = std::getenv ("WPE_AUDIO_GATE")) {
	gate = std::atof (g);
    }

    if (gate > 0.0f && rms < gate) {
	for (int i = 0; i < 64; i++) {
	    this->m_FFTdestination64[i] = 0.0f;
	    if (i < 32) {
		this->m_FFTdestination32[i] = 0.0f;
	    }
	    if (i < 16) {
		this->m_FFTdestination16[i] = 0.0f;
	    }
	}
	return;
    }

    // Hann window before the FFT: an unwindowed (rectangular) transform leaks every tone
    // across the whole spectrum, smearing the bands into mush. WE-scale compensation for
    // the window's 0.5 coherent gain is folded into kWeBandGain's derivation.
    for (int i = 0; i < WAVE_BUFFER_SIZE; i++) {
	this->m_audioFFTbuffer[i] *= this->m_hannWindow[i];
    }

    // perform full fft pass
    kiss_fftr (this->m_captureData.kisscfg, this->m_audioFFTbuffer, this->m_FFTinfo);

    // Reduce to bands exactly like Wallpaper Engine presents them: raw per-band magnitudes
    // scaled by kWeBandGain (see its comment). No normalization, no clamp — WE's values
    // track the actual playback level and can exceed 1; wallpapers were authored against
    // that behavior and do their own peak handling.
    for (int band = 0; band < 64; band++) {
	const int index = band * 2;
	const float re = this->m_FFTinfo[index].r;
	const float im = this->m_FFTinfo[index].i;
	const float value = std::sqrt (re * re + im * im) * kWeBandGain[band];

	this->m_FFTdestination64[band] = value;
	this->m_FFTdestination32[band >> 1] = value;
	this->m_FFTdestination16[band >> 2] = value;
    }

    // WPE_AUDIO_DEBUG=1: log capture activity ~once a second for verification
    if (std::getenv ("WPE_AUDIO_DEBUG") != nullptr) {
	static int debugCounter = 0;
	if (++debugCounter % 43 == 0) {
	    if (FILE* f = fopen ("/tmp/we-audio-debug.log", "a")) {
		fprintf (
		    f, "rms=%.2f b0=%.2f b8=%.2f b32=%.2f b60=%.2f\n", rms, this->m_FFTdestination64[0],
		    this->m_FFTdestination64[8], this->m_FFTdestination64[32], this->m_FFTdestination64[60]
		);
		fclose (f);
	    }
	}
    }
}

} // namespace WallpaperEngine::Audio::Drivers::Recorders