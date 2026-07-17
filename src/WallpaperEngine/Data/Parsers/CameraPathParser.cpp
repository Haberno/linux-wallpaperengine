#include "CameraPathParser.h"

#include <algorithm>

using namespace WallpaperEngine::Data::JSON;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;

namespace {
CameraPathHandle parseHandle (const JSON& keyframe, const std::string& name) {
    const auto handle = keyframe.optional (name);
    if (!handle.has_value () || !handle->is_object ()) {
	return {};
    }

    return CameraPathHandle {
	.enabled = handle->optional ("enabled", false),
	.offset = glm::vec2 (handle->optional ("x", 0.0f), handle->optional ("y", 0.0f)),
    };
}

CameraPathChannel parseChannel (const JSON& data, const float fps) {
    CameraPathChannel result;
    if (!data.is_array ()) {
	return result;
    }

    const float safeFps = fps > 0.0f ? fps : 1.0f;
    for (const auto& keyframe : data) {
	if (!keyframe.is_object ()) {
	    continue;
	}
	result.keyframes.push_back (
	    CameraPathKeyframe {
		.time = keyframe.optional ("frame", 0.0f) / safeFps,
		.value = keyframe.optional ("value", 0.0f),
		.incoming = parseHandle (keyframe, "back"),
		.outgoing = parseHandle (keyframe, "front"),
	    }
	);
    }
    std::ranges::sort (result.keyframes, {}, &CameraPathKeyframe::time);
    return result;
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

float lastTime (const CameraPathChannel& channel) {
    return channel.keyframes.empty () ? 0.0f : channel.keyframes.back ().time;
}

float findDuration (const CameraPath& path) {
    float duration = path.duration;
    for (int channel = 0; channel < 3; channel++) {
	duration = std::max (duration, lastTime (path.center[channel]));
	duration = std::max (duration, lastTime (path.eye[channel]));
	duration = std::max (duration, lastTime (path.up[channel]));
    }
    duration = std::max (duration, lastTime (path.fov));
    duration = std::max (duration, lastTime (path.zoom));
    return duration;
}

CameraPath parseLegacyPath (const JSON& data) {
    CameraPath result {
	.name = data.optional<std::string> ("name", ""),
	.duration = data.optional ("duration", 0.0f),
    };
    const auto transforms = data.optional ("transforms");
    if (!transforms.has_value () || !transforms->is_array ()) {
	return result;
    }

    for (const auto& transform : *transforms) {
	if (!transform.is_object ()) {
	    continue;
	}
	const float time = transform.optional ("timestamp", 0.0f);
	const glm::vec3 center = transform.optional ("center", glm::vec3 (0.0f));
	const glm::vec3 eye = transform.optional ("eye", glm::vec3 (0.0f));
	const glm::vec3 up = transform.optional ("up", glm::vec3 (0.0f, 1.0f, 0.0f));
	for (int channel = 0; channel < 3; channel++) {
	    result.center[channel].keyframes.push_back ({ .time = time, .value = center[channel] });
	    result.eye[channel].keyframes.push_back ({ .time = time, .value = eye[channel] });
	    result.up[channel].keyframes.push_back ({ .time = time, .value = up[channel] });
	}
	if (transform.find ("zoom") != transform.end ()) {
	    result.zoom.keyframes.push_back ({ .time = time, .value = transform.optional ("zoom", 1.0f) });
	}
    }

    for (int channel = 0; channel < 3; channel++) {
	std::ranges::sort (result.center[channel].keyframes, {}, &CameraPathKeyframe::time);
	std::ranges::sort (result.eye[channel].keyframes, {}, &CameraPathKeyframe::time);
	std::ranges::sort (result.up[channel].keyframes, {}, &CameraPathKeyframe::time);
    }
    std::ranges::sort (result.zoom.keyframes, {}, &CameraPathKeyframe::time);

    result.duration = findDuration (result);
    return result;
}

CameraPath parseCurvePath (const JSON& data) {
    const auto options = data.optional ("options");
    const float fps = options.has_value () ? options->optional ("fps", 30.0f) : 30.0f;
    const float length = options.has_value () ? options->optional ("length", 0.0f) : 0.0f;
    CameraPath result {
	.name = data.optional<std::string> ("name", ""),
	.duration = fps > 0.0f ? length / fps : 0.0f,
    };

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

    result.duration = findDuration (result);
    return result;
}
}

std::vector<CameraPath> CameraPathParser::parse (const WallpaperEngine::Data::JSON::JSON& data) {
    std::vector<CameraPath> result;
    const auto paths = data.optional ("paths");
    if (!paths.has_value () || !paths->is_array ()) {
	return result;
    }

    for (const auto& path : *paths) {
	if (!path.is_object () || !path.optional ("visible", true)) {
	    continue;
	}
	CameraPath parsed = path.find ("transforms") != path.end () ? parseLegacyPath (path) : parseCurvePath (path);
	if (parsed.duration > 0.0f) {
	    result.push_back (std::move (parsed));
	}
    }
    return result;
}
