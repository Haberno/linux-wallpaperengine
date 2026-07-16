#pragma once

#include "kiss_fft.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace WallpaperEngine::Audio::Drivers::Recorders {
/**
 * The raw 64-band capture processor used by Wallpaper Engine build 23967692.
 *
 * Wallpaper Engine analyzes 30 * 64 frames and 10 * 64 FFT bins at ordinary
 * 44.1/48 kHz mix rates. It maps those bins through a fourth-root curve and keeps the
 * strongest value assigned to each output band.
 */
class WallpaperEngineSpectrumAnalyzer final {
public:
    static constexpr std::size_t BandCount = 64;
    static constexpr std::size_t BaseFrameCount = 30 * BandCount;
    static constexpr std::size_t BaseAnalyzedBinCount = 10 * BandCount;

    explicit WallpaperEngineSpectrumAnalyzer (uint32_t sampleRate = 44100);
    ~WallpaperEngineSpectrumAnalyzer ();

    WallpaperEngineSpectrumAnalyzer (const WallpaperEngineSpectrumAnalyzer&) = delete;
    WallpaperEngineSpectrumAnalyzer& operator= (const WallpaperEngineSpectrumAnalyzer&) = delete;

    void configure (uint32_t sampleRate);
    void setInputVolume (float volume);
    void processInterleaved (
	const float* samples, std::size_t frameCount, unsigned int channelCount, float* left, float* right
    );

    [[nodiscard]] uint32_t sampleRate () const;
    [[nodiscard]] std::size_t frameCount () const;
    [[nodiscard]] std::size_t analyzedBinCount () const;
    [[nodiscard]] float frequencyForBin (std::size_t bin) const;
    [[nodiscard]] std::size_t bandForBin (std::size_t bin) const;

private:
    void processChannel (
	const float* samples, std::size_t frameCount, unsigned int channelCount, unsigned int channel, float* output
    );

    uint32_t m_sampleRate = 0;
    std::size_t m_frameCount = 0;
    std::size_t m_analyzedBinCount = 0;
    float m_inputVolume = 1.0f;
    kiss_fft_cfg m_fft = nullptr;
    std::vector<kiss_fft_cpx> m_fftInput;
    std::vector<kiss_fft_cpx> m_fftOutput;
};
} // namespace WallpaperEngine::Audio::Drivers::Recorders
