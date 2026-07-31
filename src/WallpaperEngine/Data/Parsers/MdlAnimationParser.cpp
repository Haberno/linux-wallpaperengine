#include "MdlAnimationParser.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>

#include <glm/gtc/matrix_inverse.hpp>

#include "WallpaperEngine/Assets/AssetLocator.h"
#include "WallpaperEngine/Data/Model/Project.h"

using namespace WallpaperEngine::Data::Parsers;

namespace {
constexpr size_t SECTION_HEADER_SIZE = 9;

template <typename T> T readValue (const std::vector<char>& data, size_t& offset, const size_t end) {
    if (offset > end || sizeof (T) > end - offset || offset + sizeof (T) > data.size ()) {
	throw std::runtime_error ("animation data ends unexpectedly");
    }

    T value;
    std::memcpy (&value, data.data () + offset, sizeof (value));
    offset += sizeof (value);
    return value;
}

std::string readString (const std::vector<char>& data, size_t& offset, const size_t end) {
    if (offset >= end || end > data.size ()) {
	throw std::runtime_error ("animation data ends unexpectedly");
    }
    const auto begin = data.begin () + static_cast<ptrdiff_t> (offset);
    const auto limit = data.begin () + static_cast<ptrdiff_t> (end);
    const auto terminator = std::find (begin, limit, '\0');
    if (terminator == limit) {
	throw std::runtime_error ("animation string is not terminated");
    }

    std::string value (begin, terminator);
    offset = std::distance (data.begin (), terminator) + 1;
    return value;
}

size_t findSection (const std::vector<char>& data, const char* marker, const size_t from) {
    const size_t markerLength = std::strlen (marker);
    for (size_t offset = from; offset + SECTION_HEADER_SIZE <= data.size (); offset++) {
	if (std::memcmp (data.data () + offset, marker, markerLength) == 0
	    && std::all_of (
		data.begin () + static_cast<ptrdiff_t> (offset + markerLength),
		data.begin () + static_cast<ptrdiff_t> (offset + SECTION_HEADER_SIZE - 1),
		[] (const char value) { return value >= '0' && value <= '9'; }
	    )
	    && data[offset + SECTION_HEADER_SIZE - 1] == '\0') {
	    return offset;
	}
    }
    return data.size ();
}

bool animationRecordFits (
    const std::vector<char>& data, const size_t recordOffset, const size_t sectionEnd, const size_t expectedBoneCount
) {
    try {
	size_t offset = recordOffset;
	readValue<uint32_t> (data, offset, sectionEnd);
	readValue<uint32_t> (data, offset, sectionEnd);
	readString (data, offset, sectionEnd);
	const std::string mode = readString (data, offset, sectionEnd);
	// Some MDLA0006 clips serialize an empty mode; Wallpaper Engine treats it as loop.
	if (!mode.empty () && mode != "loop" && mode != "mirror" && mode != "single") {
	    return false;
	}
	const float fps = readValue<float> (data, offset, sectionEnd);
	if (!std::isnormal (fps) || fps <= 0.0f || fps > 1000.0f) {
	    return false;
	}
	const uint32_t frameCount = readValue<uint32_t> (data, offset, sectionEnd);
	readValue<uint32_t> (data, offset, sectionEnd);
	const uint32_t boneCount = readValue<uint32_t> (data, offset, sectionEnd);
	if (frameCount == 0 || (boneCount != 0 && boneCount != expectedBoneCount)) {
	    return false;
	}

	for (uint32_t bone = 0; bone < boneCount; bone++) {
	    readValue<uint32_t> (data, offset, sectionEnd);
	    const uint32_t frameBytes = readValue<uint32_t> (data, offset, sectionEnd);
	    if (frameBytes % (sizeof (float) * 9) != 0 || offset > sectionEnd || frameBytes > sectionEnd - offset) {
		return false;
	    }
	    offset += frameBytes;
	}
	return true;
    } catch (const std::exception&) {
	return false;
    }
}

bool isValidUtf8 (const std::string& value) {
    size_t offset = 0;
    while (offset < value.size ()) {
	const auto lead = static_cast<unsigned char> (value[offset]);
	if (lead < 0x80) {
	    if (lead < 0x20 && lead != '\t') {
		return false;
	    }
	    offset++;
	    continue;
	}

	size_t continuationCount = 0;
	uint32_t codepoint = 0;
	if ((lead & 0xe0) == 0xc0) {
	    continuationCount = 1;
	    codepoint = lead & 0x1f;
	} else if ((lead & 0xf0) == 0xe0) {
	    continuationCount = 2;
	    codepoint = lead & 0x0f;
	} else if ((lead & 0xf8) == 0xf0) {
	    continuationCount = 3;
	    codepoint = lead & 0x07;
	} else {
	    return false;
	}
	if (offset + continuationCount >= value.size ()) {
	    return false;
	}
	for (size_t continuation = 0; continuation < continuationCount; continuation++) {
	    const auto byte = static_cast<unsigned char> (value[offset + continuation + 1]);
	    if ((byte & 0xc0) != 0x80) {
		return false;
	    }
	    codepoint = (codepoint << 6) | (byte & 0x3f);
	}

	const uint32_t minimum
	    = continuationCount == 1 ? 0x80 : continuationCount == 2 ? 0x800 : 0x10000;
	if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
	    return false;
	}
	offset += continuationCount + 1;
    }
    return true;
}

