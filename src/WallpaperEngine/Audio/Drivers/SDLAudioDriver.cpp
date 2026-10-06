#include "SDLAudioDriver.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"

#include <algorithm>
#include <cmath>
#include <unistd.h>
#include <array>
#include <cstring>

#define SDL_AUDIO_BUFFER_SIZE 4096
#define MAX_AUDIO_FRAME_SIZE 192000

using namespace WallpaperEngine::Audio;
using namespace WallpaperEngine::Audio::Drivers;

void audio_callback (void* userdata, uint8_t* streamData, int length) {
    auto* driver = static_cast<SDLAudioDriver*> (userdata);

    memset (streamData, 0, length);

    // if audio is playing do not do anything here!
    if (driver->getAudioDetector ().anythingPlaying ()) {
	return;
    }

    SDL_LockMutex (driver->getStreamMutex ());

    driver->mixSpatialSound (streamData, length);

    for (const auto& buffer : driver->getStreams () | std::views::values) {
        if (buffer->source != 0) continue;
	uint8_t* streamDataPointer = streamData;
	int streamLength = length;
	bool completedEmptyPass = false;
	const float gain = buffer->stream->getGain ()
	    * static_cast<float> (driver->getApplicationContext ().state.audio.volume) / SDL_MIX_MAXVOLUME;

	// sound is not initialized or stopped and is not in loop mode
	// ignore mixing it in
	if (!buffer->stream->isInitialized () || buffer->stream->isPaused ()) {
	    continue;
	}

	// check if queue is empty and signal the read thread
	if (buffer->stream->isQueueEmpty ()) {
	    SDL_CondSignal (buffer->stream->getWaitCondition ());
	}

	while (streamLength > 0 && driver->getApplicationContext ().state.general.keepRunning) {
	    if (buffer->audio_buf_index >= buffer->audio_buf_size) {
		// get more data to fill the buffer
		int audio_size = buffer->stream->decodeFrame (buffer->audio_buf, sizeof (buffer->audio_buf));

		if (audio_size == AVERROR_EOF) {
		    buffer->stream->notifyPlaybackCompletion ();
		    if (buffer->stream->isRepeat () && !completedEmptyPass) {
			completedEmptyPass = true;
			continue;
		    }
		}
		if (audio_size <= 0) {
		    // The output was cleared up front. Preserve an empty buffer so the
		    // next callback can retry without inventing a block of silent audio.
		    buffer->audio_buf_size = 0;
		    buffer->audio_buf_index = 0;
		    break;
		}
		buffer->audio_buf_size = audio_size;
		completedEmptyPass = false;

		buffer->audio_buf_index = 0;
	    }

	    int len1 = buffer->audio_buf_size - buffer->audio_buf_index;

	    if (len1 > streamLength) {
		len1 = streamLength;
	    }

	    if (driver->getSpec ().format == AUDIO_F32SYS) {
		// SDL's integer 0..128 mixer volume loses quiet authored gains after
		// squaring. Keep the native gain precision on the normal float output.
		auto* output = reinterpret_cast<float*> (streamDataPointer);
		const auto* input = reinterpret_cast<const float*> (&buffer->audio_buf[buffer->audio_buf_index]);
		const float channelGain[] = {
		    driver->getSpec ().channels == 2 ? buffer->stream->getStereoGain (0) : 1.0f,
		    driver->getSpec ().channels == 2 ? buffer->stream->getStereoGain (1) : 1.0f
		};
		for (int sample = 0; sample < len1 / static_cast<int> (sizeof (float)); sample++) {
		    output[sample] = std::clamp (output[sample] + input[sample] * gain * channelGain[sample % 2], -1.0f, 1.0f);
		}
	    } else {
		SDL_MixAudioFormat (
		    streamDataPointer, &buffer->audio_buf[buffer->audio_buf_index], driver->getSpec ().format, len1,
		    static_cast<int> (std::lround (std::clamp (gain, 0.0f, 1.0f) * SDL_MIX_MAXVOLUME))
		);
	    }

	    streamLength -= len1;
	    streamDataPointer += len1;
	    buffer->audio_buf_index += len1;
	}
    }

    // TODO: DO WE NEED TO ALSO LOCK WHILE THE AUDIO IS PLAYING? OR SOMEHOW WAIT UNTIL THE STREAM IS NOT IN USE ANYMORE?
    SDL_UnlockMutex (driver->getStreamMutex ());
}

