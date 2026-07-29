#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Application/ApplicationContext.h"
#include "WallpaperEngine/Audio/AudioContext.h"
#include "WallpaperEngine/Audio/AudioStream.h"
#include "WallpaperEngine/Audio/Drivers/AudioDriver.h"
#include "WallpaperEngine/Audio/Drivers/Detectors/AudioPlayingDetector.h"
#include "WallpaperEngine/Audio/Drivers/Recorders/PlaybackRecorder.h"
#include "WallpaperEngine/Data/Utils/MemoryStream.h"
#include "WallpaperEngine/Render/Drivers/Detectors/FullScreenDetector.h"

#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace {
using WallpaperEngine::Application::ApplicationContext;
using WallpaperEngine::Audio::AudioContext;
using WallpaperEngine::Audio::AudioStream;
using WallpaperEngine::Audio::Drivers::AudioDriver;
using WallpaperEngine::Audio::Drivers::Detectors::AudioPlayingDetector;
using WallpaperEngine::Audio::Drivers::Recorders::PlaybackRecorder;
using WallpaperEngine::Data::Utils::MemoryStream;
using WallpaperEngine::Render::Drivers::Detectors::FullScreenDetector;

class TestingAudioDriver final : public AudioDriver {
public:
    TestingAudioDriver (
	ApplicationContext& applicationContext, AudioPlayingDetector& detector, PlaybackRecorder& recorder
    ) : AudioDriver (applicationContext, detector, recorder) { }

    int addStream (AudioStream*) override { return 0; }
    void removeStream (int) override { }
    [[nodiscard]] AVSampleFormat getFormat () const override { return AV_SAMPLE_FMT_FLT; }
    [[nodiscard]] int getSampleRate () const override { return 48000; }
    [[nodiscard]] int getChannels () const override { return 2; }
};

void writeU16 (std::vector<char>& output, const size_t offset, const uint16_t value) {
    output[offset] = static_cast<char> (value & 0xff);
    output[offset + 1] = static_cast<char> ((value >> 8) & 0xff);
}

void writeU32 (std::vector<char>& output, const size_t offset, const uint32_t value) {
    output[offset] = static_cast<char> (value & 0xff);
    output[offset + 1] = static_cast<char> ((value >> 8) & 0xff);
    output[offset + 2] = static_cast<char> ((value >> 16) & 0xff);
    output[offset + 3] = static_cast<char> ((value >> 24) & 0xff);
}

std::shared_ptr<MemoryStream> shortPcmWave () {
    constexpr uint32_t sampleRate = 8000;
    constexpr uint32_t sampleCount = 800;
    constexpr uint16_t channelCount = 1;
    constexpr uint16_t bitsPerSample = 16;
    constexpr uint32_t dataSize = sampleCount * channelCount * bitsPerSample / 8;

    std::vector<char> wave (44 + dataSize, 0);
    std::memcpy (wave.data (), "RIFF", 4);
    writeU32 (wave, 4, 36 + dataSize);
    std::memcpy (wave.data () + 8, "WAVEfmt ", 8);
    writeU32 (wave, 16, 16);
    writeU16 (wave, 20, 1);
    writeU16 (wave, 22, channelCount);
    writeU32 (wave, 24, sampleRate);
    writeU32 (wave, 28, sampleRate * channelCount * bitsPerSample / 8);
    writeU16 (wave, 32, channelCount * bitsPerSample / 8);
    writeU16 (wave, 34, bitsPerSample);
    std::memcpy (wave.data () + 36, "data", 4);
    writeU32 (wave, 40, dataSize);

    auto buffer = std::make_unique<char[]> (wave.size ());
    std::memcpy (buffer.get (), wave.data (), wave.size ());
    return std::make_shared<MemoryStream> (std::move (buffer), wave.size ());
}
} // namespace

TEST_CASE ("looping audio resets its decoder in packet order", "[audio][loop]") {
    char executable[] = "tests";
    char* argv[] = { executable };
    ApplicationContext applicationContext (1, argv);
    applicationContext.state.general.keepRunning = true;
    applicationContext.state.audio.enabled = true;
    applicationContext.state.audio.volume = 0;

    FullScreenDetector fullscreenDetector (applicationContext);
    AudioPlayingDetector audioDetector (applicationContext, fullscreenDetector);
    PlaybackRecorder recorder;
    TestingAudioDriver driver (applicationContext, audioDetector, recorder);
    AudioContext audioContext (driver);
    AudioStream stream (audioContext, shortPcmWave (), true);

    std::array<uint8_t, 192000> decoded {};
    size_t decodedFrames = 0;
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);

    while (stream.getCompletionCount () < 250 && std::chrono::steady_clock::now () < deadline) {
	const int decodedSize = stream.decodeFrame (decoded.data (), decoded.size ());
	if (decodedSize > 0) {
	    decodedFrames++;
	} else {
	    std::this_thread::yield ();
	}
    }

    REQUIRE (stream.getCompletionCount () >= 250);
    REQUIRE (decodedFrames > 0);

    stream.stop ();
    applicationContext.state.general.keepRunning = false;
}
