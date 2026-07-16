#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Audio/Drivers/Recorders/PlaybackRecorder.h"
#include "WallpaperEngine/Audio/Drivers/Recorders/WallpaperEngineSpectrumAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using WallpaperEngine::Audio::Drivers::Recorders::PlaybackRecorder;
using WallpaperEngine::Audio::Drivers::Recorders::WallpaperEngineSpectrumAnalyzer;

TEST_CASE ("Wallpaper Engine spectrum uses the recovered frequency range and band curve") {
    WallpaperEngineSpectrumAnalyzer analyzer44100 (44100);
    REQUIRE (analyzer44100.frameCount () == 1920);
    REQUIRE (analyzer44100.analyzedBinCount () == 640);
    REQUIRE (analyzer44100.frequencyForBin (1) == Catch::Approx (22.96875f));
    REQUIRE (analyzer44100.frequencyForBin (639) == Catch::Approx (14677.03125f));
    REQUIRE (analyzer44100.bandForBin (1) == 0);
    REQUIRE (analyzer44100.bandForBin (17) == 16);
    REQUIRE (analyzer44100.bandForBin (639) == 63);

    WallpaperEngineSpectrumAnalyzer analyzer48000 (48000);
    REQUIRE (analyzer48000.frameCount () == 1920);
    REQUIRE (analyzer48000.analyzedBinCount () == 640);
    REQUIRE (analyzer48000.frequencyForBin (639) == Catch::Approx (15975.0f));

    WallpaperEngineSpectrumAnalyzer analyzer96000 (96000);
    REQUIRE (analyzer96000.frameCount () == 3840);
    REQUIRE (analyzer96000.analyzedBinCount () == 1280);
    REQUIRE (analyzer96000.frequencyForBin (1279) == Catch::Approx (31975.0f));
}

TEST_CASE ("Wallpaper Engine spectrum keeps stereo channels independent") {
    WallpaperEngineSpectrumAnalyzer analyzer (44100);
    std::vector<float> samples (analyzer.frameCount () * 2);
    const float leftFrequency = analyzer.frequencyForBin (17);
    const float rightFrequency = analyzer.frequencyForBin (300);

    for (std::size_t frame = 0; frame < analyzer.frameCount (); frame++) {
	const float time = static_cast<float> (frame) / 44100.0f;
	samples[frame * 2] = std::sin (2.0f * std::numbers::pi_v<float> * leftFrequency * time) * 0.1f;
	samples[frame * 2 + 1] = std::sin (2.0f * std::numbers::pi_v<float> * rightFrequency * time) * 0.1f;
    }

    std::array<float, 64> left {};
    std::array<float, 64> right {};
    analyzer.processInterleaved (samples.data (), analyzer.frameCount (), 2, left.data (), right.data ());

    const std::size_t leftPeak = std::distance (left.begin (), std::max_element (left.begin (), left.end ()));
    const std::size_t rightPeak = std::distance (right.begin (), std::max_element (right.begin (), right.end ()));
    REQUIRE (leftPeak == analyzer.bandForBin (17));
    REQUIRE (rightPeak == analyzer.bandForBin (300));
    REQUIRE (leftPeak != rightPeak);
    // Golden levels lock down the recovered frequency weighting and native 0.001 scale.
    REQUIRE (left[leftPeak] == Catch::Approx (0.48380512f).margin (0.0001f));
    REQUIRE (right[rightPeak] == Catch::Approx (5.457228184f).margin (0.0001f));
}

TEST_CASE ("scene spectra use native time-based normalization and peak reduction") {
    PlaybackRecorder recorder;
    std::array<float, 64> left {};
    std::array<float, 64> right {};
    left[3] = 2.0f;
    right[6] = 1.0f;

    recorder.setRawSpectrum (left.data (), right.data (), 0.05f);

    // The first block envelopes start at one. Left rises to 1.05; right stays at one.
    // dt*20 and dt*40 both saturate, so the final slew-limited values reach one.
    REQUIRE (recorder.audio64Left[3] == Catch::Approx (1.0f));
    REQUIRE (recorder.audio64Right[6] == Catch::Approx (1.0f));
    REQUIRE (recorder.audio64Average[3] == Catch::Approx (0.5f));
    REQUIRE (recorder.audio32Left[1] == Catch::Approx (1.0f));
    REQUIRE (recorder.audio16Left[0] == Catch::Approx (1.0f));
    REQUIRE (recorder.audio16Right[1] == Catch::Approx (1.0f));
    REQUIRE (recorder.audio16Average[0] == Catch::Approx (0.5f));
}