std::optional<size_t> findNextAnimationRecord (
    const std::vector<char>& data, const size_t searchFrom, const size_t sectionEnd, const size_t expectedBoneCount
) {
    // Versioned event/track metadata has no length field and may be followed by
    // clips with an empty mode string. Find the first complete record instead of
    // relying on a textual mode marker.
    for (size_t candidate = searchFrom; candidate + sizeof (uint32_t) * 8 < sectionEnd; candidate++) {
	if (animationRecordFits (data, candidate, sectionEnd, expectedBoneCount)) {
	    size_t offset = candidate + sizeof (uint32_t) * 2;
	    const std::string name = readString (data, offset, sectionEnd);
	    if (isValidUtf8 (name)) {
		return candidate;
	    }
	}
    }
    return std::nullopt;
}

void parseSkeleton (
    const std::vector<char>& data, const size_t sectionOffset, const std::string& filename, MdlAnimationData& result
) {
    const std::string version (data.data () + sectionOffset, std::strlen ("MDLS0004"));
    const bool nameBeforeTransform = version == "MDLS0002" || version == "MDLS0004";
    const bool nameAfterTransform = version == "MDLS0001" || version == "MDLS0003";
    if (!nameBeforeTransform && !nameAfterTransform) {
	throw std::runtime_error ("unsupported skeleton header " + version + " in " + filename);
    }

    size_t offset = sectionOffset + SECTION_HEADER_SIZE;
    const size_t sectionEnd = readValue<uint32_t> (data, offset, data.size ());
    if (sectionEnd < offset || sectionEnd > data.size ()) {
	throw std::runtime_error ("invalid skeleton section boundary in " + filename);
    }
    const uint32_t boneCount = readValue<uint32_t> (data, offset, sectionEnd);
    std::vector<glm::mat4> bindWorld;
    result.bones.reserve (boneCount);
    bindWorld.reserve (boneCount);

    for (uint32_t bone = 0; bone < boneCount; bone++) {
	std::string boneName;
	if (nameBeforeTransform) {
	    boneName = readString (data, offset, sectionEnd);
	} else {
	    readValue<uint8_t> (data, offset, sectionEnd);
	}
	const uint32_t boneType = readValue<uint32_t> (data, offset, sectionEnd);
	const auto parent = static_cast<int32_t> (readValue<uint32_t> (data, offset, sectionEnd));
	const uint32_t matrixBytes = readValue<uint32_t> (data, offset, sectionEnd);
	if (matrixBytes != sizeof (float) * 16) {
	    throw std::runtime_error ("unexpected bone transform size in " + filename);
	}
	if (parent >= static_cast<int32_t> (bone)) {
	    throw std::runtime_error ("bone parented to a later bone in " + filename);
	}

	glm::mat4 local;
	for (int column = 0; column < 4; column++) {
	    for (int row = 0; row < 4; row++) {
		local[column][row] = readValue<float> (data, offset, sectionEnd);
	    }
	}
	const glm::mat4 world = parent >= 0 ? bindWorld[parent] * local : local;
	bindWorld.push_back (world);
	result.bones.push_back (
	    {
		.name = std::move (boneName),
		.type = boneType,
		.parent = parent,
		.bindLocal = local,
		.inverseBindWorld = glm::inverse (world),
	    }
	);

	if (nameAfterTransform) {
	    result.bones.back ().name = readString (data, offset, sectionEnd);
	} else {
	    readString (data, offset, sectionEnd);
	}
    }
}

