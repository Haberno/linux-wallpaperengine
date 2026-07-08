#include "AudioContext.h"
#include "WallpaperEngine/Audio/Drivers/AudioDriver.h"

#include <algorithm>
#include <set>

namespace WallpaperEngine::Audio {
AudioContext::AudioContext (Drivers::AudioDriver& driver) : m_driver (driver) { }

int AudioContext::addStream (AudioStream* stream) const { return this->m_driver.addStream (stream); }
void AudioContext::removeStream (int streamId) const { this->m_driver.removeStream (streamId); }

void AudioContext::registerSoundCandidate (
    const std::string& wallpaper, const std::string& sound, const void* instance
) {
    this->m_soundCandidates.push_back ({ .wallpaper = wallpaper, .sound = sound, .instance = instance });

    if (this->m_activeSoundWallpaper.empty ()) {
	this->m_activeSoundWallpaper = wallpaper;
    }
}

void AudioContext::unregisterSoundCandidate (
    const std::string& wallpaper, const std::string& sound, const void* instance
) {
    const bool wasActive = wallpaper == this->m_activeSoundWallpaper;

    std::erase_if (this->m_soundCandidates, [&] (const SoundCandidate& candidate) {
	return candidate.wallpaper == wallpaper && candidate.sound == sound && candidate.instance == instance;
    });

    // if the active wallpaper lost its last sound, hand playback to the next wallpaper
    if (wasActive
	&& std::ranges::none_of (this->m_soundCandidates, [&] (const SoundCandidate& candidate) {
	       return candidate.wallpaper == wallpaper;
	   })) {
	this->m_activeSoundWallpaper.clear ();
	for (const auto& candidate : this->m_soundCandidates) {
	    this->m_activeSoundWallpaper = candidate.wallpaper;
	    break;
	}
    }
}

bool AudioContext::isActiveSoundPlayer (
    const std::string& wallpaper, const std::string& sound, const void* instance
) const {
    if (wallpaper != this->m_activeSoundWallpaper) {
	return false;
    }

    // every authored sound of the active wallpaper plays, but only through its first-registered
    // instance — screens duplicating the wallpaper stay silent
    for (const auto& candidate : this->m_soundCandidates) {
	if (candidate.wallpaper == wallpaper && candidate.sound == sound) {
	    return candidate.instance == instance;
	}
    }

    return false;
}

size_t AudioContext::distinctSoundWallpaperCount () const {
    std::set<std::string> wallpapers;
    for (const auto& candidate : this->m_soundCandidates) {
	wallpapers.insert (candidate.wallpaper);
    }
    return wallpapers.size ();
}

void AudioContext::advanceActiveSound () {
    // distinct wallpapers in registration order
    std::vector<std::string> wallpapers;
    for (const auto& candidate : this->m_soundCandidates) {
	if (std::ranges::find (wallpapers, candidate.wallpaper) == wallpapers.end ()) {
	    wallpapers.push_back (candidate.wallpaper);
	}
    }

    if (wallpapers.empty ()) {
	this->m_activeSoundWallpaper.clear ();
	return;
    }

    const auto current = std::ranges::find (wallpapers, this->m_activeSoundWallpaper);
    if (current == wallpapers.end () || std::next (current) == wallpapers.end ()) {
	this->m_activeSoundWallpaper = wallpapers.front ();
    } else {
	this->m_activeSoundWallpaper = *std::next (current);
    }
}

AVSampleFormat AudioContext::getFormat () const { return this->m_driver.getFormat (); }

int AudioContext::getSampleRate () const { return this->m_driver.getSampleRate (); }

int AudioContext::getChannels () const { return this->m_driver.getChannels (); }

Application::ApplicationContext& AudioContext::getApplicationContext () const {
    return this->m_driver.getApplicationContext ();
}

Drivers::Recorders::PlaybackRecorder& AudioContext::getRecorder () const { return this->m_driver.getRecorder (); }

Drivers::AudioDriver& AudioContext::getDriver () const { return this->m_driver; }
} // namespace WallpaperEngine::Audio