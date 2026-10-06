#include "SoundSpatialMixer.h"

#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <dlfcn.h>
#include <map>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace WallpaperEngine::Audio {
namespace {
// The pinned1.21.1 experimental callback extension includes a final flags argument.
using BufferCallback = void (*) (ALuint, ALenum, ALsizei, SoundSpatialMixer::Reader, void*, unsigned);
ALenum sourceFormat (int channels) {
    switch (channels) {
        case 1: return AL_FORMAT_MONO_FLOAT32;
        case 2: return AL_FORMAT_STEREO_FLOAT32;
        case 4: return AL_FORMAT_QUAD32;
        case 6: return AL_FORMAT_51CHN32;
        case 7: return AL_FORMAT_61CHN32;
        case 8: return AL_FORMAT_71CHN32;
        default: throw std::runtime_error ("Unsupported native sound channel layout");
    }
}
}

struct SoundSpatialMixer::State {
    void* library = nullptr;
    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;
    int sampleRate = 0;
    int hrtf = -1;
    bool headphones = false;
    std::string version;
    std::map<Source, ALuint> buffers;
#define API(name) decltype (&::name) name = nullptr
    API (alcLoopbackOpenDeviceSOFT);
    API (alcCreateContext);
    API (alcResetDeviceSOFT);
    API (alcDestroyContext);
    API (alcCloseDevice);
    API (alcSetThreadContext);
    API (alcGetThreadContext);
    API (alcGetStringiSOFT);
    API (alcGetIntegerv);
    API (alcGetError);
    API (alcRenderSamplesSOFT);
    API (alGetString);
    API (alGetError);
    API (alGenBuffers);
    API (alDeleteBuffers);
    API (alGenSources);
    API (alDeleteSources);
    API (alSourcei);
    API (alSourcef);
    API (alSource3f);
    API (alSourcePlay);
    API (alSourcePause);
    API (alSourceStop);
    API (alGetSourcei);
#undef API
    BufferCallback alBufferCallbackSOFT = nullptr;

    struct Binding {
        State& owner;
        ALCcontext* previous;
        explicit Binding (State& state) : owner (state), previous (state.alcGetThreadContext ()) {
            if (!owner.alcSetThreadContext (owner.context)) throw std::runtime_error ("Cannot bind sound mixer context");
        }
        ~Binding () { owner.alcSetThreadContext (previous); }
    };

    void check () {
        const auto error = alGetError ();
        if (error != AL_NO_ERROR) throw std::runtime_error ("Native sound mixer error " + std::to_string (error));
    }

    ~State () {
        if (context) {
            const auto previous = alcGetThreadContext ();
            alcSetThreadContext (context);
            for (const auto& [source, buffer] : buffers) {
                alSourceStop (source);
                alDeleteSources (1, &source);
                alDeleteBuffers (1, &buffer);
            }
            alcSetThreadContext (previous == context ? nullptr : previous);
            alcDestroyContext (context);
        }
        if (device) alcCloseDevice (device);
        if (library) dlclose (library);
    }
};