SDLAudioDriver::SDLAudioDriver (
    Application::ApplicationContext& applicationContext, Detectors::AudioPlayingDetector& detector,
    Recorders::PlaybackRecorder& recorder
) : AudioDriver (applicationContext, detector, recorder), m_audioSpec () {
    this->m_streamListMutex = SDL_CreateMutex ();

    if (SDL_InitSubSystem (SDL_INIT_AUDIO) < 0) {
	sLog.error ("Cannot initialize SDL audio system, SDL_GetError: ", SDL_GetError ());
	sLog.error ("Continuing without audio support");

	return;
    }

    const SDL_AudioSpec requestedSpec = { .freq = 48000,
					  .format = AUDIO_F32,
					  .channels = 2,
					  .samples = SDL_AUDIO_BUFFER_SIZE,
					  .callback = audio_callback,
					  .userdata = this };

    const bool ownStreamHint = SDL_GetHint (SDL_HINT_AUDIO_DEVICE_STREAM_NAME) == nullptr;
    if (ownStreamHint) SDL_SetHint (SDL_HINT_AUDIO_DEVICE_STREAM_NAME, "Wallpaper sound");
    const std::string streamName = SDL_GetHint (SDL_HINT_AUDIO_DEVICE_STREAM_NAME)
        ? SDL_GetHint (SDL_HINT_AUDIO_DEVICE_STREAM_NAME) : "";
    this->m_deviceID
	= SDL_OpenAudioDevice (nullptr, false, &requestedSpec, &this->m_audioSpec, SDL_AUDIO_ALLOW_ANY_CHANGE);

    if (ownStreamHint) SDL_ResetHint (SDL_HINT_AUDIO_DEVICE_STREAM_NAME);

    if (this->m_deviceID == 0) {
	sLog.error ("SDL_OpenAudioDevice: ", SDL_GetError ());
	return;
    }

    if (this->m_audioSpec.format == AUDIO_F32SYS && this->m_audioSpec.channels == 2) {
        try {
            this->m_spatialMixer = std::make_unique<SoundSpatialMixer> (
                SoundSpatialMixer::defaultLibrary (), this->m_audioSpec.freq, false);
            const char* backend = SDL_GetCurrentAudioDriver ();
            if (!streamName.empty () && backend
                && (std::strcmp (backend, "pulseaudio") == 0 || std::strcmp (backend, "pipewire") == 0))
                this->m_outputMetadata = std::make_unique<SoundOutputMetadata> (streamName, getpid ());
        } catch (const std::exception& error) {
            this->m_spatialMixer.reset ();
            sLog.error ("Using ordinary sound output: ", error.what ());
        }
    }
    SDL_PauseAudioDevice (this->m_deviceID, 0);

    this->m_initialized = true;
}

SDLAudioDriver::~SDLAudioDriver () {
    if (this->m_initialized && this->m_deviceID != 0) {
	SDL_CloseAudioDevice (this->m_deviceID);
    }

    // Stop all pull callbacks before releasing their reader/decoder storage.
    this->m_spatialMixer.reset ();
    this->m_outputMetadata.reset ();
    for (auto* buffer : this->m_streams | std::views::values) {
	delete buffer;
    }
    this->m_streams.clear ();

    if (this->m_streamListMutex != nullptr) {
	SDL_DestroyMutex (this->m_streamListMutex);
	this->m_streamListMutex = nullptr;
    }

    if (this->m_initialized) {
	SDL_QuitSubSystem (SDL_INIT_AUDIO);
    }
}

int SDLAudioDriver::addStream (AudioStream* stream) {
    const int newStreamId = this->m_lastStreamID;
    this->m_lastStreamID++;

    SDL_LockMutex (this->m_streamListMutex);
    Data::Utils::ScopeGuard unlock ([&] { SDL_UnlockMutex (this->m_streamListMutex); });

    auto buffer = std::make_unique<SDLAudioBuffer> ();
    buffer->stream = stream;
    if (stream->usesSpatialMixer () && this->supportsSpatialSound ()) {
        buffer->reader = std::make_unique<SoundStreamReader> (
            std::span<uint8_t> (buffer->audio_buf), stream->getOutputChannels (),
            [] (void* state, uint8_t* output, int bytes) {
                auto* input = static_cast<AudioStream*> (state);
                if (input->isQueueEmpty ()) SDL_CondSignal (input->getWaitCondition ());
                const int decoded = input->decodeFrame (output, bytes);
                return decoded == AVERROR_EOF ? SoundStreamReader::End : std::max (0, decoded);
            }, stream, stream->getOutputSampleRate (), this->m_audioSpec.freq);
        try {
            buffer->source = this->m_spatialMixer->add (stream->getOutputChannels (), SoundStreamReader::read,
                buffer->reader.get (), stream->getOutputSampleRate ());
        } catch (const std::exception& error) {
            buffer->reader.reset ();
            stream->useLegacyOutput ();
            sLog.error ("Using ordinary sound stream: ", error.what ());
        }
    } else if (stream->usesSpatialMixer ()) {
        stream->useLegacyOutput ();
    }
    this->m_streams.emplace (newStreamId, buffer.release ());

    return newStreamId;
}
void SDLAudioDriver::removeStream (const int streamId) {
    SDLAudioBuffer* removed = nullptr;

    SDL_LockMutex (this->m_streamListMutex);
    if (const auto stream = this->m_streams.find (streamId); stream != this->m_streams.end ()) {
	removed = stream->second;
        if (removed->source && this->m_spatialMixer) {
            try { this->m_spatialMixer->remove (removed->source); }
            catch (...) {
                // A failed context binding must not leave a callback pointing
                // at the reader which is about to be destroyed.
                this->m_spatialMixer.reset ();
                this->m_spatialFailed = true;
            }
        }
	this->m_streams.erase (stream);
    }
    SDL_UnlockMutex (this->m_streamListMutex);

    delete removed;
}

