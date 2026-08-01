#include "PropertyAnimation.h"

#include <cmath>

using namespace WallpaperEngine::Data::Model;

namespace {
float cubicBezier (const float p0, const float p1, const float p2, const float p3, const float t) {
    const float inverse = 1.0f - t;
    return inverse * inverse * inverse * p0 + 3.0f * inverse * inverse * t * p1 + 3.0f * inverse * t * t * p2
	+ t * t * t * p3;
}
}

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

	const float amount = (frame - previous.frame) / span;
	if (!previous.outgoing.enabled || !next.incoming.enabled) {
	    return previous.value + (next.value - previous.value) * amount;
	}

	// Keyframe positions are frames, while the editor stores handle X offsets
	// in seconds. Convert the offsets to frames and solve the time curve before
	// sampling its value curve, matching camera-path/property animation playback.
	const float time0 = previous.frame;
	const float time1 = previous.frame + previous.outgoing.offset.x * this->fps;
	const float time2 = next.frame + next.incoming.offset.x * this->fps;
	const float time3 = next.frame;
	float lower = 0.0f;
	float upper = 1.0f;
	for (int iteration = 0; iteration < 24; iteration++) {
	    const float parameter = (lower + upper) * 0.5f;
	    if (cubicBezier (time0, time1, time2, time3, parameter) < frame) {
		lower = parameter;
	    } else {
		upper = parameter;
	    }
	}

	const float parameter = (lower + upper) * 0.5f;
	return cubicBezier (
	    previous.value, previous.value + previous.outgoing.offset.y, next.value + next.incoming.offset.y,
	    next.value, parameter
	);
    }

    return keyframes.back ().value;
}

float PropertyAnimation::evaluateFloat (const float base, const float time) const {
    const float value = this->evaluateChannel (0, time, this->relative ? 0.0f : base);
    return this->relative ? base + value : value;
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
