#pragma once

#include <map>
#include <memory>
#include <atomic>
#include "WallpaperEngine/Audio/SoundSpatialMixer.h"
#include "WallpaperEngine/Audio/SoundStreamReader.h"
#include "WallpaperEngine/Audio/SoundOutputMetadata.h"
#include <vector>

#include "WallpaperEngine/Audio/AudioStream.h"
#include "WallpaperEngine/Audio/Drivers/AudioDriver.h"

#include <SDL.h>

#define MAX_AUDIO_FRAME_SIZE 192000

namespace WallpaperEngine::Audio::Drivers {
/**
 * Audio output buffers for streams being played under SDL
 */
struct SDLAudioBuffer {
    AudioStream* stream = nullptr;
    uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2] = { 0 };
    unsigned int audio_buf_size = 0;
    unsigned int audio_buf_index = 0;
    SoundSpatialMixer::Source source = 0;
    std::unique_ptr<SoundStreamReader> reader;
    bool spatialPaused = true;
};

/**
 * SDL's audio driver implementation
 */
class SDLAudioDriver final : public AudioDriver {
public:
    SDLAudioDriver (
	Application::ApplicationContext& applicationContext, Detectors::AudioPlayingDetector& detector,
	Recorders::PlaybackRecorder& recorder
    );
    ~SDLAudioDriver () override;

    /** @inheritdoc */
    int addStream (AudioStream* stream) override;
    [[nodiscard]] bool supportsSpatialSound () const override { return this->m_spatialMixer != nullptr && !this->m_spatialFailed; }
    void update () override;
    /** The stream mutex is held by the SDL callback throughout this operation. */
    void mixSpatialSound (uint8_t* output, int length) noexcept;
    /** @inheritdoc */
    void removeStream (int streamId) override;
    /**
     * @return All the registered audio streams
     */
    const std::map<int, SDLAudioBuffer*>& getStreams ();

    /** @inheritdoc */
    [[nodiscard]] AVSampleFormat getFormat () const override;
    /** @inheritdoc */
    [[nodiscard]] int getSampleRate () const override;
    /** @inheritdoc */
    [[nodiscard]] int getChannels () const override;
    /**
     * @return The SDL's audio driver settings
     */
    [[nodiscard]] const SDL_AudioSpec& getSpec () const;

    [[nodiscard]] SDL_mutex* getStreamMutex () const;

private:
    /** The mutex lock used to access the stream list mutex */
    SDL_mutex* m_streamListMutex;
    /** The last streamID used */
    int m_lastStreamID = 0;
    /** The device's ID */
    SDL_AudioDeviceID m_deviceID = 0;
    /** If the driver is initialized or not */
    bool m_initialized = false;
    /** The sound output configuration */
    SDL_AudioSpec m_audioSpec {};
    std::unique_ptr<SoundSpatialMixer> m_spatialMixer;
    std::unique_ptr<SoundOutputMetadata> m_outputMetadata;
    bool m_headphones = false;
    std::atomic<bool> m_spatialFailed = false;
    bool m_reportedSpatialFailure = false;
    /** All the playable steams */
    std::map<int, SDLAudioBuffer*> m_streams {};
};
} // namespace WallpaperEngine::Audio::Drivers