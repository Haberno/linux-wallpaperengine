#include "WallpaperEngine/Audio/SoundSpatialMixer.h"
#include "WallpaperEngine/Audio/SoundStreamReader.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace WallpaperEngine::Audio;
namespace {
void check (bool value) { REQUIRE (value); }
struct Input {
    int channels = 1, cursor = 0, length = 1024, stalls = 0, passes = 0;
    bool repeat = false;
    static int decode (void* opaque, uint8_t* destination, int bytes) {
        auto& input = *static_cast<Input*> (opaque);
        if (input.stalls) { --input.stalls; return 0; }
        if (input.cursor == input.length) {
            if (input.repeat) input.cursor = 0;
            ++input.passes;
            return SoundStreamReader::End;
        }
        const int frames = std::min (bytes / int (sizeof (float)) / input.channels, input.length - input.cursor);
        auto* output = reinterpret_cast<float*> (destination);
        for (int i = 0; i < frames; ++i) for (int channel = 0; channel < input.channels; ++channel)
            output[i * input.channels + channel] = input.cursor + i == input.length - 1 && channel == 0 ? .125f : 0.0f;
        input.cursor += frames;
        return frames * input.channels * sizeof (float);
    }
};
std::vector<float> render (const char* library, int rate, bool hrtf, int channels, float distance,
                           float gain, const std::vector<int>& chunks) {
    SoundSpatialMixer mixer (library, rate, hrtf);
    Input input; input.channels = channels;
    std::array<uint8_t, 4096> buffer {};
    SoundStreamReader reader (buffer, channels, Input::decode, &input);
    reader.tail = hrtf ? SoundStreamReader::FilterTail : 0;
    auto source = mixer.add (channels, SoundStreamReader::read, &reader);
    SoundSpatialMixer::Parameters parameters;
    parameters.position = {0, 0, -distance}; parameters.gain = gain;
    mixer.update (source, parameters);
    std::vector<float> output;
    unsigned completed = 0, consumed = 0;
    for (int count : chunks) {
        const auto previous = output.size (); output.resize (previous + count * 2);
        mixer.render (output.data () + previous, count);
        consumed += count;
        completed += reader.consumed (count);
        if (consumed < unsigned (input.length) + reader.tail) check (completed == 0);
    }
    check (completed == 1 && input.passes == 1);
    std::array<float, 2048> silence {};
    mixer.render (silence.data (), 1024);
    check (mixer.stopped (source));
    check (reader.consumed (1024) == 0);
    mixer.remove (source);
    return output;
}
}
TEST_CASE ("sound spatial mixer preserves callback state and native source gains", "[audio][spatial]") {
    const std::string libraryPath = SoundSpatialMixer::defaultLibrary ();
    const char* library = libraryPath.c_str ();
    for (int rate : {44100, 48000}) for (bool hrtf : {false, true}) {
        const auto whole = render (library, rate, hrtf, 1, 1, 1, {2048});
        const auto pieces = render (library, rate, hrtf, 1, 1, 1, {13, 63, 127, 511, 310, 1024});
        check (whole.size () == pieces.size ());
        float energy = 0;
        for (size_t i = 0; i < whole.size (); ++i) {
            check (std::abs (whole[i] - pieces[i]) < 2e-6f);
            energy += whole[i] * whole[i];
        }
        check (energy > 0);
        const auto distant = render (library, rate, hrtf, 1, 4, .5f, {2048});
        for (size_t i = 0; i < whole.size (); ++i) check (std::abs (distant[i] - whole[i] * .125f) < 2e-6f);
        const auto stereo = render (library, rate, hrtf, 2, 1, 1, {2048});
        const auto stereoFar = render (library, rate, hrtf, 2, 4, 1, {2048});
        for (size_t i = 0; i < stereo.size (); ++i) check (std::abs (stereo[i] - stereoFar[i]) < 2e-6f);

        SoundSpatialMixer mixer (library, rate, hrtf);
        Input input; input.length = 4096; input.stalls = 1;
        std::array<uint8_t, 256> buffer {};
        SoundStreamReader reader (buffer, 1, Input::decode, &input);
        auto source = mixer.add (1, SoundStreamReader::read, &reader);
        SoundSpatialMixer::Parameters params; params.paused = true;
        mixer.update (source, params);
        std::array<float, 256> output {};
        mixer.render (output.data (), 128);
        check (input.stalls == 1 && input.cursor == 0);
        params.paused = false; mixer.update (source, params);
        mixer.render (output.data (), 128);
        check (input.stalls == 0 && input.cursor == 0 && !mixer.stopped (source));
        mixer.render (output.data (), 128);
        check (input.cursor > 0);
        const auto held = input.cursor;
        params.paused = true; mixer.update (source, params);
        mixer.render (output.data (), 128);
        check (input.cursor == held);
        mixer.render (output.data (), 128);
        check (input.cursor == held);
        mixer.setHeadphones (!hrtf);
        mixer.render (output.data (), 128);
        check (input.cursor == held);
        params.paused = false; mixer.update (source, params);
        mixer.render (output.data (), 128);
        check (input.cursor > held);
        mixer.remove (source);
    }
    // Loop boundaries are delivered after consumption, including read-ahead.
    Input loop; loop.repeat = true; loop.length = 3;
    std::array<uint8_t, 32> buffer {};
    SoundStreamReader reader (buffer, 1, Input::decode, &loop); reader.repeat = true;
    std::array<float, 10> samples {};
    check (SoundStreamReader::read (&reader, samples.data (), sizeof (samples)) == sizeof (samples));
    check (reader.consumed (2) == 0);
    check (reader.consumed (1) == 1);
    check (reader.consumed (7) == 2);
    check (samples[2] == .125f && samples[5] == .125f && samples[8] == .125f);
    Input finite; finite.length = 3;
    SoundStreamReader pending (buffer, 1, Input::decode, &finite);
    pending.tail = SoundStreamReader::FilterTail;
    check (SoundStreamReader::read (&pending, samples.data (), sizeof (samples)) == sizeof (samples));
    pending.tail = 0; // Current output changed, but the earlier tail is still pending.
    check (pending.consumed (3) == 0);
    check (pending.consumed (SoundStreamReader::FilterTail - 1) == 0);
    check (pending.consumed (1) == 1);

}

