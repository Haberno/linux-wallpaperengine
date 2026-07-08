#pragma once

#include "WallpaperEngine/Audio/AudioStream.h"
#include "WallpaperEngine/Render/CObject.h"

using namespace WallpaperEngine;

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Data::Model;

class CSound final : public CObject {
public:
    CSound (Wallpapers::CScene& scene, const Sound& sound);
    ~CSound () override;

    void render () override;

protected:
    void load ();
    void unload ();

private:
    std::map<int, Audio::AudioStream*> m_audioStreams = {};

    const Sound& m_sound;
    /** value keys identifying this wallpaper and sound across screens (see registerSoundCandidate) */
    std::string m_wallpaperKey;
    std::string m_soundKey;
    /** registered with the AudioContext soundtrack coordinator */
    bool m_registered = false;
};
} // namespace WallpaperEngine::Render::Objects
