#include "PropertyAnimation.h"

#include <cmath>
#include <algorithm>

using namespace WallpaperEngine::Data::Model;

namespace {
float cubicBezier (const float p0, const float p1, const float p2, const float p3, const float t) {
    const float inverse = 1.0f - t;
    return inverse * inverse * inverse * p0 + 3.0f * inverse * inverse * t * p1 + 3.0f * inverse * t * t * p2
	+ t * t * t * p3;
}
}

double PropertyAnimation::elapsedFrame (const double time) const {
    if (timelineParent != nullptr) return timelineParent->elapsedFrame (time);
    return anchorFrame + (playing ? (time - anchorTime) * fps * rate : 0.0f);
}

float PropertyAnimation::frameAt (const double time) const {
    if (timelineParent != nullptr) return timelineParent->frameAt (time);
    double frame = elapsedFrame (time);
    if (length > 0.0f && (mode == "loop" || mode == "mirror")) {
	const double period = static_cast<double> (length) * (mode == "mirror" ? 2.0 : 1.0);
	frame = std::fmod (frame, period);
	if (frame < 0.0f) frame += period;
	if (mode == "mirror" && frame > length) frame = period - frame;
    } else if (length > 0.0f) {
	frame = std::clamp (frame, 0.0, static_cast<double> (length));
    }
    return static_cast<float> (frame);
}

bool PropertyAnimation::isPlaying (const double time) const {
    if (timelineParent != nullptr) return timelineParent->isPlaying (time);
    return playing && (mode != "single" || length <= 0.0f
	|| (rate < 0.0f ? elapsedFrame (time) > 0.0f : elapsedFrame (time) < length));
}

void PropertyAnimation::play (const double time) {
    if (timelineParent != nullptr) { timelineParent->play (time); return; }
    if (isPlaying (time)) return;

    // Keep the unfolded timeline when resuming the reverse half of a mirror.
    anchorFrame = elapsedFrame (time);
    if (mode == "single" && length > 0.0f
	&& (rate < 0.0f ? anchorFrame <= 0.0f : anchorFrame >= length)) {
	anchorFrame = rate < 0.0f ? length : 0.0f;
	previousEventFrame = anchorFrame - (rate < 0.0f ? -0.0001f : 0.0001f);
    }
    anchorTime = time;
    playing = true;
}

void PropertyAnimation::pause (const double time) {
    if (timelineParent != nullptr) { timelineParent->pause (time); return; }
    anchorFrame = elapsedFrame (time);
    anchorTime = time;
    playing = false;
}

void PropertyAnimation::stop (const double time) {
    if (timelineParent != nullptr) { timelineParent->stop (time); return; }
    setFrame (0.0f, time);
    playing = false;
}

void PropertyAnimation::setFrame (const float frame, const double time) {
    if (timelineParent != nullptr) { timelineParent->setFrame (frame, time); return; }
    if (!std::isfinite (frame)) return;
    anchorFrame = frame;
    anchorTime = time;
    previousEventFrame = frame - (rate < 0.0f ? -0.0001f : 0.0001f);
}

void PropertyAnimation::setRate (const float value, const double time) {
    if (timelineParent != nullptr) { timelineParent->setRate (value, time); return; }
    if (!std::isfinite (value)) return;
    anchorFrame = elapsedFrame (time);
    anchorTime = time;
    rate = value;
}

std::vector<PropertyAnimation::Event> PropertyAnimation::takeEvents (const double time) {
    std::vector<Event> result;
    const auto& clock = playbackClock ();
    if (!clock.playing) return result;
    const double current = elapsedFrame (time);
    const double previous = previousEventFrame;
    previousEventFrame = current;
    if (!std::isfinite (current) || !std::isfinite (previous) || current == previous) return result;
    const bool forward = current > previous;
    std::vector<std::pair<double, Event>> crossed;
    const auto append = [&] (const double position, const Event& event) {
	if (forward ? position > previous && position <= current : position < previous && position >= current) {
	    crossed.emplace_back (position, event);
	}
    };
    const bool repeats = clock.length > 0.0f && (clock.mode == "loop" || clock.mode == "mirror");
    const double period = static_cast<double> (clock.length) * (clock.mode == "mirror" ? 2.0 : 1.0);
    for (const auto& event : events) {
	if (event.frame < 0.0f || (clock.length > 0.0f && event.frame > clock.length)) continue;
	if (!repeats) {
	    append (event.frame, event);
	    continue;
	}
	const double last = std::floor (std::max (previous, current) / period);
	const double first = std::max (std::floor (static_cast<double> (std::min (previous, current)) / period), last - 1024.0);
	for (double cycle = first; cycle <= last; cycle++) {
	    // The tiny initial cursor offset includes frame zero, but must not
	    // synthesize an end event from an earlier cycle before playback began.
	    if (cycle < 0.0 && previous >= -0.0001f && current >= 0.0f) continue;
	    append (cycle * period + event.frame, event);
	    if (clock.mode == "mirror" && event.frame > 0.0f && event.frame < clock.length) {
		append (cycle * period + period - event.frame, event);
	    }
	}
    }
    std::stable_sort (crossed.begin (), crossed.end (), [forward] (const auto& left, const auto& right) {
	if (left.first == right.first) return forward ? left.second.frame > right.second.frame
	    : left.second.frame < right.second.frame;
	return forward ? left.first < right.first : left.first > right.first;
    });
    for (const auto& [position, event] : crossed) result.push_back (event);
    return result;
}

float PropertyAnimation::evaluateChannel (int channel, double time, float fallback) const {
    // The native property sampler blends whole-frame curve samples. Evaluating
    // the Bezier directly at fractional frames changes short authored loops.
    const float frame = frameAt (time);
    const float lower = std::floor (frame);
    const float value = evaluateFrame (channel, lower, fallback);
    const float blend = frame - lower;
    return blend == 0.0f ? value
	: value * (1.0f - blend) + evaluateFrame (channel, lower + 1.0f, fallback) * blend;
}

float PropertyAnimation::evaluateFrame (int channel, float frame, float fallback) const {
    const auto it = this->channels.find (channel);

    if (it == this->channels.end () || it->second.empty ()) {
	return fallback;
    }

    const auto& keyframes = it->second;
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

float PropertyAnimation::evaluateFloat (const float base, const double time) const {
    const float value = this->evaluateChannel (0, time, this->relative ? 0.0f : base);
    return this->relative ? base + value : value;
}

glm::vec3 PropertyAnimation::evaluateVec3 (const glm::vec3& base, double time) const {
    glm::vec3 result = base;

    for (int channel = 0; channel < 3; channel++) {
        const float fallback = this->relative ? 0.0f : result [channel];
        const float value = this->evaluateChannel (channel, time, fallback);

        result [channel] = this->relative ? result [channel] + value : value;
    }

    return result;
}
