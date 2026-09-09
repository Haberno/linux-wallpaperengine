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

    if (this->length > 0.0f && (this->mode == "loop" || this->mode == "mirror")) {
	const float period = this->length * (this->mode == "mirror" ? 2.0f : 1.0f);
	frame = std::fmod (frame, period);
	if (frame < 0.0f) {
	    frame += period;
	}
	if (this->mode == "mirror" && frame > this->length) {
	    frame = period - frame;
	}
    }

    if (frame <= keyframes.front ().frame) {
	return keyframes.front ().value;
    }
    if (frame >= keyframes.back ().frame) {
	return keyframes.back ().value;
    }

    for (size_t i = 1; i < keyframes.size (); i++) {
	const auto& previous = keyframes[i - 1];
	const auto& next = keyframes[i];

	if (frame > next.frame) {
	    continue;
	}

	const float span = next.frame - previous.frame;
	if (span <= 0.0f) {
	    return next.value;
	}

	const float amount = (frame - previous.frame) / span;
	if (!previous.outgoing.enabled && !next.incoming.enabled) {
	    return previous.value + (next.value - previous.value) * amount;
	}

	// Property animations share the native camera-path curve format. Handle X
	// offsets scale with half this segment's length, independently of playback FPS.
	const glm::vec2 outgoing = previous.outgoing.enabled ? previous.outgoing.offset : glm::vec2 (0.0f);
	const glm::vec2 incoming = next.incoming.enabled ? next.incoming.offset : glm::vec2 (0.0f);
	const float time0 = previous.frame;
	const float time1 = previous.frame + outgoing.x * span * 0.5f;
	const float time2 = next.frame + incoming.x * span * 0.5f;
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
	    previous.value, previous.value + outgoing.y, next.value + incoming.y, next.value, parameter
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
