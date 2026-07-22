#include <SDL.h>

#include "CSound.h"

#include "WallpaperEngine/FileSystem/Container.h"

using namespace WallpaperEngine::Render::Objects;

CSound::CSound (Wallpapers::CScene& scene, const Sound& sound) : CObject (scene, sound), m_sound (sound) {
    if (this->getContext ().getApp ().getContext ().settings.audio.enabled) {
	// Value keys: screens showing the same wallpaper parse their own Project copies, so data
	// addresses differ — but title/workshop id (wallpaper) and object id/file list (sound)
	// are identical, collapsing duplicates into one playback candidate per authored sound.
	const auto& project = this->getScene ().getScene ().project;
	this->m_wallpaperKey = project.title + "|" + project.workshopId;
	this->m_soundKey = std::to_string (this->m_sound.id);
	for (const auto& cur : this->m_sound.sounds) {
	    this->m_soundKey += "|" + cur;
	}

	this->getScene ().getAudioContext ().registerSoundCandidate (this->m_wallpaperKey, this->m_soundKey, this);
	this->m_registered = true;
    }
}

CSound::~CSound () {
    this->unload ();

    if (this->m_registered) {
	this->getScene ().getAudioContext ().unregisterSoundCandidate (this->m_wallpaperKey, this->m_soundKey, this);
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

void CSound::unload () {
    // free all the sound buffers and streams
    for (const auto& stream : this->m_audioStreams) {
	// Wake a decoder which may be waiting for packets before removeStream waits
	// for the SDL callback mutex.
	stream.second->stop ();
	this->getScene ().getAudioContext ().removeStream (stream.first);
	delete stream.second;
    }

    this->m_audioStreams.clear ();
}

void CSound::render () {
    if (!this->m_registered) {
	return;
    }

    auto& audioContext = this->getScene ().getAudioContext ();
    const bool active = audioContext.isActiveSoundPlayer (this->m_wallpaperKey, this->m_soundKey, this);

    if (active && this->m_audioStreams.empty ()) {
	// became the audible soundtrack (startup, rotation, or the previous owner went away):
	// streams start from the top of the track
	this->load ();
    } else if (!active && !this->m_audioStreams.empty ()) {
	this->unload ();
    } else if (active && audioContext.distinctSoundWallpaperCount () > 1) {
	// several wallpapers have music: rotate to the next one when this track finishes a
	// full pass (the read thread counts completions at demuxer EOF, loop or not)
	for (const auto& [id, stream] : this->m_audioStreams) {
	    if (stream->getCompletionCount () > 0) {
		this->unload ();
		audioContext.advanceActiveSound ();
		break;
	    }
	}
    }
}
