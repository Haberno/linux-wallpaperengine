#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <glm/geometric.hpp>

#include "CSound.h"

#include "WallpaperEngine/Audio/Drivers/AudioDriver.h"
#include "WallpaperEngine/FileSystem/Container.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Camera.h"

using namespace WallpaperEngine::Render::Objects;

CSound::CSound (Wallpapers::CScene& scene, const Sound& sound) :
    CObject (scene, sound), ScriptableObject (scene, sound), m_sound (sound), m_playing (!sound.startSilent) {
    this->registerProperty ("volume", *this->m_sound.volume->value);
    this->registerProperty ("spatialization", *this->m_sound.spatialization->value);
    this->registerProperty ("attenuation", *this->m_sound.attenuation->value);
    this->registerProperty ("mindistance", *this->m_sound.minDistance->value);
    this->registerProperty ("mintime", *this->m_sound.minTime->value);
    this->registerProperty ("maxtime", *this->m_sound.maxTime->value);
    if (!this->getScene ().isLoading ()) this->activate ();
}

void CSound::activate () {
    if (this->m_registered) return;
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
    this->m_pendingDelay = delay;
    this->m_pendingFile = file;
    // Reading the file and opening a stream (probing it, building the decoder and resampler) took
    // 50-100 ms that hitched a frame whenever a layer started, e.g. as a switch transition finished.
    // Build the stream on a worker; unload () waits for it, so the locator outlives the task.
    this->m_pendingStream = std::async (
	std::launch::async,
	[&context = this->getScene ().getAudioContext (), &locator = this->getAssetLocator (), file, repeat] {
	    return std::make_unique<Audio::AudioStream> (context, locator.read (file), repeat);
	}
    );
}

bool CSound::attachPendingStream () {
    if (!this->m_pendingStream.valid ()) {
	return false;
    }
    if (this->m_pendingStream.wait_for (std::chrono::seconds::zero ()) != std::future_status::ready) {
	return true;
    }
    auto stream = this->m_pendingStream.get ();
    this->m_attenuation = 1.0f;
    this->m_minDistance = 1.0f;
    this->m_distanceGain = 1.0f;
    // Sample the original channel layout before attaching to the SDL callback.
#if FF_API_OLD_CHANNEL_LAYOUT
    this->m_sourceChannels = stream->getContext ()->channels;
#else
    this->m_sourceChannels = stream->getContext ()->ch_layout.nb_channels;
#endif
    if (this->m_sound.spatialization->value->getBool ()) {
	const float time = this->getScene ().getTime ();
	const float attenuation = this->m_sound.attenuation->evaluateFloat (time);
	const float minDistance = this->m_sound.minDistance->evaluateFloat (time);
	// OpenAL rejects negative source parameters, retaining their unity defaults.
	if (attenuation >= 0.0f) this->m_attenuation = attenuation;
	if (minDistance >= 0.0f) this->m_minDistance = minDistance;
	this->updateDistanceGain ();
    }
    stream->setGain (this->getGain () * this->m_distanceGain);
    stream->setPaused (!this->m_playing);
    this->m_nextStartRemaining = std::max (0.0, stream->getDuration () + this->m_pendingDelay);
    this->m_lastCompletion = 0;
    this->m_waiting = false;
    this->m_started = true;
    if (std::getenv ("WPE_SOUND_TRACE") != nullptr) {
	sLog.out (
	    "Sound layer ", this->m_sound.id, " start file=", this->m_pendingFile,
	    " mode=", this->m_sound.playbackmode.value_or ("loop"), " next=", this->m_nextStartRemaining,
	    " distance_gain=", this->m_distanceGain, " channels=", this->m_sourceChannels
	);
    }
    const int id = this->getScene ().getAudioContext ().addStream (stream.get ());
    this->m_audioStreams.emplace (id, stream.release ());
    return false;
}

void CSound::unload (const bool recordSingleCompletion) {
    // A stream still being opened is discarded; this waits for its worker, which only
    // happens when the layer stops within the brief window while it opens.
    this->m_pendingStream = {};
    // free all the sound buffers and streams
    for (const auto& stream : this->m_audioStreams) {
	// Wake a decoder which may be waiting for packets before removeStream waits
	// for the SDL callback mutex.
	stream.second->stop ();
	this->getScene ().getAudioContext ().removeStream (stream.first);
	if (recordSingleCompletion) {
	    // A companion can hand ownership away before this layer sees its EOF.
	    // removeStream has waited for the callback, so completion is now stable.
	    const auto completed = stream.second->getPlaybackCompletionCount ();
	    if (completed != this->m_lastCompletion) {
		this->m_lastCompletion = completed;
		this->m_playing = false;
		if (std::getenv ("WPE_SOUND_TRACE") != nullptr) {
		    sLog.out ("Sound layer ", this->m_sound.id, " completed pass=", completed);
		}
	    }
	}
	delete stream.second;
    }

    this->m_audioStreams.clear ();
}