void parseAttachments (
    const std::vector<char>& data, const size_t sectionOffset, const std::string& filename, MdlAnimationData& result
) {
    const std::string version (data.data () + sectionOffset, std::strlen ("MDAT0001"));
    if (version != "MDAT0001") {
	throw std::runtime_error ("unsupported attachment header " + version + " in " + filename);
    }

    size_t offset = sectionOffset + SECTION_HEADER_SIZE;
    const size_t sectionEnd = readValue<uint32_t> (data, offset, data.size ());
    const uint16_t attachmentCount = readValue<uint16_t> (data, offset, sectionEnd);
    if (sectionEnd < offset || sectionEnd > data.size ()) {
	throw std::runtime_error ("invalid attachment section boundary in " + filename);
    }

    for (uint16_t index = 0; index < attachmentCount; index++) {
	MdlAttachment attachment;
	attachment.bone = readValue<uint16_t> (data, offset, sectionEnd);
	const std::string name = readString (data, offset, sectionEnd);
	if (attachment.bone >= result.bones.size ()) {
	    throw std::runtime_error ("attachment references a missing bone in " + filename);
	}
	for (int column = 0; column < 4; column++) {
	    for (int row = 0; row < 4; row++) {
		attachment.local[column][row] = readValue<float> (data, offset, sectionEnd);
	    }
	}
	result.attachments.insert_or_assign (name, attachment);
    }
    if (offset != sectionEnd) {
	throw std::runtime_error ("attachment records do not reach the next section in " + filename);
    }
}

/**
 * Reads the per-frame scalar tracks that follow a clip's bone tracks. They feed g_BlendMap
 * in the puppettexturechannels shader, which is how authored blinks fade a channelmap
 * overlay in and out; the bone tracks of such a clip are completely static.
 *
 * The record does not end with these tracks and not every clip carries them, so anything
 * that does not match the expected shape leaves the offset untouched for the record scan.
 */
bool readBlendTracks (
    const std::vector<char>& data, size_t& offset, const size_t sectionEnd, MdlAnimationClip& animation
) {
    constexpr uint32_t MAX_TRACKS = 64;
    const size_t start = offset;
    // one sample per frame plus the wrap-around sample that closes the loop
    const size_t expectedBytes = (static_cast<size_t> (animation.frameCount) + 1) * sizeof (float);

    try {
	const uint32_t trackCount = readValue<uint32_t> (data, offset, sectionEnd);
	if (trackCount > MAX_TRACKS) {
	    offset = start;
	    return false;
	}
	if (trackCount == 0) {
	    return true;
	}

	std::vector<std::vector<float>> tracks;
	tracks.reserve (trackCount);
	for (uint32_t track = 0; track < trackCount; track++) {
	    readValue<uint32_t> (data, offset, sectionEnd); // track type, only 0 observed
	    if (readValue<uint32_t> (data, offset, sectionEnd) != expectedBytes) {
		offset = start;
		return false;
	    }

	    auto& values = tracks.emplace_back (expectedBytes / sizeof (float));
	    for (auto& value : values) {
		value = readValue<float> (data, offset, sectionEnd);
	    }
	}

	animation.blendTracks = std::move (tracks);
	return true;
    } catch (const std::runtime_error&) {
	offset = start;
	return false;
    }
}

/**
 * MDLA0002 and newer append one optional per-bone float stream after the blend
 * tracks. Wallpaper Engine consumes it before the version-specific event and
 * constraint metadata. The values are not rendered yet, but stepping over the
 * stream is required to locate the next clip without scanning through its
 * sample payload.
 */
bool skipBoneScalarTracks (
    const std::vector<char>& data, size_t& offset, const size_t sectionEnd, const MdlAnimationClip& animation
) {
    const size_t start = offset;
    try {
	const bool present = readValue<uint8_t> (data, offset, sectionEnd) != 0;
	if (!present) {
	    return true;
	}

	const size_t expectedBytes = (static_cast<size_t> (animation.frameCount) + 1) * sizeof (float);
	for (size_t bone = 0; bone < animation.boneFrames.size (); bone++) {
	    readValue<uint32_t> (data, offset, sectionEnd);
	    const uint32_t byteCount = readValue<uint32_t> (data, offset, sectionEnd);
	    if (byteCount != expectedBytes || byteCount > sectionEnd - offset) {
		offset = start;
		return false;
	    }
	    for (size_t sample = 0; sample < byteCount / sizeof (float); sample++) {
		if (!std::isfinite (readValue<float> (data, offset, sectionEnd))) {
		    offset = start;
		    return false;
		}
	    }
	}
	return true;
    } catch (const std::runtime_error&) {
	offset = start;
	return false;
    }
}

