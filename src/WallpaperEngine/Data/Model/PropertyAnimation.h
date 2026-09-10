#pragma once

#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace WallpaperEngine::Data::Model {
struct PropertyKeyframeHandle {
    bool enabled { false };
    /** X is normalized to half the segment's frame span; Y is an absolute value offset. */
    glm::vec2 offset { 0.0f };
};

/** A single keyframe and its incoming/outgoing Bezier handles. */
struct PropertyKeyframe {
    float frame;
    float value;
    PropertyKeyframeHandle incoming = {};
    PropertyKeyframeHandle outgoing = {};
};

/**
 * Keyframed animation attached to an object property (e.g. an animated origin that
 * moves a layer across the scene). Channels c0..cN map to the vector components of
 * the property being animated.
 */
struct PropertyAnimation {
    /** Keyframes per vector component, keyed by channel index (c0 = x, c1 = y, c2 = z) */
    std::map<int, std::vector<PropertyKeyframe>> channels;
    /** Animation timeline speed in frames per second */
    float fps;
    /** Timeline length in frames */
    float length;
    /** "loop" repeats, "mirror" repeats forwards/backwards, "single" clamps at the end. */
    std::string mode;
    /** Whether channel values offset the base property value instead of replacing it */
    bool relative;
    struct Event {
	float frame;
	std::string name;
    };
    std::string name;
    std::vector<Event> events;
    float rate = 1.0f;
    bool playing = true;
    float anchorTime = 0.0f;
    float anchorFrame = 0.0f;
    float previousEventFrame = -0.0001f;
    /** A linked property uses its parent's playback clock, retaining its own curves. */
    std::string parentKey;
    PropertyAnimation* timelineParent = nullptr;
    [[nodiscard]] const PropertyAnimation& playbackClock () const {
	return timelineParent != nullptr ? timelineParent->playbackClock () : *this;
    }

    [[nodiscard]] float elapsedFrame (float time) const;
    [[nodiscard]] float frameAt (float time) const;
    [[nodiscard]] bool isPlaying (float time) const;
    void play (float time);
    void pause (float time);
    void stop (float time);
    void setFrame (float frame, float time);
    void setRate (float value, float time);
    /** Events crossed since the previous tick, including loop and mirror boundaries. */
    [[nodiscard]] std::vector<Event> takeEvents (float time);

    /**
     * Samples one channel at the given time (in seconds), using the authored
     * time/value Bezier. Disabled handles collapse onto their keyframe.
     * Returns fallback when the channel has no keyframes.
     */
    [[nodiscard]] float evaluateChannel (int channel, float time, float fallback) const;

    /**
     * Samples channel 0 for a scalar property. Relative animations offset the
     * property's base value; absolute animations replace it.
     */
    [[nodiscard]] float evaluateFloat (float base, float time) const;

    /**
     * Samples channels 0-2 at the given time and combines them with the base value,
     * either as an offset (relative) or a replacement per animated channel.
     */
    [[nodiscard]] glm::vec3 evaluateVec3 (const glm::vec3& base, float time) const;
};
} // namespace WallpaperEngine::Data::Model