void CSound::render () {
    if (this->getScene ().isLoading ()) return;
    const float time = this->getScene ().getTime ();
    const float delta = std::max (0.0f, time - this->m_previousTime);
    this->m_previousTime = time;
    if (!this->m_registered) {
	return;
    }

    auto& audioContext = this->getScene ().getAudioContext ();
    const bool active = audioContext.isActiveSoundPlayer (this->m_wallpaperKey, this->m_soundKey, this);
    const auto mode = this->m_sound.playbackmode.value_or ("loop");
    // Scheduling follows authored volume, independently of distance attenuation.
    const float gain = this->getGain ();
    this->updateDistanceGain ();
    for (const auto& [id, stream] : this->m_audioStreams) {
	stream->setGain (gain * this->m_distanceGain);
	stream->setPaused (!this->m_playing);
    }

    if (!active) {
	this->unload (mode == "single");
	return;
    }
    if (this->attachPendingStream ()) {
	return;
    }
    if (!this->m_playing) {
	if (mode == "single" && this->m_started && !this->m_paused
	    && audioContext.distinctSoundWallpaperCount () > 1) {
	    // A completed single must relinquish a later soundtrack turn, but
	    // another playing layer in this wallpaper still owns that turn.
	    const bool companionPlaying = std::ranges::any_of (
		this->getScene ().getObjectsByRenderOrder (), [&audioContext] (const CObject* object) {
		    const auto* sound = dynamic_cast<const CSound*> (object);
		    return sound != nullptr && sound->m_playing
			&& audioContext.isActiveSoundPlayer (sound->m_wallpaperKey, sound->m_soundKey, sound);
		}
	    );
	    if (!companionPlaying) {
		audioContext.advanceActiveSound ();
	    }
	}
	return;
    }
    const bool outputEnabled = audioContext.getApplicationContext ().state.audio.volume > 0
	&& !audioContext.getDriver ().getAudioDetector ().anythingPlaying ();
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
	    if (mode == "single") {
		// Retain completion before ownership can move to another wallpaper.
		this->m_playing = false;
	    }
	    if (audioContext.distinctSoundWallpaperCount () > 1) {
		this->unload ();
		this->m_nextStartRemaining = 0.0;
		audioContext.advanceActiveSound ();
		return;
	    }
	    if (mode == "single") {
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

float CSound::calculateDistanceGain (
    const glm::vec3& source, const glm::vec3& listener, const bool orthogonal, const glm::vec2& canvas,
    const float attenuation, const float minDistance, const int sourceChannels
) {
    // OpenAL's default spatialization only attenuates mono buffers.
    if (sourceChannels != 1 || !(minDistance > 0.0f)) return 1.0f;
    float distance = glm::length (source - listener);
    if (orthogonal) {
	// Native 1401f5460 normalizes the listener-space position before applying
	// depth relative to one quarter of the authored canvas width plus height.
	const float depth = (canvas.x + canvas.y) * 0.25f;
	distance = std::max (0.0f, depth - source.z) / depth;
    }
    // Native OpenAL Soft 1.21.1 uses inverse-distance-clamped attenuation.
    // Zero reference distance or a nonpositive denominator leaves unity gain.
    const float denominator = minDistance + attenuation * (std::max (distance, minDistance) - minDistance);
    return denominator > 0.0f ? minDistance / denominator : 1.0f;
}

void CSound::updateDistanceGain () {
    if (!this->m_sound.spatialization->value->getBool ()) return;
    // Turning spatialization off stops position updates; native handles retain
    // their previous position and gain until the next playback is created.
    const auto& camera = this->getScene ().getCamera ();
    const glm::vec3 source (this->resolveWorldMatrix ()[3]);
    // Native orthographic normalization at the listener produces an invalid
    // position, which OpenAL rejects while retaining the previous position.
    if (camera.isOrthogonal () && source == camera.getEye ()) return;
    this->m_distanceGain = calculateDistanceGain (
	source, camera.getEye (), camera.isOrthogonal (),
	{ camera.getWidth (), camera.getHeight () }, this->m_attenuation, this->m_minDistance, this->m_sourceChannels
    );
}
