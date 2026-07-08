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

private:
    std::map<int, Audio::AudioStream*> m_audioStreams = {};

    const Sound& m_sound;
    /** whether this instance claimed playback of m_sound (see AudioContext::claimSound) */
    bool m_ownsPlayback = false;
};
} // namespace WallpaperEngine::Render::Objects
