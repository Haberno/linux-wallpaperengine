#include "PropertyAnimation.h"

#include <cmath>

using namespace WallpaperEngine::Data::Model;

float PropertyAnimation::evaluateChannel (int channel, float time, float fallback) const {
    const auto it = this->channels.find (channel);

    if (it == this->channels.end () || it->second.empty ()) {
        return fallback;
    }

    const auto& keyframes = it->second;
    float frame = time * this->fps;

    if (this->mode == "loop" && this->length > 0.0f) {
        frame = std::fmod (frame, this->length);
    }

    if (frame <= keyframes.front ().frame) {
        return keyframes.front ().value;
    }
    if (frame >= keyframes.back ().frame) {
        return keyframes.back ().value;
    }

    for (size_t i = 1; i < keyframes.size (); i++) {
        const auto& previous = keyframes [i - 1];
        const auto& next = keyframes [i];

        if (frame > next.frame) {
            continue;
        }

        const float span = next.frame - previous.frame;
        if (span <= 0.0f) {
            return next.value;
        }

        const float t = (frame - previous.frame) / span;
        return previous.value + (next.value - previous.value) * t;
    }

    return keyframes.back ().value;
}

glm::vec3 PropertyAnimation::evaluateVec3 (const glm::vec3& base, float time) const {
    glm::vec3 result = base;

    for (int channel = 0; channel < 3; channel++) {
        const float fallback = this->relative ? 0.0f : result [channel];
        const float value = this->evaluateChannel (channel, time, fallback);

        result [channel] = this->relative ? result [channel] + value : value;
    }

    return result;
}
