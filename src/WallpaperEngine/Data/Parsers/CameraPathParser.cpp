#include "CameraPathParser.h"

#include <algorithm>

using namespace WallpaperEngine::Data::JSON;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;

namespace {
CameraPathHandle parseHandle (const JSON& keyframe, const std::string& name) {
    const auto handle = keyframe.optional (name);
    if (!handle.has_value () || !handle->is_object () || !handle->optional ("enabled", false)) {
	return {};
    }

    // The offsets are only read for an enabled handle; a disabled one keeps a
    // zero offset, which collapses its control point onto the keyframe.
    return CameraPathHandle {
	.enabled = true,
	.offset = glm::vec2 (handle->optional ("x", 0.0f), handle->optional ("y", 0.0f)),
    };
}

CameraPathChannel parseChannel (const JSON& data, const float fps) {
    CameraPathChannel result;
    if (!data.is_array ()) {
	return result;
    }

    const float safeFps = fps > 0.0f ? fps : 1.0f;
    // The reference loader never sorts: it keeps only keyframes whose frame
    // advances, so out-of-order or duplicated entries are dropped outright.
    int lastFrame = -1;
    for (const auto& keyframe : data) {
	if (!keyframe.is_object ()) {
	    continue;
	}
	const int frame = keyframe.optional ("frame", 0);
	if (frame <= lastFrame) {
	    continue;
	}
	lastFrame = frame;

	const bool step = keyframe.optional ("step", false);
	result.keyframes.push_back (
	    CameraPathKeyframe {
		.frame = frame,
		.time = static_cast<float> (frame) / safeFps,
		.value = keyframe.optional ("value", 0.0f),
		.step = step,
		// a step keyframe never reads its handles
		.incoming = step ? CameraPathHandle {} : parseHandle (keyframe, "back"),
		.outgoing = step ? CameraPathHandle {} : parseHandle (keyframe, "front"),
	    }
	);
    }
    return result;
}

/** wraploop closes a channel on itself: everything past the authored length is
 * dropped and the final keyframe restates the first value with a mirrored
 * incoming handle, so the seam is continuous instead of a cut. */
void closeWrapLoop (CameraPathChannel& channel, const int lengthFrames) {
    while (channel.keyframes.size () > 1 && channel.keyframes.back ().frame > lengthFrames) {
	channel.keyframes.pop_back ();
    }
    if (channel.keyframes.size () < 2) {
	return;
    }

    const CameraPathKeyframe first = channel.keyframes.front ();
    if (channel.keyframes.back ().frame != lengthFrames) {
	channel.keyframes.push_back (CameraPathKeyframe { .frame = lengthFrames });
    }

    CameraPathKeyframe& closing = channel.keyframes.back ();
    closing.value = first.value;
    closing.step = false;
    if (first.outgoing.enabled) {
	closing.incoming = CameraPathHandle { .enabled = true, .offset = -first.outgoing.offset };
    } else {
	closing.incoming.enabled = false;
    }
}

void parseVectorChannels (const JSON& data, const float fps, std::array<CameraPathChannel, 3>& output) {
    if (!data.is_object ()) {
	return;
    }
    for (int channel = 0; channel < 3; channel++) {
	const auto value = data.optional ("c" + std::to_string (channel));
	if (value.has_value ()) {
	    output[channel] = parseChannel (*value, fps);
	}
    }
}

CameraPath parseLegacyPath (const JSON& data) {
    CameraPath result {
	.name = data.optional<std::string> ("name", ""),
	.duration = data.optional ("duration", 0.0f),
	.legacy = true,
    };

    const auto transforms = data.optional ("transforms");
    if (!transforms.has_value () || !transforms->is_array ()) {
	return result;
    }

    const size_t transformCount = transforms->size ();
    for (size_t index = 0; index < transformCount; index++) {
	const auto& transform = (*transforms)[index];
	if (!transform.is_object ()) {
	    continue;
	}
	if (transform.optional ("disabled", false)) {
	    continue;
	}

	float time = 0.0f;
	if (const auto timestamp = transform.optional<float> ("timestamp"); timestamp.has_value ()) {
	    time = *timestamp;
	} else if (index != 0 && transformCount > 1) {
	    // Disabled and malformed entries still occupy an index in the official
	    // timestamp formula because it uses the original JSON array length.
	    time = static_cast<float> (index) / static_cast<float> (transformCount - 1) * result.duration;
	}
	const glm::vec3 center = transform.optional ("center", glm::vec3 (0.0f));
	const glm::vec3 eye = transform.optional ("eye", glm::vec3 (0.0f));
	const glm::vec3 up = transform.optional ("up", glm::vec3 (0.0f));
	const float zoom = transform.optional ("zoom", 1.0f);
	for (int channel = 0; channel < 3; channel++) {
	    result.center[channel].keyframes.push_back ({ .time = time, .value = center[channel] });
	    result.eye[channel].keyframes.push_back ({ .time = time, .value = eye[channel] });
	    result.up[channel].keyframes.push_back ({ .time = time, .value = up[channel] });
	}
	result.zoom.keyframes.push_back ({ .time = time, .value = zoom });
    }

    return result;
}

uint32_t parsePlaybackFlags (const JSON& options) {
    uint32_t flags = 0;
    if (const auto mode = options.optional<std::string> ("mode", ""); mode == "mirror") {
	flags |= CameraPathMirror;
    } else if (mode == "single") {
	flags |= CameraPathSingle;
    }
    if (options.optional ("random", false)) {
	flags |= CameraPathRandom;
    }
    if (options.optional ("startpaused", false)) {
	flags |= CameraPathStartPaused;
    }
    if (options.optional ("wraploop", false)) {
	flags |= CameraPathWrapLoop;
    }
    return flags;
}

CameraPath parseCurvePath (const JSON& data) {
    const auto options = data.optional ("options");
    const float fps = options.has_value () ? options->optional ("fps", 30.0f) : 30.0f;
    const int length = options.has_value () ? options->optional ("length", 0) : 0;
    CameraPath result {
	.name = data.optional<std::string> ("name", ""),
    };

    // The reference runtime rejects a path outright when its rate or its
    // authored length cannot produce a positive duration.
    if (fps <= 0.0f || length <= 0) {
	return result;
    }
    result.duration = static_cast<float> (length) / fps;
    result.secondsPerFrame = 1.0f / fps;
    result.lengthFrames = length;
    if (options.has_value ()) {
	result.flags = parsePlaybackFlags (*options);
    }

    if (const auto center = data.optional ("center"); center.has_value ()) {
	parseVectorChannels (*center, fps, result.center);
    }
    if (const auto eye = data.optional ("eye"); eye.has_value ()) {
	parseVectorChannels (*eye, fps, result.eye);
    }
    if (const auto up = data.optional ("up"); up.has_value ()) {
	parseVectorChannels (*up, fps, result.up);
    }
    if (const auto fov = data.optional ("fov"); fov.has_value ()) {
	result.fov = parseChannel (*fov, fps);
    }
    if (const auto zoom = data.optional ("zoom"); zoom.has_value ()) {
	result.zoom = parseChannel (*zoom, fps);
    }

    if ((result.flags & CameraPathWrapLoop) != 0) {
	for (int channel = 0; channel < 3; channel++) {
	    closeWrapLoop (result.center[channel], length);
	    closeWrapLoop (result.eye[channel], length);
	    closeWrapLoop (result.up[channel], length);
	}
	closeWrapLoop (result.fov, length);
	closeWrapLoop (result.zoom, length);
    }

    return result;
}
} // namespace

std::vector<CameraPath> CameraPathParser::parse (const WallpaperEngine::Data::JSON::JSON& data) {
    std::vector<CameraPath> result;
    const auto paths = data.optional ("paths");
    if (!paths.has_value () || !paths->is_array ()) {
	return result;
    }

    for (const auto& path : *paths) {
	if (!path.is_object ()) {
	    continue;
	}

	if (path.find ("transforms") != path.end ()) {
	    const auto transforms = path.optional ("transforms");
	    if (!transforms.has_value () || !transforms->is_array () || transforms->empty ()
		|| path.optional ("disabled", false)) {
		continue;
	    }
	    // Legacy paths use "disabled"; their editor-only "visible" state is not
	    // consulted by the runtime loader.
	    result.push_back (parseLegacyPath (path));
	    continue;
	}

	if (!path.optional ("visible", true)) {
	    continue;
	}
	CameraPath parsed = parseCurvePath (path);
	if (parsed.duration > 0.0f) {
	    result.push_back (std::move (parsed));
	}
    }
    return result;
}