void parseAnimations (
    const std::vector<char>& data, const size_t sectionOffset, const std::string& filename, MdlAnimationData& result
) {
    const std::string version (data.data () + sectionOffset, std::strlen ("MDLA0006"));
    if (version != "MDLA0001" && version != "MDLA0002" && version != "MDLA0003" && version != "MDLA0004"
	&& version != "MDLA0005" && version != "MDLA0006") {
	throw std::runtime_error ("unsupported animation header " + version + " in " + filename);
    }

    size_t offset = sectionOffset + SECTION_HEADER_SIZE;
    const size_t sectionEnd = readValue<uint32_t> (data, offset, data.size ());
    if (sectionEnd < offset || sectionEnd > data.size ()) {
	throw std::runtime_error ("invalid animation section boundary in " + filename);
    }
    const uint32_t animationCount = readValue<uint32_t> (data, offset, sectionEnd);

    for (uint32_t index = 0; index < animationCount; index++) {
	MdlAnimationClip animation;
	animation.id = readValue<uint32_t> (data, offset, sectionEnd);
	readValue<uint32_t> (data, offset, sectionEnd);
	animation.name = readString (data, offset, sectionEnd);
	animation.mode = readString (data, offset, sectionEnd);
	animation.fps = readValue<float> (data, offset, sectionEnd);
	animation.frameCount = readValue<uint32_t> (data, offset, sectionEnd);
	readValue<uint32_t> (data, offset, sectionEnd);
	const uint32_t boneCount = readValue<uint32_t> (data, offset, sectionEnd);
	if (boneCount != 0 && boneCount != result.bones.size ()) {
	    throw std::runtime_error ("animation bone count does not match the skeleton in " + filename);
	}

	animation.boneFrames.resize (boneCount);
	animation.boneFlags.resize (boneCount);
	for (uint32_t bone = 0; bone < boneCount; bone++) {
	    animation.boneFlags[bone] = readValue<uint32_t> (data, offset, sectionEnd);
	    const uint32_t frameBytes = readValue<uint32_t> (data, offset, sectionEnd);
	    constexpr uint32_t frameSize = sizeof (float) * 9;
	    if (frameBytes % frameSize != 0) {
		throw std::runtime_error ("unexpected animation frame size in " + filename);
	    }

	    auto& frames = animation.boneFrames[bone];
	    frames.resize (frameBytes / frameSize);
	    for (auto& frame : frames) {
		for (int component = 0; component < 3; component++) {
		    frame.translation[component] = readValue<float> (data, offset, sectionEnd);
		}
		glm::vec3 eulerRotation (0.0f);
		for (int component = 0; component < 3; component++) {
		    eulerRotation[component] = readValue<float> (data, offset, sectionEnd);
		}
		frame.rotation = glm::normalize (glm::quat (eulerRotation));
		for (int component = 0; component < 3; component++) {
		    frame.scale[component] = readValue<float> (data, offset, sectionEnd);
		}
	    }
	}

	readBlendTracks (data, offset, sectionEnd, animation);
	if (version != "MDLA0001") {
	    skipBoneScalarTracks (data, offset, sectionEnd, animation);
	}

	result.animations.push_back (std::move (animation));
	if (index + 1 < animationCount) {
	    const auto next = findNextAnimationRecord (data, offset, sectionEnd, result.bones.size ());
	    if (!next.has_value ()) {
		throw std::runtime_error ("could not locate the next animation record in " + filename);
	    }
	    offset = *next;
	} else {
	    offset = sectionEnd;
	}
    }
}
} // namespace

MdlAnimationData MdlAnimationParser::load (const Project& project, const std::string& filename) {
    const auto stream = project.assetLocator->read (filename);
    const std::vector<char> data { std::istreambuf_iterator<char> (*stream), std::istreambuf_iterator<char> () };
    return parse (data, filename);
}

MdlAnimationData MdlAnimationParser::parse (const std::vector<char>& data, const std::string& filename) {
    MdlAnimationData result;
    const size_t skeletonOffset = findSection (data, "MDLS", 0);
    if (skeletonOffset == data.size ()) {
	return result;
    }

    parseSkeleton (data, skeletonOffset, filename, result);
    const size_t attachmentOffset = findSection (data, "MDAT", skeletonOffset + SECTION_HEADER_SIZE);
    if (attachmentOffset < data.size ()) {
	parseAttachments (data, attachmentOffset, filename, result);
    }
    const size_t animationOffset = findSection (data, "MDLA", skeletonOffset + SECTION_HEADER_SIZE);
    if (animationOffset < data.size ()) {
	parseAnimations (data, animationOffset, filename, result);
    }
    return result;
}