const std::map<int, SDLAudioBuffer*>& SDLAudioDriver::getStreams () { return this->m_streams; }

AVSampleFormat SDLAudioDriver::getFormat () const {
    switch (this->m_audioSpec.format) {
	case AUDIO_U8:
	case AUDIO_S8:
	    return AV_SAMPLE_FMT_U8;
	case AUDIO_U16MSB:
	case AUDIO_U16LSB:
	case AUDIO_S16LSB:
	case AUDIO_S16MSB:
	    return AV_SAMPLE_FMT_S16;
	case AUDIO_S32LSB:
	case AUDIO_S32MSB:
	    return AV_SAMPLE_FMT_S32;
	case AUDIO_F32LSB:
	case AUDIO_F32MSB:
	    return AV_SAMPLE_FMT_FLT;
	default:
	    sLog.exception ("Cannot convert from SDL format to ffmpeg format, aborting...");
    }
}

int SDLAudioDriver::getSampleRate () const { return this->m_audioSpec.freq; }

int SDLAudioDriver::getChannels () const { return this->m_audioSpec.channels; }

const SDL_AudioSpec& SDLAudioDriver::getSpec () const { return this->m_audioSpec; }

SDL_mutex* SDLAudioDriver::getStreamMutex () const { return this->m_streamListMutex; }

void SDLAudioDriver::update () {
    AudioDriver::update ();
    if (this->m_spatialFailed && !this->m_reportedSpatialFailure) {
        sLog.error ("Private sound mixer failed; sound output is paused");
        this->m_reportedSpatialFailure = true;
    }
    if (!this->m_outputMetadata || this->m_spatialFailed) return;
    this->m_outputMetadata->update ();
    const bool headphones = this->m_outputMetadata->output () == SoundOutput::Headphones;
    if (headphones == this->m_headphones) return;
    SDL_LockMutex (this->m_streamListMutex);
    try {
        this->m_spatialMixer->setHeadphones (headphones);
        this->m_headphones = headphones;
    } catch (const std::exception& error) {
        sLog.error ("Cannot select sound output mode: ", error.what ());
    }
    SDL_UnlockMutex (this->m_streamListMutex);
}

void SDLAudioDriver::mixSpatialSound (uint8_t* output, int length) noexcept {
    if (!this->m_spatialMixer || this->m_spatialFailed) return;
    try {
        for (const auto& buffer : this->m_streams | std::views::values) {
            if (!buffer->source) continue;
            auto parameters = buffer->stream->getSpatialParameters ();
            parameters.paused = parameters.paused || !buffer->stream->isInitialized ();
            parameters.gain *= static_cast<float> (this->getApplicationContext ().state.audio.volume) / SDL_MIX_MAXVOLUME;
            buffer->spatialPaused = parameters.paused;
            buffer->reader->repeat = buffer->stream->isRepeat ();
            buffer->reader->tail = this->m_headphones ? SoundStreamReader::FilterTail : 0;
            this->m_spatialMixer->update (buffer->source, parameters);
        }
        const int frames = length / (2 * sizeof (float));
        this->m_spatialMixer->render (reinterpret_cast<float*> (output), frames);
        for (const auto& buffer : this->m_streams | std::views::values) {
            if (!buffer->source || buffer->spatialPaused) continue;
            const unsigned completed = buffer->reader->consumed (frames);
            for (unsigned pass = 0; pass < completed; ++pass) buffer->stream->notifyPlaybackCompletion ();
        }
        auto* samples = reinterpret_cast<float*> (output);
        for (int sample = 0; sample < frames * 2; ++sample) samples[sample] = std::clamp (samples[sample], -1.0f, 1.0f);
    } catch (...) {
        // Never unwind through SDL's C callback. Report once on the render thread.
        std::memset (output, 0, length);
        this->m_spatialFailed = true;
    }
}
