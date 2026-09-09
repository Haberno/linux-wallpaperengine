#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "CSound.h"

#include "WallpaperEngine/Audio/Drivers/AudioDriver.h"
#include "WallpaperEngine/FileSystem/Container.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Render::Objects;

CSound::CSound (Wallpapers::CScene& scene, const Sound& sound) :
    CObject (scene, sound), ScriptableObject (scene, sound), m_sound (sound), m_playing (!sound.startSilent) {
    this->registerProperty ("volume", *this->m_sound.volume->value);
    this->registerProperty ("mintime", *this->m_sound.minTime->value);
    this->registerProperty ("maxtime", *this->m_sound.maxTime->value);
    const auto& audioSettings = this->getContext ().getApp ().getContext ().settings.audio;
    if (audioSettings.enabled && audioSettings.volume > 0) {
	// A zero-volume wallpaper can still use live audio-input processing, but its authored
	// soundtrack must not open decoders. Audio-heavy scenes may list hundreds of candidate
	// files, and decoding them into an inaudible mix wastes a full CPU core and gigabytes.
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
    if (this->m_sound.sounds.empty ()) {
	return;
    }
    const auto mode = this->m_sound.playbackmode.value_or ("loop");
    // Native loop mode repeats one file directly, but selects a fresh alternative
    // after each pass when the layer lists several files (FUN_1401f5980).
    const bool repeat = mode == "loop" && this->m_sound.sounds.size () == 1;
    const auto index = std::uniform_int_distribution<size_t> (0, this->m_sound.sounds.size () - 1) (this->m_random);
    const auto& file = this->m_sound.sounds[index];
    auto stream = std::make_unique<Audio::AudioStream> (
	this->getScene ().getAudioContext (), this->getAssetLocator ().read (file), repeat
    );
    stream->setGain (this->getGain ());
    double delay = 0.0;
    if (mode == "random") {
	const float time = this->getScene ().getTime ();
	const float minimum = this->m_sound.minTime->evaluateFloat (time);
	const float maximum = this->m_sound.maxTime->evaluateFloat (time);
	const float fraction = std::uniform_real_distribution<float> (0.0f, 1.0f) (this->m_random);
	delay = minimum + fraction * (maximum - minimum);
	if (!std::isfinite (delay)) {
	    delay = 0.0;
	}
    }
    this->m_nextStartRemaining = std::max (0.0, stream->getDuration () + delay);
    this->m_lastCompletion = 0;
    this->m_waiting = false;
    this->m_started = true;
    if (std::getenv ("WPE_SOUND_TRACE") != nullptr) {
	sLog.out (
	    "Sound layer ", this->m_sound.id, " start file=", file, " mode=", mode, " next=", this->m_nextStartRemaining
	);
    }
    const int id = this->getScene ().getAudioContext ().addStream (stream.get ());
    this->m_audioStreams.emplace (id, stream.release ());
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
    const float time = this->getScene ().getTime ();
    const float delta = std::max (0.0f, time - this->m_previousTime);
    this->m_previousTime = time;
    if (!this->m_registered) {
	return;
    }

    auto& audioContext = this->getScene ().getAudioContext ();
    const bool active = audioContext.isActiveSoundPlayer (this->m_wallpaperKey, this->m_soundKey, this);
    const float gain = this->getGain ();
    for (const auto& [id, stream] : this->m_audioStreams) {
	stream->setGain (gain);
	stream->setPaused (!this->m_playing);
    }

    if (!active) {
	this->unload ();
	return;
    }
    if (!this->m_playing) {
	return;
    }
    const bool outputEnabled = audioContext.getApplicationContext ().state.audio.volume > 0
	&& !audioContext.getDriver ().getAudioDetector ().anythingPlaying ();
    const auto mode = this->m_sound.playbackmode.value_or ("loop");
    if (mode == "random" && gain > 0.0f && outputEnabled) {
	// Native random timing starts with duration + delay and freezes when the
	// combined gain is zero, even if an already playing handle finishes silently.
	this->m_nextStartRemaining = std::max (0.0, this->m_nextStartRemaining - delta);
    }
    if (!this->m_audioStreams.empty ()) {
	const auto* stream = this->m_audioStreams.begin ()->second;
	const auto completed = stream->getPlaybackCompletionCount ();
	if (completed != this->m_lastCompletion) {
	    this->m_lastCompletion = completed;
	    if (std::getenv ("WPE_SOUND_TRACE") != nullptr) {
		sLog.out ("Sound layer ", this->m_sound.id, " completed pass=", completed);
	    }
	    if (audioContext.distinctSoundWallpaperCount () > 1) {
		this->unload ();
		this->m_nextStartRemaining = 0.0;
		audioContext.advanceActiveSound ();
		return;
	    }
	    if (mode == "single") {
		this->m_playing = false;
		this->unload ();
		return;
	    }
	    if (!stream->isRepeat ()) {
		this->unload ();
		this->m_waiting = mode == "random";
	    }
	}
    }
    if (gain > 0.0f && outputEnabled && this->m_audioStreams.empty ()
	&& (!this->m_waiting || this->m_nextStartRemaining <= 0.0)) {
	this->load ();
    }
}

void CSound::play () {
    this->m_previousTime = this->getScene ().getTime ();
    this->m_playing = true;
    if (!this->m_paused) {
	// A playing single/loop layer restarts on play. Random mode keeps its
	// current interval; a paused layer resumes its existing buffer and timer.
	if (this->m_sound.playbackmode != "random" || !this->m_started || this->m_nextStartRemaining <= 0.0) {
	    this->unload ();
	    this->m_waiting = false;
	    this->m_started = false;
	    this->m_nextStartRemaining = 0.0;
	}
    }
    this->m_paused = false;
    for (const auto& [id, stream] : this->m_audioStreams) {
	stream->setPaused (false);
    }
}

void CSound::pause () {
    this->m_previousTime = this->getScene ().getTime ();
    if (this->m_playing) {
	this->m_paused = true;
    }
    this->m_playing = false;
    for (const auto& [id, stream] : this->m_audioStreams) {
	stream->setPaused (true);
    }
}

void CSound::stop () {
    this->m_playing = false;
    this->m_paused = false;
    this->m_waiting = false;
    this->m_started = false;
    this->m_nextStartRemaining = 0.0;
    this->unload ();
}

float CSound::getGain () const {
    const float volume = this->m_sound.volume->evaluateFloat (this->getScene ().getTime ());
    // Native sound gain (FUN_1401f4c20) squares the authored layer volume.
    const float gain = volume * volume;
    return std::isfinite (gain) ? gain : 0.0f;
}
