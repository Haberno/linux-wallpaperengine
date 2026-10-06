#pragma once

#include <future>
#include <memory>
#include <random>

#include "WallpaperEngine/Audio/AudioStream.h"
#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

using namespace WallpaperEngine;

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Data::Model;

class CSound final : virtual public CObject, public Scripting::ScriptableObject {
public:
    CSound (Wallpapers::CScene& scene, const Sound& sound);
    ~CSound () override;

    void render () override;
    void activate ();
    void play ();
    void pause ();
    void stop ();
    /** Authored playback state, independent of output volume and duplicate-screen ownership. */
    [[nodiscard]] bool isPlaying () const { return m_playing && !m_waiting; }
    /** Native listener coordinates, before the output-device speaker decoder. */
    [[nodiscard]] static glm::vec3 calculateAudioPosition (
	const glm::vec3& source, const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up,
	bool orthogonal, const glm::vec2& canvas
    );
    /** Non-HRTF stereo panpot gains, compensated for FFmpeg's mono-to-stereo matrix. */
    [[nodiscard]] static glm::vec2 calculateStereoGain (const glm::vec3& position);
    /** Native mono-source distance gain. */
    [[nodiscard]] static float calculateDistanceGain (
	const glm::vec3& source, const glm::vec3& listener, bool orthogonal, const glm::vec2& canvas,
	float attenuation, float minDistance, int sourceChannels
    );

protected:
    void load ();
    /** Adds a stream built by load () once its worker is done; true while it is still being built */
    bool attachPendingStream ();
    void unload (bool recordSingleCompletion = false);
    [[nodiscard]] float getGain () const;
    void updateDistanceGain ();

private:
    std::map<int, Audio::AudioStream*> m_audioStreams = {};
    /** Stream being opened off the render thread, and the random-mode delay chosen for it */
    std::future<std::unique_ptr<Audio::AudioStream>> m_pendingStream;
    double m_pendingDelay = 0.0;
    std::string m_pendingFile;

    const Sound& m_sound;
    /** value keys identifying this wallpaper and sound across screens (see registerSoundCandidate) */
    std::string m_wallpaperKey;
    std::string m_soundKey;
    /** registered with the AudioContext soundtrack coordinator */
    bool m_registered = false;
    bool m_playing = true;
    bool m_paused = false;
    bool m_waiting = false;
    bool m_started = false;
    double m_nextStartRemaining = 0.0;
    double m_previousTime = 0.0;
    /** OpenAL retains each handle's attenuation settings until the next playback. */
    float m_attenuation = 1.0f;
    float m_minDistance = 1.0f;
    float m_distanceGain = 1.0f;
    glm::vec2 m_stereoGain { 1.0f };
    glm::vec3 m_audioPosition { 0.0f };
    int m_sourceChannels = 0;
    uint32_t m_lastCompletion = 0;
    std::mt19937 m_random { std::random_device {}() };
};
} // namespace WallpaperEngine::Render::Objects