SoundSpatialMixer::SoundSpatialMixer (const std::string& library, int sampleRate, bool headphones) :
    m_state (std::make_unique<State> ()) {
    auto& state = *m_state;
    state.sampleRate = sampleRate;
    state.headphones = headphones;
    // A distinct path and exact-handle symbol lookup prevent a transitive host
    // OpenAL dependency from replacing the pinned callback ABI or filter data.
    state.library = dlopen (library.c_str (), RTLD_NOW | RTLD_LOCAL);
    if (!state.library) throw std::runtime_error ("Cannot load private sound mixer: " + std::string (dlerror ()));
#define LOAD(name) \
    state.name = reinterpret_cast<decltype (state.name)> (dlsym (state.library, #name)); \
    if (!state.name) throw std::runtime_error ("Missing private sound mixer entry " #name)
    LOAD (alcLoopbackOpenDeviceSOFT);
    LOAD (alcCreateContext);
    LOAD (alcResetDeviceSOFT);
    LOAD (alcDestroyContext);
    LOAD (alcCloseDevice);
    LOAD (alcSetThreadContext);
    LOAD (alcGetThreadContext);
    LOAD (alcGetStringiSOFT);
    LOAD (alcGetIntegerv);
    LOAD (alcGetError);
    LOAD (alcRenderSamplesSOFT);
    LOAD (alGetString);
    LOAD (alGetError);
    LOAD (alGenBuffers);
    LOAD (alDeleteBuffers);
    LOAD (alGenSources);
    LOAD (alDeleteSources);
    LOAD (alSourcei);
    LOAD (alSourcef);
    LOAD (alSource3f);
    LOAD (alSourcePlay);
    LOAD (alSourcePause);
    LOAD (alSourceStop);
    LOAD (alGetSourcei);
    LOAD (alBufferCallbackSOFT);
#undef LOAD
    state.device = state.alcLoopbackOpenDeviceSOFT (nullptr);
    if (!state.device) throw std::runtime_error ("Cannot open in-memory sound mixer");
    int count = 0, hrtf = -1;
    state.alcGetIntegerv (state.device, ALC_NUM_HRTF_SPECIFIERS_SOFT, 1, &count);
    for (int index = 0; index < count; ++index) {
        const auto* name = state.alcGetStringiSOFT (state.device, ALC_HRTF_SPECIFIER_SOFT, index);
        if (name && std::string (name) == "Built-In HRTF") hrtf = index;
    }
    if (hrtf < 0) throw std::runtime_error ("Private sound mixer has no native HRTF data");
    state.hrtf = hrtf;
    const ALCint attributes[] = {
        ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT,
        ALC_FREQUENCY, sampleRate, ALC_HRTF_SOFT, headphones ? ALC_TRUE : ALC_FALSE,
        ALC_HRTF_ID_SOFT, hrtf, ALC_OUTPUT_LIMITER_SOFT, ALC_FALSE, 0
    };
    state.context = state.alcCreateContext (state.device, attributes);
    if (!state.context) throw std::runtime_error ("Cannot configure in-memory sound mixer");
    State::Binding binding (state);
    state.version = state.alGetString (AL_VERSION);
    if (state.version.find ("1.21.1") == std::string::npos)
        throw std::runtime_error ("Unexpected private sound mixer version");
    int enabled = 0;
    state.alcGetIntegerv (state.device, ALC_HRTF_SOFT, 1, &enabled);
    if (enabled != int (headphones) || state.alcGetError (state.device) != ALC_NO_ERROR)
        throw std::runtime_error ("Requested sound output mode is unavailable");
}

SoundSpatialMixer::~SoundSpatialMixer () = default;

std::string SoundSpatialMixer::defaultLibrary () {
    Dl_info info {};
    if (!dladdr (reinterpret_cast<void*> (&SoundSpatialMixer::defaultLibrary), &info) || !info.dli_fname)
        throw std::runtime_error ("Cannot locate private sound mixer");
    return (std::filesystem::path (info.dli_fname).parent_path () / "liblwe-openal.so.1").string ();
}

void SoundSpatialMixer::setHeadphones (bool headphones) {
    auto& state = *m_state;
    if (headphones == state.headphones) return;
    State::Binding binding (state);
    const ALCint attributes[] = {
        ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT,
        ALC_FREQUENCY, state.sampleRate, ALC_HRTF_SOFT, headphones ? ALC_TRUE : ALC_FALSE,
        ALC_HRTF_ID_SOFT, state.hrtf, ALC_OUTPUT_LIMITER_SOFT, ALC_FALSE, 0
    };
    if (!state.alcResetDeviceSOFT (state.device, attributes))
        throw std::runtime_error ("Cannot change sound output mode");
    state.headphones = headphones;
}

SoundSpatialMixer::Source SoundSpatialMixer::add (int channels, Reader reader, void* opaque, int sampleRate) {
    auto& state = *m_state;
    const auto format = sourceFormat (channels);
    State::Binding binding (state);
    ALuint source = 0, buffer = 0;
    try {
        state.alGenSources (1, &source);
        state.alGenBuffers (1, &buffer);
        state.check ();
        state.alBufferCallbackSOFT (buffer, format, sampleRate ? sampleRate : state.sampleRate, reader, opaque, 0);
        state.alSourcei (source, AL_BUFFER, static_cast<int> (buffer));
        state.check ();
        state.buffers.emplace (source, buffer);
        return source;
    } catch (...) {
        if (source) state.alDeleteSources (1, &source);
        if (buffer) state.alDeleteBuffers (1, &buffer);
        throw;
    }
}

void SoundSpatialMixer::remove (Source source) {
    auto& state = *m_state;
    State::Binding binding (state);
    const auto found = state.buffers.find (source);
    if (found == state.buffers.end ()) return;
    state.alSourceStop (source);
    state.alDeleteSources (1, &source);
    state.alDeleteBuffers (1, &found->second);
    state.buffers.erase (found);
    state.check ();
}

void SoundSpatialMixer::update (Source source, const Parameters& parameters) {
    auto& state = *m_state;
    State::Binding binding (state);
    state.alSourcef (source, AL_GAIN, parameters.gain);
    state.alSourcef (source, AL_ROLLOFF_FACTOR, parameters.attenuation);
    state.alSourcef (source, AL_REFERENCE_DISTANCE, parameters.minimumDistance);
    state.alSource3f (source, AL_POSITION, parameters.position[0], parameters.position[1], parameters.position[2]);
    ALint playback = 0;
    state.alGetSourcei (source, AL_SOURCE_STATE, &playback);
    if (parameters.paused && playback == AL_PLAYING) state.alSourcePause (source);
    else if (!parameters.paused && (playback == AL_INITIAL || playback == AL_PAUSED)) state.alSourcePlay (source);
    state.check ();
}

void SoundSpatialMixer::render (float* stereo, int frames) {
    auto& state = *m_state;
    State::Binding binding (state);
    state.alcRenderSamplesSOFT (state.device, stereo, frames);
    state.check ();
}

bool SoundSpatialMixer::stopped (Source source) {
    auto& state = *m_state;
    State::Binding binding (state);
    ALint playback = 0;
    state.alGetSourcei (source, AL_SOURCE_STATE, &playback);
    state.check ();
    return playback == AL_STOPPED;
}

const std::string& SoundSpatialMixer::version () const { return m_state->version; }
}
