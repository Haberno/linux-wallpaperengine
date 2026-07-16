#include "PlaybackRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace WallpaperEngine::Audio::Drivers::Recorders {
void PlaybackRecorder::update () { }

float PlaybackRecorder::elapsedUpdateSeconds () {
    const auto now = std::chrono::steady_clock::now ();
    if (this->m_lastUpdate.time_since_epoch ().count () == 0) {
	this->m_lastUpdate = now;
	return 1.0f / 60.0f;
    }

    const float elapsed = std::chrono::duration<float> (now - this->m_lastUpdate).count ();
    this->m_lastUpdate = now;
    return std::clamp (elapsed, 0.0f, 1.0f);
}

void PlaybackRecorder::setRawSpectrum (const float* left, const float* right, const float deltaSeconds) {
    constexpr float kEnvelopeFloor = 0.001f;
    constexpr float kEnvelopeRisePerSecond = 1.0f;
    constexpr float kEnvelopeFallPerSecond = 0.5f;
    constexpr float kIntermediateRate = 20.0f;
    constexpr float kOutputSlewRate = 40.0f;
    constexpr float kEpsilon = 0.0001f;

    std::memmove (this->rawAudio64Left, left, sizeof (this->rawAudio64Left));
    std::memmove (this->rawAudio64Right, right, sizeof (this->rawAudio64Right));

    const float dt = std::clamp (deltaSeconds, 0.0f, 1.0f);
    const float interpolation = std::min (dt * kIntermediateRate, 1.0f);
    const float maxOutputDelta = std::min (dt * kOutputSlewRate, 1.0f);
    float* const output[2] = { this->audio64Left, this->audio64Right };
    const float* const input[2] = { this->rawAudio64Left, this->rawAudio64Right };

    for (int channel = 0; channel < 2; channel++) {
	for (int block = 0; block < 8; block++) {
	    const int firstBand = block * 8;
	    float target = input[channel][firstBand];
	    for (int offset = 1; offset < 8; offset++) {
		target = std::max (target, input[channel][firstBand + offset]);
	    }

	    float& envelope = this->m_peakEnvelope[channel][block];
	    const float difference = target - envelope;
	    if (std::abs (difference) <= kEpsilon) {
		envelope = target;
	    } else {
		const float rate = difference > 0.0f ? kEnvelopeRisePerSecond : kEnvelopeFallPerSecond;
		const float step = std::min (std::abs (difference), dt) * rate;
		envelope += std::copysign (step, difference);
	    }

	    const float normalization = std::max (envelope, kEnvelopeFloor);
	    for (int offset = 0; offset < 8; offset++) {
		const int band = firstBand + offset;
		const float normalized = input[channel][band] / normalization;
		float& intermediate = this->m_intermediateSpectrum[channel][band];
		intermediate += (normalized - intermediate) * interpolation;
		const float delta = std::clamp (intermediate - output[channel][band], -maxOutputDelta, maxOutputDelta);
		output[channel][band] += delta;
	    }
	}
    }

    for (int band = 0; band < 64; band++) {
	this->audio64Average[band] = (this->audio64Left[band] + this->audio64Right[band]) * 0.5f;
    }

    // The native provider exposes 16/32/64 resolutions. Peak-preserving reduction keeps
    // narrow tones visible instead of selecting the last member of each group.
    for (int band = 0; band < 32; band++) {
	const int first = band * 2;
	this->audio32Left[band] = std::max (this->audio64Left[first], this->audio64Left[first + 1]);
	this->audio32Right[band] = std::max (this->audio64Right[first], this->audio64Right[first + 1]);
	this->audio32Average[band] = (this->audio32Left[band] + this->audio32Right[band]) * 0.5f;
    }
    for (int band = 0; band < 16; band++) {
	const int first = band * 4;
	this->audio16Left[band] = *std::max_element (this->audio64Left + first, this->audio64Left + first + 4);
	this->audio16Right[band] = *std::max_element (this->audio64Right + first, this->audio64Right + first + 4);
	this->audio16Average[band] = (this->audio16Left[band] + this->audio16Right[band]) * 0.5f;
    }
}

} // namespace WallpaperEngine::Audio::Drivers::Recorders
