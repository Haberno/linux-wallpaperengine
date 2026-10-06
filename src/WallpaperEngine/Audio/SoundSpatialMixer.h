#pragma once

#include <array>
#include <memory>
#include <string>

namespace WallpaperEngine::Audio {
/** In-memory OpenAL mixer. The caller serializes access and owns reader lifetime. */
class SoundSpatialMixer {
public:
    using Reader = int (*) (void* state, void* destination, int bytes);
    using Source = unsigned;
    struct Parameters {
        std::array<float, 3> position {};
        float gain = 1.0f;
        float attenuation = 1.0f;
        float minimumDistance = 1.0f;
        bool paused = false;
    };

    SoundSpatialMixer (const std::string& library, int sampleRate, bool headphones);
    ~SoundSpatialMixer ();
    SoundSpatialMixer (const SoundSpatialMixer&) = delete;
    SoundSpatialMixer& operator= (const SoundSpatialMixer&) = delete;

    static std::string defaultLibrary ();
    void setHeadphones (bool headphones);
    Source add (int channels, Reader reader, void* state, int sampleRate = 0);
    void remove (Source source);
    void update (Source source, const Parameters& parameters);
    void render (float* stereo, int frames);
    bool stopped (Source source);
    const std::string& version () const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};
}
