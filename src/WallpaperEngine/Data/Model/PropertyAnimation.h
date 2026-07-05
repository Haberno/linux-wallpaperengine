#pragma once

#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace WallpaperEngine::Data::Model {
/**
 * A single keyframe of a property animation. Tangent handles (front/back) exist in the
 * editor data but interpolation falls back to linear when they're disabled, which is
 * the common case for workshop scenes.
 */
struct PropertyKeyframe {
    float frame;
    float value;
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
    /** Playback mode, "loop" repeats the timeline, anything else clamps at the end */
    std::string mode;
    /** Whether channel values offset the base property value instead of replacing it */
    bool relative;

    /**
     * Samples one channel at the given time (in seconds), interpolating linearly
     * between keyframes. Returns fallback when the channel has no keyframes.
     */
    [[nodiscard]] float evaluateChannel (int channel, float time, float fallback) const;

    /**
     * Samples channels 0-2 at the given time and combines them with the base value,
     * either as an offset (relative) or a replacement per animated channel.
     */
    [[nodiscard]] glm::vec3 evaluateVec3 (const glm::vec3& base, float time) const;
};
} // namespace WallpaperEngine::Data::Model
