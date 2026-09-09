#pragma once

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
    void play ();
    void pause ();
    void stop ();
    /** Authored playback state, independent of output volume and duplicate-screen ownership. */
    [[nodiscard]] bool isPlaying () const { return m_playing && !m_waiting; }

protected:
    void load ();
    void unload ();
    [[nodiscard]] float getGain () const;

private:
    std::map<int, Audio::AudioStream*> m_audioStreams = {};

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
    float m_previousTime = 0.0f;
    uint32_t m_lastCompletion = 0;
    std::mt19937 m_random { std::random_device {}() };
};
} // namespace WallpaperEngine::Render::Objects
