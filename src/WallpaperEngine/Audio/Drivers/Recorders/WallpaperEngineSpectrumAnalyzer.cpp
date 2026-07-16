#include "WallpaperEngineSpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace WallpaperEngine::Audio::Drivers::Recorders {
namespace {
    constexpr float kPcmScale = 127.0f;
    constexpr float kBandCurveExponent = 0.25f;
    constexpr float kFrequencyWeightCenter = 0.5009999871253967f;
    constexpr float kOutputScale = 0.001f;
} // namespace

WallpaperEngineSpectrumAnalyzer::WallpaperEngineSpectrumAnalyzer (const uint32_t sampleRate) {
    this->configure (sampleRate);
}

WallpaperEngineSpectrumAnalyzer::~WallpaperEngineSpectrumAnalyzer () {
    if (this->m_fft != nullptr) {
	kiss_fft_free (this->m_fft);
    }
}

void WallpaperEngineSpectrumAnalyzer::configure (const uint32_t sampleRate) {
    if (sampleRate == this->m_sampleRate && this->m_fft != nullptr) {
	return;
    }

    this->m_sampleRate = sampleRate == 0 ? 44100 : sampleRate;
    const std::size_t rateMultiplier = std::max<std::size_t> (this->m_sampleRate / 44100, 1);
    this->m_frameCount = BaseFrameCount * rateMultiplier;
    this->m_analyzedBinCount = BaseAnalyzedBinCount * rateMultiplier;

    if (this->m_fft != nullptr) {
	kiss_fft_free (this->m_fft);
    }
    this->m_fft = kiss_fft_alloc (static_cast<int> (this->m_frameCount), 0, nullptr, nullptr);
    this->m_fftInput.resize (this->m_frameCount);
    this->m_fftOutput.resize (this->m_frameCount);
}

void WallpaperEngineSpectrumAnalyzer::setInputVolume (const float volume) {
    this->m_inputVolume = volume > 0.0f ? volume : 1.0f;
}

uint32_t WallpaperEngineSpectrumAnalyzer::sampleRate () const { return this->m_sampleRate; }

std::size_t WallpaperEngineSpectrumAnalyzer::frameCount () const { return this->m_frameCount; }

std::size_t WallpaperEngineSpectrumAnalyzer::analyzedBinCount () const { return this->m_analyzedBinCount; }

float WallpaperEngineSpectrumAnalyzer::frequencyForBin (const std::size_t bin) const {
    return static_cast<float> (bin) * static_cast<float> (this->m_sampleRate) / static_cast<float> (this->m_frameCount);
}

std::size_t WallpaperEngineSpectrumAnalyzer::bandForBin (const std::size_t requestedBin) const {
    if (requestedBin == 0 || this->m_analyzedBinCount < 2) {
	return 0;
    }

    std::size_t previousBand = 0;
    const std::size_t lastBin = std::min (requestedBin, this->m_analyzedBinCount - 1);
    for (std::size_t bin = 1; bin <= lastBin; bin++) {
	const float position = static_cast<float> (bin - 1) / static_cast<float> (this->m_analyzedBinCount - 1);
	const std::size_t curvedBand = static_cast<std::size_t> (std::pow (position, kBandCurveExponent) * BandCount);
	previousBand = std::min ({ curvedBand, previousBand + 1, BandCount - 1 });
    }
    return previousBand;
}

void WallpaperEngineSpectrumAnalyzer::processInterleaved (
    const float* samples, const std::size_t frameCount, const unsigned int channelCount, float* left, float* right
) {
    if (this->m_fft == nullptr || samples == nullptr || channelCount == 0) {
	std::fill_n (left, BandCount, 0.0f);
	std::fill_n (right, BandCount, 0.0f);
	return;
    }

    this->processChannel (samples, frameCount, channelCount, 0, left);
    if (channelCount < 2) {
	std::memcpy (right, left, BandCount * sizeof (float));
    } else {
	this->processChannel (samples, frameCount, channelCount, 1, right);
    }
}

void WallpaperEngineSpectrumAnalyzer::processChannel (
    const float* samples, const std::size_t frameCount, const unsigned int channelCount, const unsigned int channel,
    float* output
) {
    for (std::size_t frame = 0; frame < this->m_frameCount; frame++) {
	const float sample = frame < frameCount ? samples[frame * channelCount + channel] : 0.0f;
	const float transformed = sample * kPcmScale + kPcmScale;
	this->m_fftInput[frame].r = transformed;
	this->m_fftInput[frame].i = 1.0f / transformed;
    }

    kiss_fft (this->m_fft, this->m_fftInput.data (), this->m_fftOutput.data ());
    std::fill_n (output, BandCount, 0.0f);

    std::size_t previousBand = 0;
    for (std::size_t bin = 1; bin < this->m_analyzedBinCount; bin++) {
	const float real = this->m_fftOutput[bin].r;
	const float imaginary = this->m_fftOutput[bin].i;
	float magnitudeSquared = real * real + imaginary * imaginary;
	if (!std::isfinite (magnitudeSquared)) {
	    magnitudeSquared = 0.0f;
	}

	const float position = static_cast<float> (bin - 1) / static_cast<float> (this->m_analyzedBinCount - 1);
	const std::size_t curvedBand = static_cast<std::size_t> (std::pow (position, kBandCurveExponent) * BandCount);
	const std::size_t band = std::min ({ curvedBand, previousBand + 1, BandCount - 1 });
	previousBand = band;

	const float phase = position * std::numbers::pi_v<float>;
	const float weight = kFrequencyWeightCenter - std::cos (phase) * (1.0f - kFrequencyWeightCenter);
	const float value = std::sqrt (weight * magnitudeSquared);
	output[band] = std::max (output[band], value);
    }

    const float scale = this->m_inputVolume * kOutputScale
	* (static_cast<float> (this->m_analyzedBinCount) / (static_cast<float> (this->m_frameCount) * 0.5f));
    for (std::size_t band = 0; band < BandCount; band++) {
	output[band] *= scale;
    }
}
} // namespace WallpaperEngine::Audio::Drivers::Recorders
