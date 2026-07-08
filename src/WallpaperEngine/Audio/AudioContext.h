#pragma once

#include <libavutil/samplefmt.h>
#include <string>
#include <vector>

#include "WallpaperEngine/Application/ApplicationContext.h"
#include "WallpaperEngine/Audio/Drivers/Recorders/PulseAudioPlaybackRecorder.h"

namespace WallpaperEngine {
namespace Application {
    class ApplicationContext;
}

namespace Audio {
    namespace Drivers {
	class AudioDriver;

	namespace Recorders {
	    class PulseAudioPlaybackRecorder;
	}
    } // namespace Drivers

    class AudioStream;

    class AudioContext {
    public:
	explicit AudioContext (Drivers::AudioDriver& driver);

	/**
	 * Registers the given stream in the driver for playing
	 *
	 * @param stream
	 */
	int addStream (AudioStream* stream) const;

	/**
	 * @param streamId The stream to stop playing
	 */
	void removeStream (int streamId) const;

	/**
	 * Soundtrack coordinator: at most one wallpaper's audio is audible at a time across any
	 * number of monitors. Sound objects register under a wallpaper key (value-based, so the
	 * same wallpaper on several screens shares it) plus a per-sound key (so a wallpaper with
	 * several authored Sound objects plays all of them together, deduped per screen). One
	 * wallpaper is active at a time; advanceActiveSound() rotates round-robin through the
	 * distinct wallpapers — the active CSound calls it when its track finishes a full pass.
	 */
	void registerSoundCandidate (const std::string& wallpaper, const std::string& sound, const void* instance);
	void unregisterSoundCandidate (const std::string& wallpaper, const std::string& sound, const void* instance);
	/** @return true when this instance is the one that should be audible for its sound right now */
	[[nodiscard]] bool
	isActiveSoundPlayer (const std::string& wallpaper, const std::string& sound, const void* instance) const;
	/** @return how many distinct wallpapers with audio are registered */
	[[nodiscard]] size_t distinctSoundWallpaperCount () const;
	/** Rotate the active soundtrack to the next distinct wallpaper in registration order */
	void advanceActiveSound ();

	/**
	 * TODO: MAYBE THIS SHOULD BE OUR OWN DEFINITIONS INSTEAD OF LIBRARY SPECIFIC ONES?
	 *
	 * @return The audio format the driver supports
	 */
	[[nodiscard]] AVSampleFormat getFormat () const;
	/**
	 * @return The sample rate the driver supports
	 */
	[[nodiscard]] int getSampleRate () const;
	/**
	 * @return The channels the driver supports
	 */
	[[nodiscard]] int getChannels () const;
	/**
	 * @return The application context under which the audio driver is initialized
	 */
	Application::ApplicationContext& getApplicationContext () const;
	/**
	 * @return The audio recorder to use to capture stereo mix data
	 */
	[[nodiscard]] Drivers::Recorders::PlaybackRecorder& getRecorder () const;

	/**
	 * @return The audio driver used to playback and record audio
	 */
	[[nodiscard]] Drivers::AudioDriver& getDriver () const;

    private:
	/** One registered CSound instance, in registration order */
	struct SoundCandidate {
	    std::string wallpaper;
	    std::string sound;
	    const void* instance;
	};

	/** The audio driver in use */
	Drivers::AudioDriver& m_driver;
	/** Registered wallpaper sounds, see registerSoundCandidate */
	std::vector<SoundCandidate> m_soundCandidates = {};
	/** The wallpaper whose sounds are currently audible; empty = none */
	std::string m_activeSoundWallpaper;
    };
} // namespace Audio
} // namespace WallpaperEngine