namespace RateCases {
struct Input {
    unsigned cursor = 0, length = 1024, passes = 0;
    bool repeating = false, periodic = false;
    static int decode (void* state, uint8_t* destination, int bytes) {
        auto& input = *static_cast<Input*> (state);
        if (input.cursor == input.length) {
            ++input.passes;
            if (input.repeating) input.cursor = 0;
            return SoundStreamReader::End;
        }
        const unsigned frames = std::min ({ unsigned (bytes / sizeof (float)), input.length - input.cursor, 3u });
        auto* output = reinterpret_cast<float*> (destination);
        for (unsigned i = 0; i < frames; ++i) {
            const unsigned position = input.cursor + i;
            output[i] = (input.periodic ? position % 7 == 6 : position == input.length - 1) ? .125f : 0.0f;
        }
        input.cursor += frames;
        return frames * sizeof (float);
    }
};
std::vector<float> render (const char* library, int rate, bool hrtf, bool repeated,
                          bool continuous, const std::vector<int>& chunks) {
    SoundSpatialMixer mixer (library, rate, hrtf);
    Input input;
    input.repeating = repeated;
    input.periodic = repeated || continuous;
    input.length = repeated ? 7 : continuous ? 7000 : 1024;
    std::array<uint8_t, 28> buffer {};
    SoundStreamReader reader (buffer, 1, Input::decode, &input, 32000, rate);
    reader.repeat = repeated;
    reader.tail = hrtf ? SoundStreamReader::FilterTail : 0;
    const auto source = mixer.add (1, SoundStreamReader::read, &reader, 32000);
    SoundSpatialMixer::Parameters parameters;
    parameters.position = {0, 0, -1}; parameters.paused = true;
    mixer.update (source, parameters);
    std::array<float, 26> paused {};
    mixer.render (paused.data (), 13);
    check (input.cursor == 0); // Paused output never advances the source-rate clock.
    parameters.paused = false; mixer.update (source, parameters);
    std::vector<float> output;
    unsigned completed = 0, consumed = 0;
    // Pinned mixer steps: round(32000/outputRate * 4096), not exact-ratio time.
    const unsigned step = rate == 48000 ? 2731 : 2972;
    for (const int count : chunks) {
        const size_t offset = output.size ();
        output.resize (offset + count * 2);
        mixer.render (output.data () + offset, count);
        consumed += count;
        completed += reader.consumed (count);
        if (repeated) {
            const uint64_t audible = consumed > reader.tail ? consumed - reader.tail : 0;
            check (completed == audible * step / (4096 * 7));
        } else if (!continuous) {
            const unsigned deadline = (1024 * 4096 + step - 1) / step + reader.tail;
            check (completed == unsigned (consumed >= deadline));
        }
    }
    float energy = 0;
    for (const float sample : output) energy += sample * sample;
    check (energy > 0);
    if (!repeated && !continuous) {
        check (completed == 1 && input.passes == 1);
        const unsigned deadline = (1024 * 4096 + step - 1) / step + reader.tail;
        for (size_t i = deadline * 2; i < output.size (); ++i) check (std::abs (output[i]) < 1e-9f);
    }
    mixer.remove (source);
    return output;
}
void run (const char* library) {
    for (const int rate : {44100, 48000}) for (const bool hrtf : {false, true}) {
        const auto whole = render (library, rate, hrtf, false, false, {2048});
        const auto pieces = render (library, rate, hrtf, false, false, {1, 17, 53, 127, 255, 383, 1212});
        for (size_t i = 0; i < whole.size (); ++i) check (std::abs (whole[i] - pieces[i]) < 2e-6f);
        const auto repeated = render (library, rate, hrtf, true, false, {1, 17, 53, 127, 255, 383, 1212});
        const auto continuous = render (library, rate, hrtf, false, true, {2048});
        for (size_t i = 0; i < repeated.size (); ++i) check (std::abs (repeated[i] - continuous[i]) < 2e-6f);
    }
    // A long source proves cumulative fixed-point accounting avoids early
    // completion; using the ideal sample-rate ratio would give a different deadline.
    Input longInput; longInput.length = 32000;
    std::vector<uint8_t> buffer (32000 * sizeof (float));
    SoundStreamReader reader (buffer, 1, Input::decode, &longInput, 32000, 48000);
    reader.tail = SoundStreamReader::FilterTail;
    std::vector<float> block (32500);
    check (SoundStreamReader::read (&reader, block.data (), block.size () * sizeof (float)) == int (block.size () * sizeof (float)));
    reader.tail = 0; // Pending completion keeps its original output-frame tail.
    constexpr unsigned deadline = (uint64_t (32000) * 4096 + 2730) / 2731 + SoundStreamReader::FilterTail;
    check (reader.consumed (deadline - 1) == 0);
    check (reader.consumed (1) == 1);
    check (reader.consumed (100) == 0);
}
}

TEST_CASE ("sound source rates preserve native stepping and gapless loop history", "[audio][spatial][source-rate]") {
    RateCases::run (SoundSpatialMixer::defaultLibrary ().c_str ());
}
