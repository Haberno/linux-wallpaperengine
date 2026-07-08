#include <SDL.h>

#include "CSound.h"

#include "WallpaperEngine/FileSystem/Container.h"

using namespace WallpaperEngine::Render::Objects;

CSound::CSound (Wallpapers::CScene& scene, const Sound& sound) : CObject (scene, sound), m_sound (sound) {
    if (this->getContext ().getApp ().getContext ().settings.audio.enabled) {
	// Screens showing the same wallpaper share the loaded Project, so this Sound data
	// object is the same instance across their scenes — claim it so only one screen
	// plays it (overlapping copies of the same track phase into an echo). Different
	// wallpapers have their own Sound instances and are unaffected.
	this->m_ownsPlayback = this->getScene ().getAudioContext ().claimSound (&sound);

	if (this->m_ownsPlayback) {
	    this->load ();
	}
    }
}

CSound::~CSound () {
    // free all the sound buffers and streams
    for (const auto& stream : this->m_audioStreams) {
	this->getScene ().getAudioContext ().removeStream (stream.first);
	delete stream.second;
    }

    this->m_audioStreams.clear ();

    if (this->m_ownsPlayback) {
	this->getScene ().getAudioContext ().releaseSound (&this->m_sound);
    }
}

void CSound::load () {
    for (const auto& cur : this->m_sound.sounds) {
	auto stream
	    = new Audio::AudioStream (this->getScene ().getAudioContext (), this->getAssetLocator ().read (cur));

	stream->setRepeat (this->m_sound.playbackmode.has_value () && this->m_sound.playbackmode == "loop");

	// add the stream to the context so it can be played
	this->m_audioStreams.insert_or_assign (this->getScene ().getAudioContext ().addStream (stream), stream);
    }
}

void CSound::render () {
    // self-heal after the owning screen switches away: the destroyed scene releases its
    // claim, and the surviving screen picks playback up on its next frame
    if (!this->m_ownsPlayback && this->getContext ().getApp ().getContext ().settings.audio.enabled) {
	this->m_ownsPlayback = this->getScene ().getAudioContext ().claimSound (&this->m_sound);

	if (this->m_ownsPlayback) {
	    this->load ();
	}
    }
}
