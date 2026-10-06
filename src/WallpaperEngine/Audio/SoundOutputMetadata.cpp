#include "SoundOutputMetadata.h"
#include <cstring>
#include <pulse/pulseaudio.h>
#include <utility>

namespace WallpaperEngine::Audio {
namespace {
bool equal (const char* value, const char* expected) { return value && std::strcmp (value, expected) == 0; }
}

SoundOutput classifySoundOutput (unsigned channels, unsigned portType, const char* portName, const char* formFactor) {
    if (channels == 0) return SoundOutput::Unknown;
    if (channels != 2) return SoundOutput::Speakers;
    if (portType == PA_DEVICE_PORT_TYPE_HEADPHONES || portType == PA_DEVICE_PORT_TYPE_HEADSET)
        return SoundOutput::Headphones;
    if (portType == PA_DEVICE_PORT_TYPE_SPEAKER || portType == PA_DEVICE_PORT_TYPE_LINE
        || portType == PA_DEVICE_PORT_TYPE_HDMI || portType == PA_DEVICE_PORT_TYPE_SPDIF)
        return SoundOutput::Speakers;
    if (equal (portName, "analog-output-headphones")) return SoundOutput::Headphones;
    if (equal (formFactor, "headphone") || equal (formFactor, "headset")) return SoundOutput::Headphones;
    if (equal (formFactor, "speaker") || equal (formFactor, "tv") || equal (formFactor, "hifi"))
        return SoundOutput::Speakers;
    return SoundOutput::Unknown;
}

bool matchesSoundStream (const std::string& streamName, int processId, const char* name, const char* owner) {
    return name && owner && streamName == name && std::to_string (processId) == owner;
}

struct SoundOutputMetadata::State {
    std::string streamName;
    int processId;
    pa_mainloop* loop = nullptr;
    pa_context* context = nullptr;
    SoundOutput output = SoundOutput::Unknown;
    unsigned generation = 0, queriedGeneration = 0;
    unsigned matches = 0;
    uint32_t sink = PA_INVALID_INDEX, stream = PA_INVALID_INDEX;
    uint32_t foundSink = PA_INVALID_INDEX, foundStream = PA_INVALID_INDEX;
    bool dirty = true, pending = false;

    static void sinkInfo (pa_context*, const pa_sink_info* info, int end, void* opaque) {
        auto& state = *static_cast<State*> (opaque);
        if (end) {
            state.pending = false;
            if (end < 0) state.output = SoundOutput::Unknown;
            return;
        }
        if (!info || state.queriedGeneration != state.generation) return;
        state.output = classifySoundOutput (info->sample_spec.channels,
            info->active_port ? info->active_port->type : PA_DEVICE_PORT_TYPE_UNKNOWN,
            info->active_port ? info->active_port->name : nullptr,
            info->proplist ? pa_proplist_gets (info->proplist, PA_PROP_DEVICE_FORM_FACTOR) : nullptr);
    }

    static void streamInfo (pa_context* context, const pa_sink_input_info* info, int end, void* opaque) {
        auto& state = *static_cast<State*> (opaque);
        if (end) {
            if (state.queriedGeneration != state.generation) { state.pending = false; return; }
            if (end < 0 || state.matches != 1) {
                state.pending = false;
                state.output = SoundOutput::Unknown;
                return;
            }
            state.sink = state.foundSink;
            state.stream = state.foundStream;
            auto* operation = pa_context_get_sink_info_by_index (context, state.sink, sinkInfo, opaque);
            if (operation) pa_operation_unref (operation);
            else { state.pending = false; state.output = SoundOutput::Unknown; }
            return;
        }
        if (info && info->proplist && matchesSoundStream (state.streamName, state.processId, info->name,
            pa_proplist_gets (info->proplist, PA_PROP_APPLICATION_PROCESS_ID))) {
            ++state.matches;
            state.foundSink = info->sink;
            state.foundStream = info->index;
        }
    }

    static void changed (pa_context*, pa_subscription_event_type_t event, uint32_t index, void* opaque) {
        auto& state = *static_cast<State*> (opaque);
        const auto facility = event & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
        if (facility == PA_SUBSCRIPTION_EVENT_SINK && index != state.sink) return;
        ++state.generation;
        state.dirty = true;
        if ((facility == PA_SUBSCRIPTION_EVENT_SINK && index == state.sink)
            || (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT && index == state.stream))
            state.output = SoundOutput::Unknown;
    }

    static void connection (pa_context* context, void* opaque) {
        auto& state = *static_cast<State*> (opaque);
        if (pa_context_get_state (context) == PA_CONTEXT_READY) {
            pa_context_set_subscribe_callback (context, changed, opaque);
            auto* operation = pa_context_subscribe (context,
                static_cast<pa_subscription_mask_t> (PA_SUBSCRIPTION_MASK_SINK_INPUT | PA_SUBSCRIPTION_MASK_SINK),
                nullptr, nullptr);
            if (operation) pa_operation_unref (operation);
            state.dirty = true;
        } else {
            state.output = SoundOutput::Unknown;
        }
    }

    ~State () {
        if (context) { pa_context_disconnect (context); pa_context_unref (context); }
        if (loop) pa_mainloop_free (loop);
    }
};

SoundOutputMetadata::SoundOutputMetadata (std::string streamName, int processId) : m_state (std::make_unique<State> ()) {
    auto& state = *m_state;
    state.streamName = std::move (streamName);
    state.processId = processId;
    state.loop = pa_mainloop_new ();
    if (!state.loop) return;
    state.context = pa_context_new (pa_mainloop_get_api (state.loop), "Wallpaper audio output");
    if (!state.context) return;
    pa_context_set_state_callback (state.context, State::connection, &state);
    pa_context_connect (state.context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr);
}

SoundOutputMetadata::~SoundOutputMetadata () = default;

void SoundOutputMetadata::update () {
    auto& state = *m_state;
    if (!state.loop || !state.context) return;
    pa_mainloop_iterate (state.loop, 0, nullptr);
    if (pa_context_get_state (state.context) != PA_CONTEXT_READY || state.pending || !state.dirty) return;
    state.queriedGeneration = state.generation;
    state.matches = 0;
    state.foundSink = PA_INVALID_INDEX;
    state.foundStream = PA_INVALID_INDEX;
    state.pending = true;
    state.dirty = false;
    auto* operation = pa_context_get_sink_input_info_list (state.context, State::streamInfo, &state);
    if (operation) pa_operation_unref (operation);
    else { state.pending = false; state.output = SoundOutput::Unknown; }
}

SoundOutput SoundOutputMetadata::output () const { return m_state->output; }
}
