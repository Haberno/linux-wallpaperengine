#include "CameraPath.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

using namespace WallpaperEngine::Data::Model;

namespace {
float cubicBezier (const float p0, const float p1, const float p2, const float p3, const float t) {
    const float inverse = 1.0f - t;
    return inverse * inverse * inverse * p0 + 3.0f * inverse * inverse * t * p1 + 3.0f * inverse * t * t * p2
	+ t * t * t * p3;
}
}

float CameraPathChannel::evaluate (const float time, const float fallback) const {
    if (this->keyframes.empty ()) {
	return fallback;
    }
    if (time <= this->keyframes.front ().time) {
	return this->keyframes.front ().value;
    }
    if (time >= this->keyframes.back ().time) {
	return this->keyframes.back ().value;
    }

    for (size_t index = 1; index < this->keyframes.size (); index++) {
	const CameraPathKeyframe& previous = this->keyframes[index - 1];
	const CameraPathKeyframe& next = this->keyframes[index];
	if (time > next.time) {
	    continue;
	}

	const float span = next.time - previous.time;
	if (span <= 0.0f) {
	    return next.value;
	}

	const float amount = (time - previous.time) / span;
	if (this->interpolation == CameraPathInterpolation::LegacyHermite) {
	    // The legacy runtime gives both ends of each segment the same tangent:
	    // half of that segment's value delta. This is not linear except at the
	    // midpoint, and it is independent of the neighboring segments.
	    const float amountSquared = amount * amount;
	    const float amountCubed = amountSquared * amount;
	    const float tangent = (next.value - previous.value) * 0.5f;
	    const float previousWeight = 2.0f * amountCubed - 3.0f * amountSquared + 1.0f;
	    const float previousTangentWeight = amountCubed - 2.0f * amountSquared + amount;
	    const float nextWeight = -2.0f * amountCubed + 3.0f * amountSquared;
	    const float nextTangentWeight = amountCubed - amountSquared;
	    return previousWeight * previous.value + previousTangentWeight * tangent + nextWeight * next.value
		+ nextTangentWeight * tangent;
	}

	if (!previous.outgoing.enabled || !next.incoming.enabled) {
	    return glm::mix (previous.value, next.value, amount);
	}

	// The editor stores a cubic Bezier in the time/value plane. Find the
	// curve parameter for the requested time, then evaluate its value. The
	// authored camera curves are monotonic in time, so bisection is stable
	// even where automatic handles cross slightly around a short segment.
	const float time0 = previous.time;
	const float time1 = previous.time + previous.outgoing.offset.x;
	const float time2 = next.time + next.incoming.offset.x;
	const float time3 = next.time;
	float lower = 0.0f;
	float upper = 1.0f;
	for (int iteration = 0; iteration < 24; iteration++) {
	    const float parameter = (lower + upper) * 0.5f;
	    if (cubicBezier (time0, time1, time2, time3, parameter) < time) {
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

    return this->keyframes.back ().value;
}

CameraTransform CameraPath::evaluate (const float time, const CameraTransform& fallback) const {
    CameraTransform result = fallback;
    for (int channel = 0; channel < 3; channel++) {
	result.center[channel] = this->center[channel].evaluate (time, result.center[channel]);
	result.eye[channel] = this->eye[channel].evaluate (time, result.eye[channel]);
	result.up[channel] = this->up[channel].evaluate (time, result.up[channel]);
    }
    result.fov = this->fov.evaluate (time, result.fov);
    result.zoom = this->zoom.evaluate (time, result.zoom);

    if (glm::dot (result.up, result.up) > 0.000001f) {
	result.up = glm::normalize (result.up);
    } else {
	result.up = fallback.up;
    }
    result.fov = glm::clamp (result.fov, 1.0f, 179.0f);
    result.zoom = glm::max (result.zoom, 0.0001f);
    return result;
}
