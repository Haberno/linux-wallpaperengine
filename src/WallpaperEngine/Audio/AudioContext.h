#pragma once

#include <libavutil/samplefmt.h>
#include <set>
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
	 * Claims playback ownership of a scene sound. Screens showing the same wallpaper share
	 * one loaded Project — and therefore the same Sound data objects — so keying by the data
	 * address dedupes identical audio across monitors (only the first CSound plays) while
	 * different wallpapers keep their own audio untouched.
	 *
	 * @param sound The Sound data object's address
	 * @return true when the caller now owns playback, false when another screen already does
	 */
	bool claimSound (const void* sound);
	/**
	 * Releases a claim made by claimSound (call only when the claim succeeded).
	 *
	 * @param sound The Sound data object's address
	 */
	void releaseSound (const void* sound);

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
	/** The audio driver in use */
	Drivers::AudioDriver& m_driver;
	/** Sound data objects currently owned by a playing CSound, see claimSound */
	std::set<const void*> m_claimedSounds = {};
    };
} // namespace Audio
} // namespace WallpaperEngine