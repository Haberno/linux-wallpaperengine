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

// Solver constants lifted from the reference runtime: it seeds the curve
// parameter with the segment-relative position, then halves a step that starts
// just under 1.0 until the solved frame is within a hundredth of a frame.
constexpr float SOLVER_INITIAL_STEP = 0x1.ff7ceep-1f;
constexpr float SOLVER_EPSILON = 0.01f;
constexpr int SOLVER_ITERATIONS = 1000;
// An x offset of 1.0 reaches half the segment, so handles stay valid when the
// keyframes around them move.
constexpr float HANDLE_SCALE = 0.5f;
} // namespace

float CameraPathChannel::evaluateLegacy (const float time, const float fallback) const {
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

	// The legacy runtime gives both ends of each segment the same tangent:
	// half of that segment's value delta. This is not linear except at the
	// midpoint, and it is independent of the neighboring segments.
	const float amount = (time - previous.time) / span;
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

    return this->keyframes.back ().value;
}

float CameraPathChannel::evaluateFrame (const int frame, const float fallback) const {
    if (this->keyframes.empty ()) {
	return fallback;
    }
    if (frame <= this->keyframes.front ().frame) {
	return this->keyframes.front ().value;
    }
    if (frame >= this->keyframes.back ().frame) {
	return this->keyframes.back ().value;
    }

    for (size_t index = 1; index < this->keyframes.size (); index++) {
	const CameraPathKeyframe& previous = this->keyframes[index - 1];
	const CameraPathKeyframe& next = this->keyframes[index];
	if (frame >= next.frame) {
	    continue;
	}
	// A step keyframe holds the previous value for the whole segment ending on it.
	if (next.step) {
	    return previous.value;
	}

	const int span = next.frame - previous.frame;
	if (span <= 0) {
	    return next.value;
	}

	// The editor stores a cubic Bezier in the frame/value plane. Find the
	// curve parameter for the requested frame, then evaluate its value.
	const float target = static_cast<float> (frame);
	const float scale = static_cast<float> (span) * HANDLE_SCALE;
	const float frame0 = static_cast<float> (previous.frame);
	const float frame1 = frame0 + previous.outgoing.offset.x * scale;
	const float frame3 = static_cast<float> (next.frame);
	const float frame2 = frame3 + next.incoming.offset.x * scale;

	float parameter = static_cast<float> (frame - previous.frame) / static_cast<float> (span);
	float step = SOLVER_INITIAL_STEP;
	for (int iteration = 0; iteration < SOLVER_ITERATIONS; iteration++) {
	    const float solved = cubicBezier (frame0, frame1, frame2, frame3, parameter);
	    if (std::fabs (solved - target) < SOLVER_EPSILON) {
		break;
	    }
	    step *= 0.5f;
	    parameter += solved <= target ? step : -step;
	}

	parameter = glm::clamp (parameter, 0.0f, 1.0f);
	return cubicBezier (
	    previous.value, previous.value + previous.outgoing.offset.y, next.value + next.incoming.offset.y,
	    next.value, parameter
	);
    }

    return this->keyframes.back ().value;
}

CameraTransform CameraPath::evaluate (const float time, const CameraTransform& fallback) const {
    CameraTransform result = fallback;

    if (this->legacy) {
	for (int channel = 0; channel < 3; channel++) {
	    result.center[channel] = this->center[channel].evaluateLegacy (time, result.center[channel]);
	    result.eye[channel] = this->eye[channel].evaluateLegacy (time, result.eye[channel]);
	    result.up[channel] = this->up[channel].evaluateLegacy (time, result.up[channel]);
	}
	result.fov = this->fov.evaluateLegacy (time, result.fov);
	result.zoom = this->zoom.evaluateLegacy (time, result.zoom);
    } else {
	// Curve channels are baked per whole frame and the two frames around the
	// cursor are blended, so playback does not depend on the display rate.
	const float secondsPerFrame = this->secondsPerFrame > 0.0f ? this->secondsPerFrame : 1.0f;
	const int lower =
	    glm::clamp (static_cast<int> (time / secondsPerFrame), 0, glm::max (0, this->lengthFrames - 1));
	const int upper = glm::min (lower + 1, this->lengthFrames);
	const float blend = std::fmod (time, secondsPerFrame) / secondsPerFrame;
	const auto sample = [lower, upper, blend] (const CameraPathChannel& channel, const float channelFallback) {
	    return channel.evaluateFrame (lower, channelFallback) * (1.0f - blend)
		+ channel.evaluateFrame (upper, channelFallback) * blend;
	};

	for (int channel = 0; channel < 3; channel++) {
	    result.center[channel] = sample (this->center[channel], result.center[channel]);
	    result.eye[channel] = sample (this->eye[channel], result.eye[channel]);
	    result.up[channel] = sample (this->up[channel], result.up[channel]);
	}
	result.fov = sample (this->fov, result.fov);
	result.zoom = sample (this->zoom, result.zoom);
    }

    if (glm::dot (result.up, result.up) > 0.000001f) {
	result.up = glm::normalize (result.up);
    } else {
	result.up = fallback.up;
    }
    result.fov = glm::clamp (result.fov, 1.0f, 179.0f);
    result.zoom = glm::max (result.zoom, 0.0001f);
    return result;
}
