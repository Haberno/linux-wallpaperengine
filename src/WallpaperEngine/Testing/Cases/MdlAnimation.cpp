#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "WallpaperEngine/Data/Model/MdlAnimation.h"
#include "WallpaperEngine/Data/Parsers/MdlAnimationParser.h"
#include "WallpaperEngine/Data/Parsers/MdlParser.h"
#include "WallpaperEngine/Render/Objects/PuppetClippingPlan.h"

using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Data::Parsers::MdlAnimationParser;

namespace {
template <typename T> void appendValue (std::vector<char>& data, const T& value) {
    const auto* bytes = reinterpret_cast<const char*> (&value);
    data.insert (data.end (), bytes, bytes + sizeof (T));
}

void appendString (std::vector<char>& data, const std::string& value) {
    data.insert (data.end (), value.begin (), value.end ());
    data.push_back ('\0');
}

void appendMarker (std::vector<char>& data, const char (&marker)[9]) {
    data.insert (data.end (), marker, marker + sizeof (marker));
}

void appendMatrix (std::vector<char>& data, const glm::mat4& matrix) {
    for (int column = 0; column < 4; column++) {
	for (int row = 0; row < 4; row++) {
	    appendValue (data, matrix[column][row]);
	}
    }
}

void patchU32 (std::vector<char>& data, const size_t offset, const uint32_t value) {
    std::memcpy (data.data () + offset, &value, sizeof (value));
}

void appendFrame (std::vector<char>& data, const glm::vec3& translation) {
    for (int component = 0; component < 3; component++) {
	appendValue (data, translation[component]);
    }
    for (int component = 0; component < 3; component++) {
	appendValue (data, 0.0f);
    }
    for (int component = 0; component < 3; component++) {
	appendValue (data, 1.0f);
    }
}

std::vector<char> makeAnimatedModelSections (const uint32_t submeshCount = 0) {
    std::vector<char> data;
    if (submeshCount != 0) {
	appendMarker (data, "MDLV0016");
	appendValue<uint32_t> (data, 15); // default vertex tag
	appendValue<uint32_t> (data, 1); // materials per submesh
	appendValue (data, submeshCount);
	for (uint32_t submesh = 0; submesh < submeshCount; ++submesh) {
	    appendString (data, "materials/test.json");
	    appendValue<uint32_t> (data, 0); // submesh flags
	    appendValue<uint32_t> (data, 15); // vertex tag
	    appendValue<uint32_t> (data, 3 * 48); // three unskinned vertices
	    data.resize (data.size () + 3 * 48, '\0');
	    appendValue<uint32_t> (data, 3 * sizeof (uint16_t));
	    for (uint16_t index = 0; index < 3; ++index) appendValue (data, index);
	}
    }

    appendMarker (data, "MDLS0002");
    const size_t skeletonEndOffset = data.size ();
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 2);
    for (uint32_t bone = 0; bone < 2; bone++) {
	appendString (data, bone == 0 ? "root" : "child");
	appendValue<uint32_t> (data, 0);
	appendValue<uint32_t> (data, bone == 0 ? UINT32_MAX : 0);
	appendValue<uint32_t> (data, sizeof (float) * 16);
	appendMatrix (data, glm::mat4 (1.0f));
	appendString (data, "");
    }
    patchU32 (data, skeletonEndOffset, static_cast<uint32_t> (data.size ()));

    appendMarker (data, "MDAT0001");
    const size_t attachmentEndOffset = data.size ();
    appendValue<uint32_t> (data, 0);
    appendValue<uint16_t> (data, 1);
    appendValue<uint16_t> (data, 1);
    appendString (data, "tip");
    appendMatrix (data, glm::translate (glm::mat4 (1.0f), glm::vec3 (0.0f, 0.0f, 3.0f)));
    patchU32 (data, attachmentEndOffset, static_cast<uint32_t> (data.size ()));

    appendMarker (data, "MDLA0006");
    const size_t animationEndOffset = data.size ();
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 2);

    appendValue<uint32_t> (data, 7);
    appendValue<uint32_t> (data, 0);
    appendString (data, "move");
    appendString (data, "loop");
    appendValue (data, 1.0f);
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 2);
    for (uint32_t bone = 0; bone < 2; bone++) {
	appendValue<uint32_t> (data, 0);
	appendValue<uint32_t> (data, sizeof (float) * 9 * 2);
	appendFrame (data, glm::vec3 (0.0f));
	appendFrame (data, bone == 0 ? glm::vec3 (2.0f, 0.0f, 0.0f) : glm::vec3 (0.0f, 2.0f, 0.0f));
    }

    // MDLA0002+ places an optional per-bone scalar stream after the legacy
    // blend tracks. Reze's MDLA0006 file uses this shape before a large block
    // of version-specific metadata.
    appendValue<uint32_t> (data, 0); // no legacy blend tracks
    appendValue<uint8_t> (data, 1); // per-bone scalar stream present
    for (uint32_t bone = 0; bone < 2; bone++) {
	appendValue<uint32_t> (data, 0);
	appendValue<uint32_t> (data, sizeof (float) * 2);
	appendValue<float> (data, 1.0f);
	appendValue<float> (data, bone == 0 ? 1.0f : 0.5f);
    }

    // Newer metadata has no enclosing byte length. Include the exact kind of
    // false header that the old scanner accepted inside Reze's scalar payload:
    // invalid UTF-8 and a positive subnormal frame rate.
    appendValue<uint32_t> (data, 16256);
    appendValue<uint32_t> (data, 0);
    appendString (data, std::string ("\xff?", 2));
    appendString (data, "");
    appendValue<float> (data, std::numeric_limits<float>::denorm_min ());
    appendValue<uint32_t> (data, 16256);
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 0);

    appendValue<uint32_t> (data, 9);
    appendValue<uint32_t> (data, 0);
    appendString (data, "events-only");
    appendString (data, "");
    appendValue (data, 24.0f);
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 0);
    patchU32 (data, animationEndOffset, static_cast<uint32_t> (data.size ()));

    return data;
}
} // namespace

TEST_CASE ("MDL animation parser shares legacy skeleton attachments and event-only clips") {
    const auto animationData = MdlAnimationParser::parse (makeAnimatedModelSections (), "synthetic.mdl");

    REQUIRE (animationData.bones.size () == 2);
    CHECK (animationData.bones[0].name == "root");
    CHECK (animationData.bones[1].parent == 0);
    REQUIRE (animationData.attachments.contains ("tip"));
    CHECK (animationData.attachments.at ("tip").bone == 1);
    REQUIRE (animationData.animations.size () == 2);
    CHECK (animationData.animations[0].id == 7);
    CHECK (animationData.animations[1].id == 9);
    CHECK (animationData.animations[1].mode.empty ());
    CHECK (animationData.animations[1].boneFrames.empty ());
}

TEST_CASE ("MDL bone controls retain their extra animation pose tracks", "[mdl][animation][solver-track-mask]") {
    std::vector<char> data;
    appendMarker (data, "MDLS0002");
    const auto skeletonEnd = data.size ();
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 2);
    for (int bone = 0; bone < 2; ++bone) {
	appendString (data, "bone");
	appendValue<uint32_t> (data, 3);
	appendValue<int32_t> (data, bone - 1);
	appendValue<uint32_t> (data, 64);
	appendMatrix (data, glm::mat4 (1.0f));
	appendString (data, "");
    }
    appendValue<uint16_t> (data, 2);
    size_t controlledBoneOffset = 0;
    for (uint32_t type = 0; type < 2; ++type) {
	appendString (data, type == 0 ? "wrist" : "target");
	controlledBoneOffset = data.size ();
	appendValue<uint32_t> (data, 1);
	appendValue<uint32_t> (data, type);
	appendMatrix (data, glm::translate (glm::mat4 (1.0f), glm::vec3 (5.0f, 6.0f, 0.0f)));
    }
    appendValue<uint8_t> (data, 0); // Alternate reference transforms.
    appendValue<uint32_t> (data, 0); // Scalar dependencies.
    appendValue<uint16_t> (data, 2);
    appendValue<float> (data, 0);
    appendValue<float> (data, 5);
    appendValue<uint16_t> (data, 1);
    appendValue<uint32_t> (data, 1);
    appendValue<float> (data, 0);
    appendValue<float> (data, 1);
    appendValue<float> (data, 0);
    appendValue<uint16_t> (data, 0);
    appendValue<uint16_t> (data, 1); // One IK group, root bone zero.
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 1); // One end control.
    appendValue<uint32_t> (data, 0);
    appendValue<uint16_t> (data, 1); // One node, root zero, one branch.
    appendValue<uint32_t> (data, 0);
    appendValue<uint16_t> (data, 1);
    appendValue<uint32_t> (data, 1); // End bone.
    appendValue<uint32_t> (data, 1); // Controlled chain flag.
    appendValue<float> (data, 5);
    appendValue<float> (data, 0);
    appendValue<uint16_t> (data, 2);
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 1);
    patchU32 (data, skeletonEnd, data.size ());
    appendMarker (data, "MDLA0002");
    const auto animationEnd = data.size ();
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 2);
    for (uint32_t clip = 0; clip < 2; ++clip) {
	appendValue<uint32_t> (data, clip + 1);
	appendValue<uint32_t> (data, 0);
	appendString (data, "move");
	appendString (data, "loop");
	appendValue<float> (data, 30.0f);
	appendValue<uint32_t> (data, 1);
	appendValue<uint32_t> (data, 0);
	appendValue<uint32_t> (data, 2);
	for (uint32_t track = 0; track < 4; ++track) {
	    appendValue<uint32_t> (data, 0);
	    appendValue<uint32_t> (data, 72);
	    appendFrame (data, glm::vec3 (static_cast<float> (track), 0, 0));
	    appendFrame (data, glm::vec3 (static_cast<float> (track), static_cast<float> (clip + 1), 0));
	}
	appendValue<uint32_t> (data, 0);
	appendValue<uint8_t> (data, 0);
	appendValue<uint32_t> (data, 0);
    }
    patchU32 (data, animationEnd, data.size ());
    const auto parsed = MdlAnimationParser::parse (data, "controls.mdl");
    REQUIRE (parsed.controls.size () == 2);
    CHECK (parsed.controls[0].name == "wrist");
    CHECK (parsed.controls[0].bone == 1);
    CHECK (parsed.controls[0].type == 0);
    CHECK (parsed.controls[1].type == 1);
    CHECK (parsed.controls[0].bindWorld[3].x == Catch::Approx (5));
    REQUIRE (parsed.ikGroups.size () == 1);
    const auto& group = parsed.ikGroups.front ();
    REQUIRE (group.nodes.size () == 1);
    REQUIRE (group.nodes.front ().branches.size () == 1);
    CHECK (group.nodes.front ().branches.front ().bones == std::vector<uint32_t> { 0, 1 });
    CHECK (parsed.boneLengths == std::vector<float> { 0, 5 });
    CHECK (parsed.boneDirections[0].at (1).y == Catch::Approx (1));
    REQUIRE (parsed.animations.size () == 2);
    for (size_t clip = 0; clip < 2; ++clip) {
	const auto& animation = parsed.animations[clip];
	CHECK (animation.boneFlags == std::vector<uint32_t> { 0, 3 });
	CHECK (animation.controlFlags == std::vector<uint32_t> { 0, 0 });
	REQUIRE (animation.controlFrames.size () == 2);
	CHECK (animation.controlFrames[0][1].translation.x == Catch::Approx (2));
	CHECK (animation.controlFrames[0][1].translation.y == Catch::Approx (clip + 1));
	CHECK (animation.controlFrames[1][1].translation.x == Catch::Approx (3));
	CHECK (animation.boneFrames[1][1].translation.x == Catch::Approx (1));
    }
    patchU32 (data, controlledBoneOffset, 8);
    CHECK_THROWS (MdlAnimationParser::parse (data, "invalid-control.mdl"));
}

TEST_CASE ("MDLS0003 imports retain named bones and skip legacy metadata", "[mdl][animation]") {
    std::vector<char> data;
    appendMarker (data, "MDLS0003");
    const auto endOffset = data.size ();
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 1);
    appendString (data, "RootNode");
    appendValue<uint32_t> (data, 1);
    appendValue<int32_t> (data, -1);
    appendValue<uint32_t> (data, 64);
    appendMatrix (data, glm::mat4 (1));
    appendString (data, "");
    // This optional legacy tail starts with a byte flag, not a u16 control count.
    appendValue<uint8_t> (data, 1);
    appendMatrix (data, glm::mat4 (1));
    patchU32 (data, endOffset, data.size ());
    const auto parsed = MdlAnimationParser::parse (data, "legacy-3d.mdl");
    REQUIRE (parsed.bones.size () == 1);
    CHECK (parsed.bones.front ().name == "RootNode");
    CHECK (parsed.controls.empty ());
    CHECK (parsed.ikGroups.empty ());
}

TEST_CASE ("MDLS reference transforms survive parsing with or without controls", "[mdl][animation]") {
    for (const auto* version : { "MDLS0002", "MDLS0004" }) {
	for (const uint16_t controlCount : { 0, 1 }) {
	    for (const bool hasReference : { false, true }) {
		CAPTURE (version, controlCount, hasReference);
		std::vector<char> data;
		appendString (data, version);
		const auto endOffset = data.size ();
		appendValue<uint32_t> (data, 0);
		appendValue<uint32_t> (data, 2);
		for (int bone = 0; bone < 2; ++bone) {
		    appendString (data, "bone");
		    appendValue<uint32_t> (data, 1);
		    appendValue<int32_t> (data, bone - 1);
		    appendValue<uint32_t> (data, 64);
		    appendMatrix (data, glm::translate (glm::mat4 (1), glm::vec3 (100 * (bone + 1), 0, 0)));
		    appendString (data, "");
		}
		appendValue<uint16_t> (data, controlCount);
		if (controlCount) {
		    appendString (data, "tip");
		    appendValue<uint32_t> (data, 1);
		    appendValue<uint32_t> (data, 0);
		    appendMatrix (data, glm::mat4 (1));
		}
		appendValue<uint8_t> (data, hasReference);
		if (hasReference) {
		    for (int bone = 0; bone < 2; ++bone)
			appendMatrix (data, glm::translate (glm::mat4 (1), glm::vec3 (10 * (bone + 1), 0, 0)));
		    if (controlCount)
			appendMatrix (data, glm::translate (glm::mat4 (1), glm::vec3 (99, 0, 0)));
		}
		appendValue<uint32_t> (data, 0); // Dependencies.
		appendValue<uint16_t> (data, 0); // Bone lengths.
		appendValue<uint16_t> (data, 0); // IK groups.
		patchU32 (data, endOffset, data.size ());
		const auto parsed = MdlAnimationParser::parse (data, "reference.mdl");
		REQUIRE (parsed.bones.size () == 2);
		CHECK (parsed.bones[0].referenceLocal.has_value () == hasReference);
		CHECK (parsed.bones[1].bindLocal[3].x == Catch::Approx (200));
		CHECK (parsed.bones[1].inverseBindWorld[3].x == Catch::Approx (-300));
		const auto pose = MdlAnimationEvaluator::evaluate (parsed, {});
		CHECK (pose.worldBones[1][3].x == Catch::Approx (hasReference ? 30 : 300));
		if (controlCount) {
		    CHECK (parsed.controls[0].referenceWorld.has_value () == hasReference);
		    if (hasReference) CHECK ((*parsed.controls[0].referenceWorld)[3].x == Catch::Approx (99));
		}
	    }
	}
    }
}

TEST_CASE ("MDLA events follow versioned tracks and cropped clip metadata", "[mdl][animation][morph-metadata]") {
    const auto submeshCount = GENERATE (1u, 2u, 3u);
    CAPTURE (submeshCount);
    auto data = makeAnimatedModelSections (submeshCount);
    const std::string marker = "MDLA0006";
    const auto begin = std::search (data.begin (), data.end (), marker.begin (), marker.end ());
    data.resize (std::distance (data.begin (), begin));
    appendMarker (data, "MDLA0006");
    const auto endField = data.size ();
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 35);
    appendValue<uint32_t> (data, 0);
    appendString (data, "Idle");
    appendString (data, "");
    appendValue<float> (data, 24);
    appendValue<uint32_t> (data, 60);
    appendValue<uint32_t> (data, 0x401);
    appendValue<uint32_t> (data, 2);
    for (int bone = 0; bone < 2; ++bone) {
	appendValue<uint32_t> (data, 0);
	appendValue<uint32_t> (data, 36);
	appendFrame (data, glm::vec3 (0));
    }
    appendValue<uint32_t> (data, 0); // blend tracks
    appendValue<uint8_t> (data, 0); // earlier scalar tracks
    appendValue<uint8_t> (data, 1); // v4 morph tracks, one record per submesh
    for (uint32_t submesh = 0; submesh < submeshCount; ++submesh) {
	appendValue<uint32_t> (data, submesh == 2 ? 0 : 1);
	if (submesh == 2) continue; // Submeshes without tracks still have a flags record.
	appendValue<float> (data, 1);
	appendValue<uint16_t> (data, 1);
	appendValue<uint16_t> (data, 0);
	appendValue<uint32_t> (data, 61 * sizeof (float));
	for (int frame = 0; frame < 61; ++frame) appendValue<float> (data, 1);
    }
    for (const float component : { -2.0f, -3.0f, -4.0f, 5.0f, 6.0f, 7.0f }) appendValue<float> (data, component); // v5 bounds
    appendValue<uint8_t> (data, 1); // v6 scalar tracks
    for (int bone = 0; bone < 2; ++bone) {
	appendValue<uint32_t> (data, 0);
	appendValue<uint32_t> (data, 61 * sizeof (float));
	for (int frame = 0; frame < 61; ++frame) appendValue<float> (data, bone + 1);
    }
    appendValue<uint16_t> (data, 0); // source clip/range descriptor, flags bit 0
    for (const uint32_t field : { 128u, 188u, 0u, UINT32_MAX }) appendValue<uint32_t> (data, field);
    appendValue<uint32_t> (data, 2);
    appendValue<float> (data, 59.0f / 24.0f);
    appendString (data, R"({"frame":59,"name":"end"})");
    appendValue<float> (data, 45.0f / 24.0f);
    appendString (data, R"({"frame":45,"name":"cry"})");
    patchU32 (data, endField, data.size ());

    const auto mesh = WallpaperEngine::Data::Parsers::MdlParser::parse (data, "events.mdl");
    REQUIRE (mesh.submeshes.size () == submeshCount);
    const auto parsed = MdlAnimationParser::parse (data, "events.mdl");
    REQUIRE (parsed.animations.size () == 1);
    const auto& clip = parsed.animations.front ();
    CHECK (clip.flags == 0x401);
    CHECK (clip.bounds.minimum == glm::vec3 (-2, -3, -4));
    CHECK (clip.bounds.maximum == glm::vec3 (5, 6, 7));
    REQUIRE (clip.morphTracks.size () == submeshCount);
    for (uint32_t submesh = 0; submesh < submeshCount; ++submesh) {
	if (submesh == 2) { CHECK (clip.morphTracks[submesh].empty ()); continue; }
	REQUIRE (clip.morphTracks[submesh].size () == 1);
	CHECK (clip.morphTracks[submesh][0].target == 0);
	REQUIRE (clip.morphTracks[submesh][0].frames.size () == 61);
	CHECK (clip.morphTracks[submesh][0].frames.back () == Catch::Approx (1));
    }
    REQUIRE (clip.depthTracks.size () == 2);
    REQUIRE (clip.depthTracks[0].size () == 61);
    REQUIRE (clip.depthTracks[1].size () == 61);
    CHECK (clip.depthTracks[0][60] == Catch::Approx (1));
    CHECK (clip.depthTracks[1][60] == Catch::Approx (2));
    REQUIRE (clip.events.size () == 2);
    CHECK (clip.events[0].frame == Catch::Approx (59));
    CHECK (clip.events[0].name == "end");
    CHECK (clip.events[1].frame == Catch::Approx (45));
    CHECK (clip.events[1].name == "cry");

    data.pop_back (); // A truncated event must not corrupt the usable bone data.
    patchU32 (data, endField, data.size ());
    const auto truncated = MdlAnimationParser::parse (data, "truncated-event.mdl");
    REQUIRE (truncated.animations.size () == 1);
    CHECK (truncated.animations.front ().bounds.minimum == clip.bounds.minimum);
    CHECK (truncated.animations.front ().bounds.maximum == clip.bounds.maximum);
    CHECK (truncated.animations.front ().events.empty ());
    CHECK (truncated.animations.front ().boneFrames.size () == 2);
}

TEST_CASE ("MDLA unbaked clip ranges sample their source animation", "[mdl][animation][morph-weights]") {
    for (const bool baked : { false, true }) {
	CAPTURE (baked);
	auto data = makeAnimatedModelSections (1);
	const std::string marker = "MDLA0006";
	data.resize (std::distance (data.begin (), std::search (data.begin (), data.end (), marker.begin (), marker.end ())));
	appendMarker (data, "MDLA0006");
	const auto endField = data.size ();
	appendValue<uint32_t> (data, 0);
	appendValue<uint32_t> (data, 2);
	for (uint32_t clip = 0; clip < 2; ++clip) {
	    const uint32_t frames = clip == 0 ? 4 : 2;
	    appendValue<uint32_t> (data, clip == 0 ? 241 : 31);
	    appendValue<uint32_t> (data, 0);
	    appendString (data, clip == 0 ? "source" : "Idle");
	    appendString (data, "loop");
	    appendValue<float> (data, 24);
	    appendValue<uint32_t> (data, frames);
	    appendValue<uint32_t> (data, clip == 0 ? 0 : baked ? 0x401 : 1);
	    const bool hasTracks = clip == 0 || baked;
	    appendValue<uint32_t> (data, hasTracks ? 2 : 0);
	    if (hasTracks) {
		for (uint32_t bone = 0; bone < 2; ++bone) {
		    appendValue<uint32_t> (data, bone); // The source masks its second bone.
		    appendValue<uint32_t> (data, (frames + 1) * 36);
		    for (uint32_t frame = 0; frame <= frames; ++frame)
			appendFrame (data, glm::vec3 (clip == 0 ? frame * 10 : 99, 0, 0));
		}
	    }
	    appendValue<uint32_t> (data, 0); // Blend tracks.
	    appendValue<uint8_t> (data, 0); // Bone scalar tracks.
	    appendValue<uint8_t> (data, 1); // Per-submesh morph tracks.
	    appendValue<uint32_t> (data, hasTracks ? 1 : 0);
	    if (hasTracks) {
		appendValue<float> (data, 0);
		appendValue<uint16_t> (data, 1);
		appendValue<uint16_t> (data, 2); // Sparse target index.
		appendValue<uint32_t> (data, (frames + 1) * sizeof (float));
		for (uint32_t frame = 0; frame <= frames; ++frame)
		    appendValue<float> (data, clip == 0 ? frame * 0.1f : 0.9f);
	    }
	    for (int i = 0; i < 6; ++i) appendValue<float> (data, 0);
	    appendValue<uint8_t> (data, 0); // Version 6 scalar tracks.
	    if (clip != 0) {
		appendValue<uint16_t> (data, 0); // Source index, not clip ID 241.
		for (const uint32_t field : { 1u, 3u, 0u, UINT32_MAX }) appendValue<uint32_t> (data, field);
	    }
	    appendValue<uint32_t> (data, 0); // Events.
	}
	patchU32 (data, endField, data.size ());
	const auto parsed = MdlAnimationParser::parse (data, "clip-range.mdl");
	REQUIRE (parsed.animations.size () == 2);
	const auto& idle = parsed.animations[1];
	const auto middle = MdlAnimationEvaluator::evaluate (parsed, {{ .animation = &idle, .frame = 0.5f }});
	CHECK (middle.worldBones[0][3].x == Catch::Approx (baked ? 99 : 15));
	const auto end = MdlAnimationEvaluator::evaluate (parsed, {{ .animation = &idle, .frame = 2.0f }});
	CHECK (end.worldBones[0][3].x == Catch::Approx (baked ? 99 : 30));
	CHECK (middle.worldBones[1][3].x == middle.worldBones[0][3].x);
	REQUIRE (idle.morphTracks.size () == 1);
	REQUIRE (idle.morphTracks[0].size () == 1);
	CHECK (idle.morphTracks[0][0].target == 2);
	REQUIRE (middle.puppetMorphWeights.size () == 3);
	CHECK (middle.puppetMorphWeights[2] == Catch::Approx (baked ? 0.9f : 0.15f));
	CHECK (end.puppetMorphWeights[2] == Catch::Approx (baked ? 0.9f : 0.3f));
    }
}

TEST_CASE ("MDL match-loop closure uses native quaternion interpolation", "[mdl][animation]") {
    MdlAnimationData data;
    data.bones.resize (1);
    MdlAnimationClip clip;
    clip.flags = 0x401; // The model compiler bakes matchloop clips into their own tracks.
    clip.mode = "loop";
    clip.fps = 1;
    clip.frameCount = 2;
    clip.boneFrames.resize (1);
    clip.boneFrames[0].resize (3);
    clip.boneFrames[0][1].rotation = glm::angleAxis (glm::radians (120.0f), glm::vec3 (0, 0, 1));

    for (const bool antipodal : { false, true }) {
	CAPTURE (antipodal);
	if (antipodal) clip.boneFrames[0][1].rotation = -clip.boneFrames[0][1].rotation;
	for (const float time : { 0.25f, 1.25f, 2.25f, -0.75f }) {
	    CAPTURE (time);
	    const auto pose = MdlAnimationEvaluator::evaluate (data, {{ .animation = &clip, .time = time }});
	    const bool closing = time == 1.25f || time < 0;
	    // Native 1401f89a0 normalizes the shortest-hemisphere weighted sum.
	    // A quarter-frame gives these exact unit-axis components; slerp differs.
	    CHECK (pose.worldBones[0][0].x == Catch::Approx ((closing ? -1.0f : 23.0f) / 26.0f).margin (1e-6f));
	    CHECK (pose.worldBones[0][0].y == Catch::Approx ((closing ? 15.0f : 7.0f) * std::sqrt (3.0f) / 26.0f).margin (1e-6f));
	}
    }
    const auto end = MdlAnimationEvaluator::evaluate (data, {{ .animation = &clip, .frame = 2.0f }});
    CHECK (end.worldBones[0][0].x == Catch::Approx (1));
    CHECK (end.worldBones[0][0].y == Catch::Approx (0));
}

TEST_CASE ("MDL animation evaluator interpolates and composes parent bones") {
    const auto animationData = MdlAnimationParser::parse (makeAnimatedModelSections (), "synthetic.mdl");
    const std::vector<MdlActiveAnimation> active {
	{ .animation = &animationData.animations[0], .time = 0.5f },
    };

    const auto pose = MdlAnimationEvaluator::evaluate (animationData, active);
    REQUIRE (pose.worldBones.size () == 2);
    CHECK (pose.worldBones[0][3].x == Catch::Approx (1.0f));
    CHECK (pose.worldBones[1][3].x == Catch::Approx (1.0f));
    CHECK (pose.worldBones[1][3].y == Catch::Approx (1.0f));
    CHECK (pose.skinBones[1][3].x == Catch::Approx (1.0f));
    CHECK (pose.skinBones[1][3].y == Catch::Approx (1.0f));

    const auto attachmentWorld = MdlAnimationEvaluator::attachmentTransform (animationData, pose.worldBones, "tip");
    REQUIRE (attachmentWorld.has_value ());
    CHECK ((*attachmentWorld)[3].x == Catch::Approx (1.0f));
    CHECK ((*attachmentWorld)[3].y == Catch::Approx (1.0f));
    CHECK ((*attachmentWorld)[3].z == Catch::Approx (3.0f));
    CHECK_FALSE (MdlAnimationEvaluator::attachmentTransform (animationData, pose.worldBones, "missing").has_value ());
}

TEST_CASE ("Authored IK end controls move rigid chains and their attachments", "[mdl][animation]") {
    const auto makeRig = [] (const bool pinned, const bool pole) {
	MdlAnimationData data;
	std::vector<glm::vec3> offsets = pole
	    ? std::vector<glm::vec3> {{0, 0, 0}, {0, 0, 0}, {5, 5, 0}, {5, -5, 0}}
	    : pinned ? std::vector<glm::vec3> {{2, 3, 0}, {0, 0, 0}, {0, 10, 0}}
	    : std::vector<glm::vec3> {{0, 0, 0}, {0, 10, 0}, {6, 0, 0}};
	std::vector<glm::mat4> world;
	data.boneDirections.resize (offsets.size ());
	for (size_t bone = 0; bone < offsets.size (); ++bone) {
	    const auto local = glm::translate (glm::mat4 (1), offsets[bone]);
	    world.push_back (bone ? world.back () * local : local);
	    data.bones.push_back ({.parent = static_cast<int32_t> (bone) - 1,
		.bindLocal = local, .inverseBindWorld = glm::inverse (world.back ())});
	    data.boneLengths.push_back (glm::length (offsets[bone]));
	    if (bone && data.boneLengths.back () > 0)
		data.boneDirections[bone - 1][bone] = glm::normalize (offsets[bone]);
	}
	const uint32_t tip = pole ? 3 : pinned ? 2 : 1;
	const uint32_t root = pinned || pole ? 1 : 0;
	data.controls.push_back ({.bone = tip, .type = 0, .bindWorld = world[tip]});
	MdlAnimationClip clip;
	clip.controlFrames = {{{.translation = pole ? glm::vec3 (12, 0, 0)
	    : pinned ? glm::vec3 (6, 11, 0) : glm::vec3 (4, 8, 0),
	    .rotation = glm::angleAxis (glm::radians (30.0f), glm::vec3 (0, 0, 1))}}};
	if (pole) {
	    data.controls.push_back ({.bone = tip, .type = 1});
	    clip.controlFrames.push_back ({{.translation = {5, -10, 0}}});
	}
	data.animations.push_back (clip);
	MdlIkChain chain {.flags = 1};
	for (uint32_t bone = root; bone <= tip; ++bone) {
	    chain.bones.push_back (bone);
	    if (bone != root) chain.maxLength += data.boneLengths[bone];
	}
	data.ikGroups.push_back ({.root = root, .nodes = {{.root = root, .branches = {chain}}}});
	return data;
    };
    const auto checkPosition = [] (const glm::mat4& matrix, const glm::vec3& expected) {
	CHECK (matrix[3].x == Catch::Approx (expected.x).margin (0.0001f));
	CHECK (matrix[3].y == Catch::Approx (expected.y).margin (0.0001f));
	CHECK (matrix[3].z == Catch::Approx (expected.z).margin (0.0001f));
    };
    // Numerical positions recovered by executing the original 140271910 solver
    // on these synthetic rigs. A free root can translate; an attached root cannot.
    auto data = makeRig (false, false);
    data.attachments["tip"] = {.bone = 2, .local = glm::translate (glm::mat4 (1), glm::vec3 (0, 2, 0))};
    const auto pose = MdlAnimationEvaluator::evaluate (data, {{.animation = &data.animations[0]}});
    checkPosition (pose.worldBones[0], {-0.47213602f, -0.94427204f, 0});
    checkPosition (pose.worldBones[1], {4, 8, 0});
    checkPosition (pose.worldBones[2], {9.1961524f, 11, 0});
    const auto attached = MdlAnimationEvaluator::attachmentTransform (data, pose.worldBones, "tip");
    REQUIRE (attached.has_value ());
    checkPosition (*attached, {8.1961524f, 12.7320508f, 0});
    CHECK (glm::length (glm::vec3 (pose.worldBones[1][3] - pose.worldBones[0][3])) == Catch::Approx (10));
    const auto additive = MdlAnimationEvaluator::evaluate (data, {{.animation = &data.animations[0], .additive = true}});
    checkPosition (additive.worldBones[1], {4, 8, 0});
    const auto half = MdlAnimationEvaluator::evaluate (data, {{.animation = &data.animations[0], .weight = 0.5f}});
    checkPosition (half.worldBones[1], {2, 9, 0});
    data.animations[0].controlFlags = { 1 };
    const auto masked = MdlAnimationEvaluator::evaluate (data, {{.animation = &data.animations[0]}});
    checkPosition (masked.worldBones[1], {0, 10, 0});

    data = makeRig (true, false);
    const auto pinned = MdlAnimationEvaluator::evaluate (data, {{.animation = &data.animations[0]}});
    checkPosition (pinned.worldBones[1], {2, 3, 0});
    checkPosition (pinned.worldBones[2], {6.47213602f, 11.94427204f, 0});

    data = makeRig (true, true);
    const auto guided = MdlAnimationEvaluator::evaluate (data, {{.animation = &data.animations[0]}});
    checkPosition (guided.worldBones[1], {0, 0, 0});
    checkPosition (guided.worldBones[2], {6.08557749f, -3.60079908f, 0});
    checkPosition (guided.worldBones[3], {12.12534904f, 0.07631421f, 0});
    CHECK (glm::length (glm::vec3 (guided.worldBones[2][3] - guided.worldBones[1][3]))
	== Catch::Approx (std::sqrt (50.0f)));
}

TEST_CASE ("An explicit model playhead preserves the final frame of a looping source clip", "[mdl][animation]") {
    const auto data = MdlAnimationParser::parse (makeAnimatedModelSections (), "playhead.mdl");
    const auto wrapped = MdlAnimationEvaluator::evaluate (data, {{ .animation = &data.animations.front (), .time = 1.0f }});
    const auto held = MdlAnimationEvaluator::evaluate (data, {{ .animation = &data.animations.front (), .frame = 1.0f }});
    CHECK (wrapped.worldBones[0][3].x == Catch::Approx (0.0f));
    CHECK (held.worldBones[0][3].x == Catch::Approx (2.0f));
}

TEST_CASE ("additive MDL entrance clips resolve against the shared reference pose") {
    MdlAnimationData animationData;
    const glm::mat4 bind = glm::translate (glm::mat4 (1.0f), glm::vec3 (10.0f, 20.0f, 0.0f));
    animationData.bones.push_back (
	{
	    .name = "root",
	    .bindLocal = bind,
	    .inverseBindWorld = glm::inverse (bind),
	}
    );

    MdlAnimationClip base {
	.id = 1,
	.mode = "loop",
	.fps = 1.0f,
	.frameCount = 1,
	.boneFrames = { {
	    { .translation = { 10.0f, 20.0f, 0.0f } },
	    { .translation = { 10.0f, 20.0f, 0.0f } },
	} },
    };
    MdlAnimationClip entrance {
	.id = 2,
	.mode = "single",
	.fps = 1.0f,
	.frameCount = 1,
	.boneFrames = { {
	    { .translation = { 4.0f, 8.0f, 0.0f }, .scale = { 0.8f, 0.8f, 1.0f } },
	    { .translation = { 10.0f, 20.0f, 0.0f }, .scale = { 1.0f, 1.0f, 1.0f } },
	} },
    };

    animationData.animations = { base, entrance };

    const auto evaluateAt = [&] (const float time) {
	return MdlAnimationEvaluator::evaluate (
	    animationData,
	    {
		{ .animation = &animationData.animations[0], .time = time, .additive = true },
		{ .animation = &animationData.animations[1], .time = time, .additive = true },
	    }
	);
    };

    const auto opening = evaluateAt (0.0f);
    REQUIRE (opening.worldBones.size () == 1);
    CHECK (opening.worldBones[0][3].x == Catch::Approx (4.0f));
    CHECK (opening.worldBones[0][3].y == Catch::Approx (8.0f));
    CHECK (glm::length (glm::vec3 (opening.worldBones[0][0])) == Catch::Approx (0.8f));

    const auto resting = evaluateAt (1.0f);
    REQUIRE (resting.worldBones.size () == 1);
    CHECK (resting.worldBones[0][3].x == Catch::Approx (10.0f));
    CHECK (resting.worldBones[0][3].y == Catch::Approx (20.0f));
    CHECK (glm::length (glm::vec3 (resting.worldBones[0][0])) == Catch::Approx (1.0f));
}

TEST_CASE ("additive bone deltas are composed per component") {
    MdlAnimationData animationData;
    const glm::mat4 bind = glm::translate (glm::mat4 (1.0f), { 10.0f, 20.0f, 0.0f })
	* glm::scale (glm::mat4 (1.0f), { 2.0f, 2.0f, 1.0f });
    animationData.bones.push_back (
	{
	    .name = "root",
	    .bindLocal = bind,
	    .inverseBindWorld = glm::inverse (bind),
	    .referenceLocal = glm::translate (glm::mat4 (1), glm::vec3 (4, 8, 0))
		* glm::scale (glm::mat4 (1), glm::vec3 (0.8f, 0.8f, 1)),
	}
    );

    MdlAnimationClip base {
	.id = 1,
	.mode = "loop",
	.fps = 1.0f,
	.frameCount = 0,
	.boneFrames = { { { .translation = { 4.0f, 8.0f, 0.0f }, .scale = { 0.8f, 0.8f, 1.0f } } } },
    };
    MdlAnimationClip detail {
	.id = 2,
	.mode = "loop",
	.fps = 1.0f,
	.frameCount = 0,
	.boneFrames = { { { .translation = { 6.0f, 8.0f, 0.0f }, .scale = { 1.8f, 0.8f, 1.0f } } } },
    };

    animationData.animations = { base, detail };

    const auto pose = MdlAnimationEvaluator::evaluate (
	animationData,
	{
	    { .animation = &animationData.animations[0], .additive = true },
	    { .animation = &animationData.animations[1], .weight = 0.5f, .additive = true },
	}
    );

    REQUIRE (pose.worldBones.size () == 1);
    CHECK (pose.worldBones[0][3].x == Catch::Approx (5.0f));
    CHECK (pose.worldBones[0][3].y == Catch::Approx (8.0f));
    CHECK (glm::length (glm::vec3 (pose.worldBones[0][0])) == Catch::Approx (1.3f));
    CHECK (glm::length (glm::vec3 (pose.worldBones[0][1])) == Catch::Approx (0.8f));
}

TEST_CASE ("puppets use their explicit shared reference pose for partial additive layers") {
    MdlAnimationData animationData;
    const glm::mat4 bind = glm::translate (glm::mat4 (1.0f), { 1000.0f, -400.0f, 0.0f });
    animationData.bones.push_back (
	{
	    .name = "constrained",
	    .bindLocal = bind,
	    .inverseBindWorld = glm::inverse (bind),
	    .referenceLocal = glm::translate (glm::mat4 (1), glm::vec3 (10, 20, 0)),
	}
    );
    animationData.animations = {
	{
	    .id = 0,
	    .name = "event only",
	},
	{
	    .id = 1,
	    .mode = "single",
	    .fps = 1.0f,
	    .frameCount = 1,
	    .boneFrames = { {
		{ .translation = { 10.0f, 20.0f, 0.0f } },
		{ .translation = { 14.0f, 26.0f, 0.0f } },
	    } },
	},
	{
	    .id = 2,
	    .mode = "single",
	    .fps = 1.0f,
	    .frameCount = 1,
	    .boneFrames = { {
		{ .translation = { 10.0f, 20.0f, 0.0f } },
		{ .translation = { 8.0f, 24.0f, 0.0f } },
	    } },
	},
    };

    const auto pose = MdlAnimationEvaluator::evaluate (
	animationData,
	{
	    { .animation = &animationData.animations[1], .time = 1.0f, .weight = 0.5f, .additive = true },
	    { .animation = &animationData.animations[2], .time = 1.0f, .weight = 0.25f, .additive = true },
	}
    );

    REQUIRE (pose.worldBones.size () == 1);
    CHECK (pose.worldBones[0][3].x == Catch::Approx (11.5f));
    CHECK (pose.worldBones[0][3].y == Catch::Approx (24.0f));
}

TEST_CASE ("an idle additive clip preserves parts positioned by a base animation", "[mdl][animation]") {
    MdlAnimationData data;
    const auto bind = glm::translate (glm::mat4 (1), glm::vec3 (1000, 0, 0));
    data.bones.push_back ({ .name = "eye", .bindLocal = bind, .inverseBindWorld = glm::inverse (bind) });
    const MdlAnimationClip base {
	.id = 1, .mode = "single", .fps = 1, .frameCount = 1,
	.boneFrames = { { { .translation = { 10, 0, 0 } }, { .translation = { 20, 0, 0 } } } },
    };
    const MdlAnimationClip idle {
	.id = 2, .mode = "single", .fps = 1, .frameCount = 1,
	.boneFrames = { { { .translation = { 1000, 0, 0 } }, { .translation = { 1000, 0, 0 } } } },
    };
    for (const bool reordered : { false, true }) {
	data.animations = reordered ? std::vector { idle, base } : std::vector { base, idle };
	for (const float time : { 0.0f, 0.5f, 1.0f }) {
	    const auto pose = MdlAnimationEvaluator::evaluate (data, {
		{ .animation = &base, .time = time },
		{ .animation = &idle, .time = time, .additive = true },
	    });
	    CHECK (pose.worldBones[0][3].x == Catch::Approx (10 + 10 * time));
	}
	const auto rest = MdlAnimationEvaluator::evaluate (data, {});
	CHECK (rest.worldBones[0][3].x == Catch::Approx (1000));
    }
}

TEST_CASE ("masked blink bone tracks preserve motion from earlier layers", "[mdl][animation]") {
    MdlAnimationData data;
    data.bones.resize (2);
    const MdlAnimationClip motion {
	.mode = "single", .fps = 1, .frameCount = 1,
	.boneFrames = { { {} }, { { .translation = { 10, 0, 0 } } } },
    };
    MdlAnimationClip blink {
	.mode = "single", .fps = 1, .frameCount = 1,
	.boneFlags = { 0, 1 },
	.boneFrames = { { { .translation = { 0, 3, 0 } } }, { { .translation = { 40, 0, 0 } } } },
	.blendTracks = { { 0, 1 } },
    };
    for (const bool additive : { false, true }) {
	const auto pose = MdlAnimationEvaluator::evaluate (data, {
	    { .animation = &motion, .time = 0.5f },
	    { .animation = &blink, .time = 0.5f, .additive = additive },
	});
	CHECK (pose.worldBones[0][3].y == Catch::Approx (3));
	CHECK (pose.worldBones[1][3].x == Catch::Approx (10));
	REQUIRE (pose.blendWeights.size () == 1);
	CHECK (pose.blendWeights[0] == Catch::Approx (0.5f));
    }
    blink.boneFlags[1] = 2; // Only bit zero disables a track.
    const auto unmasked = MdlAnimationEvaluator::evaluate (data, {
	{ .animation = &motion }, { .animation = &blink },
    });
    CHECK (unmasked.worldBones[1][3].x == Catch::Approx (40));
}

TEST_CASE ("Full weight model clips preserve rotations and masked reference bones", "[mdl][animation]") {
    MdlAnimationData data;
    const auto reference = glm::translate (glm::mat4 (1.0f), glm::vec3 (7, 8, 9));
    data.bones = { { .name = "moving", .bindLocal = reference }, { .name = "masked", .bindLocal = reference } };
    MdlBoneFrame sample { .translation = { 2, 3, 4 },
	.rotation = glm::angleAxis (2.0f, glm::normalize (glm::vec3 (1, 2, 3))), .scale = { -2, 0.5f, 3 } };
    MdlAnimationClip clip { .fps = 1, .frameCount = 1, .boneFlags = { 0, 1 },
	.boneFrames = { { sample, sample }, { sample, sample } } };
    const auto expected = glm::translate (glm::mat4 (1.0f), sample.translation)
	* glm::mat4_cast (sample.rotation) * glm::scale (glm::mat4 (1.0f), sample.scale);
    for (const float weight : { 1.0f, 2.0f }) {
	const auto pose = MdlAnimationEvaluator::evaluate (data, { { .animation = &clip, .time = 0.4f, .weight = weight } });
	for (int column = 0; column < 4; ++column) {
	    for (int row = 0; row < 4; ++row) {
		CHECK (pose.worldBones[0][column][row] == Catch::Approx (expected[column][row]).margin (1e-5f));
		CHECK (pose.worldBones[1][column][row] == Catch::Approx (reference[column][row]).margin (1e-5f));
	    }
	}
    }
}

TEST_CASE ("MDLS spring bone constraints parse the native physics keys", "[mdl][animation][physics]") {
    const std::string spring = R"({"se":true,"re":false,"r":true,"t":false,"rs":35,"rf":15,"ri":45,)"
	R"("tp":"100 20","tm":200.0,"la":true,"lamin":"0 0 -0.5","lamax":"0 0 0.25","rax":false,"ray":false,"raz":true})";
    std::vector<char> data;
    appendMarker (data, "MDLS0002");
    const auto endOffset = data.size ();
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 2);
    for (const auto& metadata : { spring, std::string (R"({"se":false,"re":true,"r":true})") }) {
	appendString (data, "bone");
	appendValue<uint32_t> (data, 1);
	appendValue<int32_t> (data, -1);
	appendValue<uint32_t> (data, 64);
	appendMatrix (data, glm::mat4 (1));
	appendString (data, metadata);
    }
    patchU32 (data, endOffset, data.size ());

    const auto parsed = MdlAnimationParser::parse (data, "spring.mdl");
    REQUIRE (parsed.bones[0].spring.has_value ());
    const auto& bone = *parsed.bones[0].spring;
    CHECK ((bone.rotation && !bone.translation && bone.limitAngles));
    CHECK (bone.rotationStiffness == Catch::Approx (35));
    CHECK (bone.rotationFriction == Catch::Approx (15));
    CHECK (bone.rotationInertia == Catch::Approx (0.55f));
    CHECK (bone.tip == glm::vec3 (100, 20, 0));
    CHECK (bone.maxAngles.z == Catch::Approx (0.25f));
    CHECK (bone.rotationAxes == glm::bvec3 (false, false, true));
    CHECK (bone.translationAxes == glm::bvec3 (true));
    CHECK_FALSE (parsed.bones[1].spring.has_value ()); // rigid physics is not simulated
}

TEST_CASE ("Spring bones lag behind layer motion and settle back", "[mdl][animation][physics]") {
    MdlAnimationData data;
    data.bones = { { .name = "arm", .spring = MdlBoneSpring {
	.rotation = true, .rotationStiffness = 35, .rotationFriction = 15, .rotationInertia = 0.55f,
	.tip = { 100, 0, 0 }, .rotationAxes = { false, false, true } } } };
    MdlPhysicsState state;
    const auto step = [&] (const glm::vec3& position) {
	MdlPhysicsStep physics { .state = state, .model = glm::translate (glm::mat4 (1), position), .deltaTime = 1 / 60.0f };
	return MdlAnimationEvaluator::evaluate (data, {}, &physics);
    };

    CHECK (step ({ 0, 0, 0 }).worldBones[0] == glm::mat4 (1)); // nothing to measure against yet
    for (int frame = 0; frame < 5; ++frame) step ({ 0, 10.0f * (frame + 1), 0 });
    // Moving up leaves the tip below, so the bone swings clockwise, around Z only.
    CHECK (state.bones[0].angles.z < -0.01f);
    CHECK (state.bones[0].angles.x == 0.0f);
    CHECK (step ({ 0, 60, 0 }).worldBones[0][0].y < 0.0f);

    for (int frame = 0; frame < 600; ++frame) step ({ 0, 60, 0 });
    CHECK (state.bones[0].angles.z == Catch::Approx (0).margin (1e-3f));

    data.bones[0].spring->limitAngles = true;
    data.bones[0].spring->minAngles = { 0, 0, -0.02f };
    data.bones[0].spring->maxAngles = { 0, 0, 0.02f };
    for (int frame = 0; frame < 5; ++frame) step ({ 0, 60 - 40.0f * (frame + 1), 0 });
    CHECK (state.bones[0].angles.z == Catch::Approx (0.02f));
}

TEST_CASE ("Spring bones recover after a zero scale frame", "[mdl][animation][physics]") {
    MdlAnimationData data;
    data.bones = { { .name = "arm", .spring = MdlBoneSpring {
	.rotation = true, .translation = true, .rotationStiffness = 35, .rotationFriction = 15,
	.rotationInertia = 0.55f, .translationStiffness = 200, .translationFriction = 20,
	.translationInertia = 0.7f, .tip = { 100, 0, 0 }, .rotationAxes = { false, false, true } } } };
    MdlPhysicsState state;
    const auto step = [&] (const float position, const glm::vec3 scale = glm::vec3 (1.0f)) {
	MdlPhysicsStep physics {
	    .state = state,
	    .model = glm::scale (glm::translate (glm::mat4 (1), { 0, position, 0 }), scale),
	    .deltaTime = 1 / 60.0f,
	};
	return MdlAnimationEvaluator::evaluate (data, {}, &physics);
    };
    const auto checkFinitePose = [] (const MdlPose& pose) {
	for (int column = 0; column < 4; ++column) {
	    for (int row = 0; row < 4; ++row) {
		CHECK (std::isfinite (pose.worldBones[0][column][row]));
		CHECK (std::isfinite (pose.skinBones[0][column][row]));
	    }
	}
    };

    step (0);
    for (int frame = 0; frame < 5; ++frame) step (10.0f * (frame + 1));
    CHECK (state.bones[0].angles.z < -0.01f);
    checkFinitePose (step (50, { 0, 1, 1 }));
    checkFinitePose (step (50));
    for (int frame = 0; frame < 5; ++frame) step (60.0f + 10.0f * frame);
    checkFinitePose (step (110));
    CHECK (state.bones[0].angles.z < -0.01f);
    CHECK (state.bones[0].offset.y < 0.0f);
}

TEST_CASE ("Puppet depth uses floor samples, layer weights and bounded additive deltas") {
    MdlAnimationData data;
    data.bones.resize (2);
    data.defaultDepths = { 300, 700 };
    MdlAnimationClip clip;
    clip.frameCount = 2;
    clip.mode = "single";
    clip.boneFlags = { 0, 0 };
    clip.depthTracks = { { 0, 1000, 0 }, { 0, 0, 0 } };
    MdlActiveAnimation layer { .animation = &clip, .frame = 0.5f };
    CHECK (MdlAnimationEvaluator::evaluate (data, { layer }).depths == std::vector<float> { 0, 0 });
    layer.frame = 1;
    layer.weight = 0.5f;
    CHECK (MdlAnimationEvaluator::evaluate (data, { layer }).depths == std::vector<float> { 650, 350 });
    layer.additive = true;
    CHECK (MdlAnimationEvaluator::evaluate (data, { layer, layer, layer }).depths == std::vector<float> { 1000, 0 });
    clip.boneFlags = { 1, 1 };
    CHECK (MdlAnimationEvaluator::evaluate (data, { layer }).depths.empty ());
    clip.boneFlags = { 3, 1 };
    CHECK (MdlAnimationEvaluator::evaluate (data, { layer }).depths == std::vector<float> { 650, 700 });
    CHECK (MdlAnimationEvaluator::evaluate (data, {}).depths.empty ());
}

TEST_CASE ("Installed Reze depth tracks reorder the clipped eye ranges", "[.][puppet-depth-installed]") {
    const char* filename = std::getenv ("LWE_TEST_PUPPET_DEPTH_MODEL");
    REQUIRE (filename != nullptr);
    std::ifstream stream (filename, std::ios::binary);
    REQUIRE (stream.good ());
    const std::vector<char> bytes { std::istreambuf_iterator<char> (stream), std::istreambuf_iterator<char> () };
    const auto data = MdlAnimationParser::parse (bytes, filename);
    const auto mesh = WallpaperEngine::Data::Parsers::MdlParser::parse (bytes, filename);
    REQUIRE (data.bones.size () == 86);
    REQUIRE (data.defaultDepths.size () == 86);
    CHECK (*std::min_element (data.defaultDepths.begin (), data.defaultDepths.end ()) == -1700);
    CHECK (*std::max_element (data.defaultDepths.begin (), data.defaultDepths.end ()) == 8600);
    const auto clip = std::find_if (data.animations.begin (), data.animations.end (),
	[] (const auto& value) { return value.id == 258; });
    REQUIRE (clip != data.animations.end ());
    REQUIRE (clip->depthTracks.size () == 86);
    CHECK (clip->frameCount == 300);
    CHECK (mesh.submeshes.front ().flags == 0x100c);
    REQUIRE (mesh.clippingDescriptors.size () == 1);
    CHECK (mesh.clippingDescriptors.front ().targets == std::vector<uint32_t> { 74, 75 });
    std::vector<uint32_t> order;
    std::vector<uint32_t> firstOrder;
    bool changed = false;
    for (uint32_t frame = 0; frame <= clip->frameCount; ++frame) {
	const auto pose = MdlAnimationEvaluator::evaluate (data, {
	    { .animation = &*clip, .additive = true, .frame = static_cast<float> (frame) }
	});
	WallpaperEngine::Render::Objects::updatePuppetDrawOrder (mesh, data.defaultDepths, pose.depths, order);
	if (frame == 0) firstOrder = order;
	changed |= order != firstOrder;
	const auto plan = WallpaperEngine::Render::Objects::buildPuppetClippingPlan (mesh, 0, &order);
	CHECK_FALSE (plan.requiresComposition);
	uint32_t indices = 0;
	for (const auto& draw : plan.draws) indices += draw.indexCount;
	CHECK (indices == 20088);
    }
    CHECK (changed);
}

TEST_CASE ("Puppet morph layers preserve sparse targets and native activation", "[mdl][morph-weights]") {
    MdlAnimationData data;
    MdlAnimationClip first, second, zero;
    for (auto* clip : { &first, &second, &zero }) {
	clip->fps = 1; clip->frameCount = 1; clip->mode = "single";
    }
    first.morphTracks = { { { 0, { 0.7f, 0.7f } }, { 3, { 0.2f, 0.8f } } } };
    second.morphTracks = { { { 0, { 0.6f, -0.2f } } } };
    zero.morphTracks = { { { 0, { 0.0f, 0.0f } } } };
    MdlActiveAnimation base { &first, 0, 1, false };
    MdlActiveAnimation partial { &second, 0, 0.5f, false };
    MdlActiveAnimation additive { &second, 0, 0.5f, true };
    MdlActiveAnimation clear { &zero, 0, 1, false };
    auto pose = MdlAnimationEvaluator::evaluate (data, { base, partial });
    REQUIRE (pose.puppetMorphWeights.size () == 4);
    CHECK (pose.puppetMorphWeights[0] == Catch::Approx (0.65f));
    CHECK (pose.puppetMorphWeights[3] == Catch::Approx (0.2f));
    CHECK (pose.puppetMorphMask == 9);
    pose = MdlAnimationEvaluator::evaluate (data, { base, additive });
    CHECK (pose.puppetMorphWeights[0] == Catch::Approx (0.7f));
    additive.frame = 1;
    pose = MdlAnimationEvaluator::evaluate (data, { base, additive });
    CHECK (pose.puppetMorphWeights[0] == Catch::Approx (0.6f));
    pose = MdlAnimationEvaluator::evaluate (data, { base, clear });
    CHECK (pose.puppetMorphWeights[0] == Catch::Approx (0.7f));
    CHECK (pose.puppetMorphMask == 8);
    pose = MdlAnimationEvaluator::evaluate (data, { base, clear, partial });
    CHECK (pose.puppetMorphWeights[0] == Catch::Approx (0.65f));
    CHECK (pose.puppetMorphMask == 9);
    clear.weight = 0.5f;
    pose = MdlAnimationEvaluator::evaluate (data, { base, clear });
    CHECK (pose.puppetMorphWeights[0] == Catch::Approx (0.35f));
    CHECK (pose.puppetMorphMask == 9);
    base.frame = 0.5f;
    pose = MdlAnimationEvaluator::evaluate (data, { base });
    CHECK (pose.puppetMorphWeights[3] == Catch::Approx (0.5f));
    CHECK (MdlAnimationEvaluator::evaluate (data, {}).puppetMorphWeights.empty ());
    // A visible zero-weight layer can reactivate a retained value after a clear.
    clear.weight = 1; partial.weight = 0;
    pose = MdlAnimationEvaluator::evaluate (data, { base, clear, partial });
    CHECK (pose.puppetMorphWeights[0] == Catch::Approx (0.7f));
    CHECK (pose.puppetMorphMask == 9);
    partial.enabled = false;
    pose = MdlAnimationEvaluator::evaluate (data, { base, clear, partial });
    CHECK (pose.puppetMorphMask == 8);
}

TEST_CASE ("Spring torque limits retain native scalar parsing and zero defaults", "[mdl][animation][physics][torque]") {
    const std::vector<std::string> settings {
	R"({"se":true,"r":true,"lt":true,"ltmax":7.5})",
	R"({"se":true,"r":true,"lt":true})",
	R"({"se":true,"r":true,"lt":true,"ltmax":"7.5"})",
	R"({"se":true,"r":true,"lt":false,"ltmax":7.5})",
	R"({"se":true,"r":true,"lt":1,"ltmax":7.5})",
    };
    std::vector<char> bytes;
    appendMarker (bytes, "MDLS0002");
    const auto endOffset = bytes.size ();
    appendValue<uint32_t> (bytes, 0);
    appendValue<uint32_t> (bytes, settings.size ());
    for (const auto& setting : settings) {
	appendString (bytes, "bone");
	appendValue<uint32_t> (bytes, 1);
	appendValue<int32_t> (bytes, -1);
	appendValue<uint32_t> (bytes, 64);
	appendMatrix (bytes, glm::mat4 (1));
	appendString (bytes, setting);
    }
    patchU32 (bytes, endOffset, bytes.size ());
    const auto parsed = MdlAnimationParser::parse (bytes, "torque.mdl");
    REQUIRE (parsed.bones.size () == settings.size ());
    for (size_t index = 0; index < settings.size (); ++index) {
	REQUIRE (parsed.bones[index].spring.has_value ());
	CHECK (parsed.bones[index].spring->limitTorque == (index < 3));
	CHECK (parsed.bones[index].spring->maxTorque == Catch::Approx (index == 0 ? 7.5f : 0));
    }
}

TEST_CASE ("Spring torque caps quaternion components before timestep integration", "[mdl][animation][physics][torque]") {
    MdlAnimationData data;
    data.bones = { { .name = "spring", .spring = MdlBoneSpring {
	.rotation = true, .rotationInertia = 0, .limitTorque = true, .maxTorque = 10 } } };
    const auto evaluate = [&] (const glm::quat& velocity, const float dt) {
	MdlPhysicsState state;
	MdlPhysicsStep physics { .state = state, .deltaTime = dt };
	(void)MdlAnimationEvaluator::evaluate (data, {}, &physics);
	state.bones[0].angularVelocity = velocity;
	const auto pose = MdlAnimationEvaluator::evaluate (data, {}, &physics);
	return std::make_pair (state.bones[0].angularVelocity, pose.worldBones[0]);
    };
    // These fixed ratios encode +40, -30 and +4 degrees independently. A total
    // quaternion-angle cap or Euler clamp produces a different mixed-axis result.
    const auto input = glm::normalize (glm::quat (1, 0.3639702343f, -0.2679491924f, 0.0349207695f));
    const auto expected = glm::normalize (glm::quat (1, 0.08748866353f, -0.08748866353f, 0.0349207695f));
    for (const float dt : { 1 / 120.0f, 1 / 60.0f, 1 / 30.0f }) {
	const auto actual = evaluate (input, dt).first;
	for (int component = 0; component < 4; ++component)
	    CHECK (actual[component] == Catch::Approx (expected[component]).margin (1e-6f));
    }
    const auto zInput = glm::angleAxis (glm::radians (40.0f), glm::vec3 (0, 0, 1));
    for (const float dt : { 1 / 120.0f, 1 / 60.0f, 1 / 30.0f }) {
	const auto matrix = evaluate (zInput, dt).second;
	const float angle = glm::radians (dt < 1 / 60.0f ? 5.0f : 10.0f);
	CHECK (matrix[0][0] == Catch::Approx (std::cos (angle)).margin (1e-6f));
	CHECK (matrix[0][1] == Catch::Approx (std::sin (angle)).margin (1e-6f));
    }
    data.bones[0].spring->maxTorque = 0;
    CHECK (evaluate (input, 1 / 60.0f).first == glm::quat (1, 0, 0, 0));
    data.bones[0].spring->limitTorque = false;
    const auto untouched = evaluate (input, 1 / 60.0f).first;
    for (int component = 0; component < 4; ++component)
	CHECK (untouched[component] == Catch::Approx (input[component]).margin (1e-6f));
}

TEST_CASE ("MDLA morph samples cannot become a following clip header", "[mdl][morph-weights][morph-boundary]") {
    auto bytes = makeAnimatedModelSections (1);
    const std::string marker = "MDLA0006";
    const auto header = std::search (bytes.begin (), bytes.end (), marker.begin (), marker.end ());
    REQUIRE (header != bytes.end ());
    const size_t sectionEndOffset = std::distance (bytes.begin (), header) + marker.size () + 1;
    bytes.resize (sectionEndOffset);
    appendValue<uint32_t> (bytes, 0);
    appendValue<uint32_t> (bytes, 3);
    const std::array samples { 0.7f, 0.0f, 0.6f };
    for (uint32_t clip = 0; clip < samples.size (); ++clip) {
        appendValue<uint32_t> (bytes, clip + 1);
        appendValue<uint32_t> (bytes, 0);
        appendString (bytes, "clip" + std::to_string (clip + 1));
        appendString (bytes, "loop");
        appendValue<float> (bytes, 1);
        appendValue<uint32_t> (bytes, 1);
        appendValue<uint32_t> (bytes, 0);
        appendValue<uint32_t> (bytes, 2);
        for (int bone = 0; bone < 2; ++bone) {
            appendValue<uint32_t> (bytes, 0);
            appendValue<uint32_t> (bytes, 72);
            appendFrame (bytes, glm::vec3 (0));
            appendFrame (bytes, glm::vec3 (0));
        }
        appendValue<uint32_t> (bytes, 0); // No blend tracks.
        appendValue<uint8_t> (bytes, 0); // No early bone scalar tracks.
        appendValue<uint8_t> (bytes, 1); // One submesh's morph tracks follow.
        appendValue<uint32_t> (bytes, 1);
        appendValue<float> (bytes, 0);
        appendValue<uint16_t> (bytes, 1);
        appendValue<uint16_t> (bytes, 0);
        appendValue<uint32_t> (bytes, 8);
        appendValue<float> (bytes, samples[clip]);
        appendValue<float> (bytes, samples[clip]);
        for (int axis = 0; axis < 6; ++axis) appendValue<float> (bytes, 0);
        appendValue<uint8_t> (bytes, 0); // No depth tracks.
        appendValue<uint32_t> (bytes, 0); // No events.
    }
    patchU32 (bytes, sectionEndOffset, bytes.size ());
    const auto parsed = MdlAnimationParser::parse (bytes, "layered-morph.mdl");
    REQUIRE (parsed.animations.size () == 3);
    for (size_t index = 0; index < samples.size (); ++index) {
        const auto& clip = parsed.animations[index];
        CHECK (clip.id == index + 1);
        CHECK (clip.fps == 1);
        CHECK (clip.frameCount == 1);
        REQUIRE (clip.morphTracks.size () == 1);
        REQUIRE (clip.morphTracks[0].size () == 1);
        CHECK (clip.morphTracks[0][0].target == 0);
        CHECK (clip.morphTracks[0][0].frames == std::vector<float> { samples[index], samples[index] });
    }
    const MdlActiveAnimation base { .animation = &parsed.animations[0], .frame = 0 };
    const MdlActiveAnimation clear { .animation = &parsed.animations[1], .frame = 0 };
    const MdlActiveAnimation partial { .animation = &parsed.animations[2], .weight = 0.5f, .frame = 0 };
    const auto pose = MdlAnimationEvaluator::evaluate (parsed, { base, clear, partial });
    CHECK (pose.puppetMorphMask == 1);
    REQUIRE (pose.puppetMorphWeights.size () == 1);
    CHECK (pose.puppetMorphWeights[0] == Catch::Approx (0.65f));
}

namespace {
MdlAnimationData ropeRig (const bool freeEnd = false, const bool gravity = true) {
    MdlAnimationData data;
    data.boneLengths = { 0, 30, 30, 30 };
    data.boneDirections.resize (4);
    for (uint32_t index = 0; index < 4; ++index) {
	data.bones.push_back ({
	    .parent = static_cast<int32_t> (index) - 1,
	    .bindLocal = glm::translate (glm::mat4 (1), glm::vec3 (index ? 30 : -45, 0, 0)),
	    .inverseBindWorld = glm::translate (glm::mat4 (1), glm::vec3 (45 - 30 * static_cast<int> (index), 0, 0)),
	});
	if (index) data.boneDirections[index - 1][index] = { 1, 0, 0 };
    }
    data.bones.back ().ikFollowEnd = freeEnd;
    data.bones.back ().rope = MdlBoneRope { .gravity = gravity, .mass = 1 };
    data.ikGroups = {{ .nodes = {{ .branches = {{ .flags = 4, .maxLength = 90, .bones = { 0, 1, 2, 3 } }} }} }};
    return data;
}
}

TEST_CASE ("MDLS retains IK gravity and friction without spring mode", "[mdl][animation][physics][rope]") {
    std::vector<char> bytes;
    appendMarker (bytes, "MDLS0002");
    const auto endOffset = bytes.size ();
    appendValue<uint32_t> (bytes, 0);
    appendValue<uint32_t> (bytes, 4);
    for (const auto* metadata : {
	R"({"ik":true,"ge":true,"m":2.5,"tf":-50,"ikse":true,"ikfe":true})",
	R"({"ik":true,"ge":1,"m":"2","tf":200})", R"({"ik":true})", R"({"ik":false,"ge":true,"m":1})"
    }) {
	appendString (bytes, "tip");
	appendValue<uint32_t> (bytes, 1);
	appendValue<int32_t> (bytes, -1);
	appendValue<uint32_t> (bytes, 64);
	appendMatrix (bytes, glm::mat4 (1));
	appendString (bytes, metadata);
    }
    patchU32 (bytes, endOffset, bytes.size ());
    const auto parsed = MdlAnimationParser::parse (bytes, "rope.mdl");
    REQUIRE (parsed.bones.size () == 4);
    for (const auto& bone : parsed.bones) CHECK_FALSE (bone.spring);
    REQUIRE (parsed.bones[0].rope);
    CHECK (parsed.bones[0].rope->gravity);
    CHECK (parsed.bones[0].rope->mass == 2.5f);
    CHECK (parsed.bones[0].rope->friction == 0.25f);
    CHECK (parsed.bones[0].rope->stretch);
    CHECK (parsed.bones[0].ikFollowEnd);
    REQUIRE (parsed.bones[1].rope);
    CHECK_FALSE (parsed.bones[1].rope->gravity);
    CHECK (parsed.bones[1].rope->mass == 0.0f);
    CHECK (parsed.bones[1].rope->friction == 1.0f);
    REQUIRE (parsed.bones[2].rope);
    CHECK (parsed.bones[2].rope->mass == 0.0f);
    CHECK (parsed.bones[2].rope->friction == 0.0f);
    CHECK_FALSE (parsed.bones[3].rope);
}

TEST_CASE ("Ropes replay native predicted poses and retain corrected state", "[mdl][animation][physics][rope]") {
    // wallpaper64.exe 40e2ce021e9352324fadb3b8f72b8ba2a7ee95b71cc571d5b9f84be75cd993b0,
    // native08 synthetic four-bone rig, 2026-10-06: hooks 14026eb60/140271910/14026e340.
    // These literals are captured values, independent of the evaluator under test.
    struct Frame {
	float dt;
	std::array<glm::vec2, 4> positions;
	std::array<glm::vec2, 3> basisX;
    };
    const std::array<Frame, 4> pinned = {{
	{ 0.0351076983f, {{ { -45.0f, 0.0f }, { -15.0f, -0.110931396f }, { 15.0f, -0.110931396f }, { 45.0f, 0.0f } }},
	  {{ { 0.999993145f, -0.00369768799f }, { 1.0f, 0.0f }, { 0.999993145f, 0.00369768799f } }} },
	{ 0.0342923999f, {{ { -45.0f, 0.0f }, { -15.0f, -0.327701569f }, { 15.0f, -0.327701569f }, { 45.0f, 0.0f } }},
	  {{ { 0.999940336f, -0.0109227346f }, { 1.0f, 0.0f }, { 0.999940336f, 0.0109227346f } }} },
	{ 0.0332053006f, {{ { -45.0f, 0.0f }, { -15.0009155f, -0.643642426f }, { 14.9990845f, -0.643665314f }, { 45.0f, 0.0f } }},
	  {{ { 0.999769926f, -0.0214504674f }, { 1.0f, 0.0f }, { 0.999769926f, 0.0214499217f } }} },
	{ 0.0327802002f, {{ { -45.0f, 0.0f }, { -15.0034485f, -1.05589676f }, { 14.9965515f, -1.05602264f }, { 45.0f, 0.0f } }},
	  {{ { 0.999381006f, -0.0351788178f }, { 1.0f, 0.0f }, { 0.999381185f, 0.0351749286f } }} },
    }};
    const std::array<glm::vec2, 4> pinnedCurrent = {{ { 115.0f, 45.0f }, { 144.994507f, 43.9450226f }, { 174.994507f, 43.9447517f }, { 205.0f, 45.0f } }};
    const std::array<glm::vec2, 4> pinnedPrevious = {{ { 0.0f, 0.0f }, { 144.998047f, 44.3565712f }, { 174.998047f, 44.3565025f }, { 0.0f, 0.0f } }};
    const std::array<Frame, 4> free = {{
	{ 0.0351076983f, {{ { -45.0f, 0.0f }, { -15.0f, -0.110931396f }, { 15.0f, -0.110931396f }, { 45.0f, -0.110931396f } }},
	  {{ { 0.999993145f, -0.00369768799f }, { 1.0f, 0.0f }, { 1.0f, 0.0f } }} },
	{ 0.0342923999f, {{ { -45.0f, 0.0f }, { -15.0f, -0.327697754f }, { 15.0f, -0.327697754f }, { 45.0f, -0.327697754f } }},
	  {{ { 0.999940336f, -0.010922607f }, { 1.0f, 0.0f }, { 1.0f, 0.0f } }} },
	{ 0.0332053006f, {{ { -45.0f, 0.0f }, { -15.0018005f, -0.643684387f }, { 15.0f, -0.643699646f }, { 45.0f, -0.643699646f } }},
	  {{ { 0.999769866f, -0.0214524958f }, { 1.0f, 0.0f }, { 1.0f, 0.0f } }} },
	{ 0.0327802002f, {{ { -45.0f, 0.0f }, { -15.0039215f, -1.05626678f }, { 14.9975891f, -1.05641174f }, { 44.9975891f, -1.05641174f } }},
	  {{ { 0.999380589f, -0.035191685f }, { 1.0f, 0.0f }, { 1.0f, 0.0f } }} },
    }};
    const std::array<glm::vec2, 4> freeCurrent = {{ { 115.007706f, 74.9997253f }, { 144.992981f, 73.9440079f }, { 174.995285f, 73.9435883f }, { 204.995285f, 73.9435883f } }};
    const std::array<glm::vec2, 4> freePrevious = {{ { 0.0f, 0.0f }, { 144.997589f, 74.3563766f }, { 174.998795f, 74.3563004f }, { 204.998795f, 74.3563004f } }};
    for (const bool freeEnd : { false, true }) {
	CAPTURE (freeEnd);
	const auto data = ropeRig (freeEnd);
	const auto& frames = freeEnd ? free : pinned;
	MdlPhysicsState state;
	for (const auto& frame : frames) {
	    MdlPhysicsStep physics {
		.state = state,
		.model = glm::translate (glm::mat4 (1), glm::vec3 (160, freeEnd ? 75 : 45, 0)),
		.rope = MdlRopeStep { .gravity = { 0, -90, 0 }, .deltaTime = frame.dt },
	    };
	    const auto pose = MdlAnimationEvaluator::evaluate (data, {}, &physics);
	    REQUIRE (state.ropePoints.size () == 4);
	    for (size_t bone = 0; bone < 4; ++bone) {
		for (int axis = 0; axis < 2; ++axis) {
		    CHECK (pose.worldBones[bone][3][axis] == Catch::Approx (frame.positions[bone][axis]).margin (2e-4f));
		    if (bone < 3)
			CHECK (pose.worldBones[bone][0][axis] == Catch::Approx (frame.basisX[bone][axis]).margin (2e-4f));
		}
		CHECK (pose.worldBones[bone][3].z == 0.0f);
		CHECK (pose.skinBones[bone] == pose.worldBones[bone] * data.bones[bone].inverseBindWorld);
	    }
	    CHECK (glm::mat3 (pose.worldBones.back ()) == glm::mat3 (1));
	}
	for (size_t bone = 0; bone < 4; ++bone) for (int axis = 0; axis < 2; ++axis) {
	    CHECK (state.ropePoints[bone].current[axis] == Catch::Approx ((freeEnd ? freeCurrent : pinnedCurrent)[bone][axis]).margin (2e-4f));
	    CHECK (state.ropePoints[bone].previous[axis] == Catch::Approx ((freeEnd ? freePrevious : pinnedPrevious)[bone][axis]).margin (2e-4f));
	}
    }
    const auto data = ropeRig (false, false);
    MdlPhysicsState state;
    for (const auto& frame : pinned) {
	MdlPhysicsStep physics {
	    .state = state, .model = glm::translate (glm::mat4 (1), glm::vec3 (160, 105, 0)),
	    .rope = MdlRopeStep { .gravity = { 0, -90, 0 }, .deltaTime = frame.dt },
	};
	const auto pose = MdlAnimationEvaluator::evaluate (data, {}, &physics);
	for (const auto& skin : pose.skinBones) CHECK (skin == glm::mat4 (1));
    }
}

TEST_CASE ("Rope inertia at zero delta is separate from spring integration", "[mdl][animation][physics][rope]") {
    auto data = ropeRig (true, false);
    data.bones.back ().rope->friction = 1.0f;
    for (const float dt : { 0.0f, 0.1f }) {
	MdlPhysicsState state;
	state.bones.resize (4);
	state.ropePoints.resize (4);
	state.ropePoints[0].initialized = true;
	for (int bone = 0; bone < 4; ++bone) {
	    state.ropePoints[bone].current = { -45 + 30 * bone, 0, 0 };
	    state.ropePoints[bone].previous = { -45 + 30 * bone, -1, 0 };
	}
	MdlPhysicsStep physics { .state = state, .rope = MdlRopeStep { .deltaTime = dt } };
	const auto pose = MdlAnimationEvaluator::evaluate (data, {}, &physics);
	// Native source applies no friction at dt=0, but still extrapolates current-previous.
	CHECK (pose.worldBones[1][3].y == Catch::Approx (dt == 0 ? 1.0f : 0.0f).margin (1e-6f));
	CHECK (state.ropePoints[1].previous.y == 0.0f);
    }
}

TEST_CASE ("Unsupported rope routes preserve authored pose and ordinary state", "[mdl][animation][physics][rope]") {
    for (int route = 0; route < 8; ++route) {
	CAPTURE (route);
	auto data = ropeRig ();
	MdlPhysicsState state;
	MdlPhysicsStep physics { .state = state, .rope = MdlRopeStep { .gravity = { 0, -90, 0 }, .deltaTime = 0.1f } };
	if (route == 0) physics.rope.reset ();
	if (route == 1) data.ikGroups[0].hasDependencies = true;
	if (route == 2) data.ikGroups[0].nodes[0].branches[0].flags = 5;
	if (route == 3) data.bones[1].spring = MdlBoneSpring {};
	if (route == 4) data.bones[1].ikAngleLimits = true;
	if (route == 5) physics.model = glm::scale (glm::mat4 (1), glm::vec3 (0));
	if (route == 6) data.ikGroups[0].nodes[0].root = 1;
	if (route == 7) {
	    // An extra solver-owned side branch remains unsupported.
	    data.bones.push_back ({
		.type = 2, .parent = 1, .bindLocal = glm::translate (glm::mat4 (1), glm::vec3 (0, 5, 0)),
		.inverseBindWorld = glm::translate (glm::mat4 (1), glm::vec3 (15, -5, 0)),
	    });
	    data.boneLengths.push_back (5);
	    data.boneDirections.resize (5);
	    data.boneDirections[1][4] = { 0, 1, 0 };
	}
	const auto pose = MdlAnimationEvaluator::evaluate (data, {}, &physics);
	CHECK (state.ropePoints.empty ());
	for (const auto& skin : pose.skinBones) CHECK (skin == glm::mat4 (1));
    }
}

TEST_CASE ("Rope wind replays native force phases independently of mass", "[mdl][animation][physics][rope][wind]") {
    // Native wind01: binary 40e2ce02, 32 direct wind calls, four sequential
    // updates per rig. These are the first predicted local poses of each rig.
    struct Case { float mass; bool gravity; glm::vec3 model; glm::vec3 first; glm::vec3 second; };
    const Case cases[] = {
        { 1.0f, false, { 160.0f, 45.0f, 0.0f }, { -15.0f, 0.0f, 0.0f }, { 15.0631256f, -0.0157814026f, 0.0078904517f } },
        { 4.0f, false, { 160.0f, 75.0f, 0.0f }, { -15.0f, 0.0f, 0.0f }, { 15.0631256f, -0.0157775879f, 0.0078904517f } },
        { 2.0f, true, { 160.0f, 105.0f, 0.0f }, { -15.0f, -0.212257385f, 0.0f }, { 15.0631256f, -0.228042603f, 0.0078904517f } },
        { 1.0f, false, { 220.0f, 135.0f, 0.0f }, { -14.8933716f, -0.0266571045f, 0.0133283241f }, { 15.0f, 0.0f, 0.0f } },
    };
    for (const auto& sample : cases) for (const bool centered : { false, true }) {
        auto data = ropeRig (false, sample.gravity);
        data.bones.back ().rope->mass = sample.mass;
        MdlPhysicsState state;
        const auto offset = centered ? glm::vec3 (160, 90, 0) : glm::vec3 (0);
        MdlPhysicsStep physics {
            .state = state,
            .model = glm::translate (glm::mat4 (1), sample.model - offset),
            .rope = MdlRopeStep {
                .gravity = { 0, -90, 0 }, .deltaTime = 0.0343399011f,
                .windDirection = { 2, -.5f, .25f }, .windStrength = 60,
                .windTime = 6.0665226f, .windWidth = 320, .windOriginX = offset.x,
            },
        };
        const auto pose = MdlAnimationEvaluator::evaluate (data, {}, &physics);
        for (int axis = 0; axis < 3; ++axis) {
            CHECK (pose.worldBones[1][3][axis] == Catch::Approx (sample.first[axis]).margin (2e-4f));
            CHECK (pose.worldBones[2][3][axis] == Catch::Approx (sample.second[axis]).margin (2e-4f));
        }
    }
}

TEST_CASE ("MDLA masks solver-owned bone tracks while preserving authored masks", "[mdl][animation][solver-track-mask]") {
    // Native 140261880 masks type-0x2 bones unless they own an IK group.
    // Wind01's animated phaseX tip stays at bind X45 despite two authored X60 local samples.
    const uint32_t expected[4][4] = { { 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 3, 1, 3, 3 }, { 3, 1, 3, 3 } };
    for (uint32_t type = 0; type < 4; ++type) for (uint32_t flags = 0; flags < 4; ++flags) {
        CAPTURE (type, flags);
        std::vector<char> bytes;
        appendMarker (bytes, "MDLS0002");
        const auto skeletonEnd = bytes.size ();
        appendValue<uint32_t> (bytes, 0);
        appendValue<uint32_t> (bytes, 1);
        appendString (bytes, "tip");
        appendValue (bytes, type);
        appendValue<int32_t> (bytes, -1);
        appendValue<uint32_t> (bytes, 64);
        appendMatrix (bytes, glm::translate (glm::mat4 (1), glm::vec3 (45, 0, 0)));
        appendString (bytes, "");
        patchU32 (bytes, skeletonEnd, bytes.size ());
        appendMarker (bytes, "MDLA0001");
        const auto animationEnd = bytes.size ();
        appendValue<uint32_t> (bytes, 0);
        appendValue<uint32_t> (bytes, 1);
        appendValue<uint32_t> (bytes, 1);
        appendValue<uint32_t> (bytes, 0);
        appendString (bytes, "move");
        appendString (bytes, "loop");
        appendValue<float> (bytes, 30);
        appendValue<uint32_t> (bytes, 1);
        appendValue<uint32_t> (bytes, 0);
        appendValue<uint32_t> (bytes, 1);
        appendValue (bytes, flags);
        appendValue<uint32_t> (bytes, 72);
        appendFrame (bytes, glm::vec3 (75, 0, 0));
        appendFrame (bytes, glm::vec3 (75, 0, 0));
        appendValue<uint32_t> (bytes, 0);
        appendValue<uint32_t> (bytes, 0);
        patchU32 (bytes, animationEnd, bytes.size ());
        const auto parsed = MdlAnimationParser::parse (bytes, "solver-track.mdl");
        REQUIRE (parsed.animations.size () == 1);
        CHECK (parsed.animations[0].boneFlags == std::vector<uint32_t> { expected[type][flags] });
        CHECK (parsed.animations[0].boneFrames[0][0].translation.x == 75);
        const auto pose = MdlAnimationEvaluator::evaluate (parsed, { { .animation = &parsed.animations[0] } });
        CHECK (pose.worldBones[0][3].x == ((expected[type][flags] & 1) ? 45 : 75));
    }
}

TEST_CASE ("Controlled ropes retain native target, stretch and endpoint basis", "[mdl][animation][physics][rope][controlled]") {
    // Native controlled01, wallpaper64.exe 40e2ce02, 2026-10-06: these first-step
    // positions, corrected state and basis values are direct 140271910 readbacks.
    struct Case {
	bool stretch;
	float angle;
	glm::vec3 model;
	std::array<glm::vec2, 4> positions;
	std::array<glm::vec2, 3> basisX;
	std::array<glm::vec3, 3> endBasis;
	std::array<glm::vec2, 4> corrected;
    };
    const Case cases[] = {
	{ false, 0.0f, { 160.0f, 45.0f, 0.0f },
	  {{ { -45.0f, 0.0f }, { -15.0f, -0.1069526672f }, { 15.0f, -0.1069526672f }, { 45.0f, 0.0f } }},
	  {{ { 0.9999936223f, -0.003565066727f }, { 1.0f, 0.0f }, { 0.9999936223f, 0.003565066727f } }},
	  {{ { 0.9983081222f, 0.0f, 0.0f }, { 0.0f, 0.9983081222f, 0.0f }, { 0.0f, 0.0f, 0.9983081222f } }},
	  {{ { 115.0f, 45.0f }, { 145.0f, 44.89304733f }, { 175.0f, 44.89304733f }, { 205.0f, 45.0f } }},
	},
	{ true, 0.0f, { 160.0f, 75.0f, 0.0f },
	  {{ { -45.0f, 0.0f }, { -15.0f, -0.1069564819f }, { 15.0f, -0.1069564819f }, { 75.0f, 0.0f } }},
	  {{ { 0.9999936223f, -0.003565193852f }, { 1.0f, 0.0f }, { 0.9999983907f, 0.001782605308f } }},
	  {{ { 0.9983081222f, 0.0f, 0.0f }, { 0.0f, 0.9983081222f, 0.0f }, { 0.0f, 0.0f, 0.9983081222f } }},
	  {{ { 115.0f, 75.0f }, { 152.499939f, 74.87874603f }, { 192.4999084f, 74.92822266f }, { 235.0f, 75.0f } }},
	},
	{ true, 0.5f, { 160.0f, 105.0f, 0.0f },
	  {{ { -45.0f, 0.0f }, { -15.0f, -0.1069564819f }, { 15.0f, -0.1069564819f }, { 75.0f, 0.0f } }},
	  {{ { 0.9999936223f, -0.003565193852f }, { 1.0f, 0.0f }, { 0.9999983907f, 0.001782605308f } }},
	  {{ { 0.8760977983f, 0.4786144495f, 0.0f }, { -0.4786144495f, 0.8760977983f, 0.0f }, { 0.0f, 0.0f, 0.9983081222f } }},
	  {{ { 115.0f, 105.0f }, { 152.499939f, 104.878746f }, { 192.4999084f, 104.9282227f }, { 235.0f, 105.0f } }},
	},
    };
    for (const auto& sample : cases) {
	CAPTURE (sample.stretch, sample.angle);
	auto data = ropeRig ();
	data.ikGroups[0].nodes[0].branches[0].flags = 5;
	data.bones.back ().rope->stretch = sample.stretch;
	// The authored control is at bind; the active control track moves and rotates it.
	data.controls = {{ .bone = 3, .bindWorld = glm::translate (glm::mat4 (1), glm::vec3 (45, 0, 0)) }};
	MdlAnimationClip clip;
	clip.controlFrames = {{ { .translation = { 75, 0, 0 }, .rotation = glm::angleAxis (sample.angle, glm::vec3 (0, 0, 1)) } }};
	MdlPhysicsState state;
	MdlPhysicsStep physics {
	    .state = state, .model = glm::translate (glm::mat4 (1), sample.model),
	    .rope = MdlRopeStep { .gravity = { 0, -90, 0 }, .deltaTime = 0.03447270021f },
	};
	const auto pose = MdlAnimationEvaluator::evaluate (data, {{ .animation = &clip }}, &physics);
	REQUIRE (state.ropePoints.size () == 4);
	for (size_t bone = 0; bone < 4; ++bone) {
	    for (int axis = 0; axis < 2; ++axis) {
		CHECK (pose.worldBones[bone][3][axis] == Catch::Approx (sample.positions[bone][axis]).margin (2e-4f));
		CHECK (state.ropePoints[bone].current[axis] == Catch::Approx (sample.corrected[bone][axis]).margin (2e-4f));
		if (bone < 3)
		    CHECK (pose.worldBones[bone][0][axis] == Catch::Approx (sample.basisX[bone][axis]).margin (2e-4f));
	    }
	    CHECK (pose.skinBones[bone] == pose.worldBones[bone] * data.bones[bone].inverseBindWorld);
	}
	for (int column = 0; column < 3; ++column) for (int axis = 0; axis < 3; ++axis)
	    CHECK (pose.worldBones.back ()[column][axis] == Catch::Approx (sample.endBasis[column][axis]).margin (2e-6f));
    }
}

TEST_CASE ("Unmeasured controlled rope branches retain their authored pose", "[mdl][animation][physics][rope][controlled]") {
    for (int route = 0; route < 5; ++route) {
	CAPTURE (route);
	auto data = ropeRig ();
	data.ikGroups[0].nodes[0].branches[0].flags = 5;
	data.controls = {{ .bone = 3, .bindWorld = glm::translate (glm::mat4 (1), glm::vec3 (75, 0, 0)) }};
	if (route == 0) data.controls[0].bone = 2;
	if (route == 1) data.controls[0].type = 1;
	if (route == 2) data.controls.push_back (data.controls[0]);
	if (route == 3) data.bones.back ().ikAimToTarget = true;
	if (route == 4) data.bones.back ().ikFollowEnd = true;
	MdlPhysicsState state;
	MdlPhysicsStep physics { .state = state, .rope = MdlRopeStep { .gravity = { 0, -90, 0 }, .deltaTime = 0.1f } };
	const auto pose = MdlAnimationEvaluator::evaluate (data, {}, &physics);
	CHECK (state.ropePoints.empty ());
	for (const auto& skin : pose.skinBones) CHECK (skin == glm::mat4 (1));
    }
}

TEST_CASE ("Serial rope nodes replay native shared-boundary ordering", "[mdl][animation][physics][rope][coupled]") {
    // Native coupled01 (40e2ce02), four complete updates. Reversing predictor nodes
    // differs by0.08007; failing to retain prior working poses loses the moving boundary.
    MdlAnimationData data;
    data.boneLengths = { 0, 20, 20, 20, 20, 20, 20 };
    data.boneDirections.resize (7);
    for (uint32_t bone = 0; bone < 7; ++bone) {
	data.bones.push_back ({ .type = 3, .parent = static_cast<int32_t> (bone) - 1,
	    .bindLocal = glm::translate (glm::mat4 (1), glm::vec3 (bone ? 20 : -60, 0, 0)),
	    .inverseBindWorld = glm::translate (glm::mat4 (1), glm::vec3 (60 - 20 * static_cast<int> (bone), 0, 0)) });
	if (bone) data.boneDirections[bone - 1][bone] = { 1, 0, 0 };
    }
    data.bones[3].ikFollowEnd = true;
    data.bones[3].rope = data.bones[6].rope = MdlBoneRope { .gravity = true, .mass = 1 };
    data.controls = {{ .bone = 6, .bindWorld = glm::translate (glm::mat4 (1), glm::vec3 (70, 0, 0)) }};
    data.ikGroups = {{ .nodes = {
	{ .root = 0, .branches = {{ .flags = 4, .maxLength = 60, .bones = { 0, 1, 2, 3 } }} },
	{ .root = 3, .branches = {{ .flags = 5, .maxLength = 60, .bones = { 3, 4, 5, 6 } }} },
    } }};
    struct Frame { float dt; std::array<glm::vec2, 7> positions; glm::vec2 rootBasis; glm::vec2 lastBasis; };
    const Frame frames[] = {
	{ 0.04950379953f, {{ { -60.0f, 0.0f }, { -40.0f, -0.2205581665f }, { -20.0f, -0.2205581665f }, { 0.0f, -0.2205581665f }, { 20.0f, -0.2205581665f }, { 40.0f, -0.2205581665f }, { 60.0f, 0.0f } }}, { 0.9999392033f, -0.01102723833f }, { 0.9999392033f, 0.01102723833f } },
	{ 0.04177010059f, {{ { -60.0f, 0.0f }, { -40.0012207f, -0.5981292725f }, { -20.0f, -0.5981445312f }, { 0.0f, -0.5981445312f }, { 20.0012207f, -0.5981445312f }, { 40.0012207f, -0.5981140137f }, { 59.99969864f, -0.0315092504f } }}, { 0.9995530248f, -0.02989492379f }, { 0.9995988607f, 0.02832102776f } },
	{ 0.03172549978f, {{ { -60.0f, 0.0f }, { -40.004776f, -1.066047668f }, { -20.00268555f, -1.066314697f }, { 0.0f, -1.066314697f }, { 20.00250244f, -1.066314697f }, { 40.00250244f, -1.06590271f }, { 59.99780655f, -0.08546793461f } }}, { 0.9985817671f, -0.05323950201f }, { 0.9988000393f, 0.04897441342f } },
	{ 0.03147879988f, {{ { -60.0f, 0.0f }, { -40.01506805f, -1.621879578f }, { -20.00753784f, -1.623664856f }, { 0.0f, -1.623664856f }, { 20.00613403f, -1.623664856f }, { 40.00613403f, -1.621162415f }, { 59.99303818f, -0.1524367332f } }}, { 0.9967231154f, -0.08088918775f }, { 0.9973109365f, 0.07328678668f } },
    };
    MdlPhysicsState state;
    for (const auto& frame : frames) {
	MdlPhysicsStep physics { .state = state,
	    .model = glm::translate (glm::mat4 (1), glm::vec3 (160, 120, 0)),
	    .rope = MdlRopeStep { .gravity = { 0, -90, 0 }, .deltaTime = frame.dt } };
	const auto pose = MdlAnimationEvaluator::evaluate (data, {}, &physics);
	REQUIRE (state.ropePoints.size () == 7);
	for (size_t bone = 0; bone < 7; ++bone) for (int axis = 0; axis < 2; ++axis)
	    CHECK (pose.worldBones[bone][3][axis] == Catch::Approx (frame.positions[bone][axis]).margin (2e-4f));
	for (int axis = 0; axis < 2; ++axis) {
	    CHECK (pose.worldBones[0][0][axis] == Catch::Approx (frame.rootBasis[axis]).margin (2e-6f));
	    CHECK (pose.worldBones[5][0][axis] == Catch::Approx (frame.lastBasis[axis]).margin (2e-6f));
	}
	CHECK (pose.worldBones[6][0].x == Catch::Approx (0.9983081222f).margin (2e-6f));
    }
    const std::array<glm::vec2, 7> corrected = {{ { 100.0289764f, 119.9976501f }, { 119.9779739f, 118.3804703f }, { 139.9889832f, 118.3763351f }, { 159.9889832f, 118.3763351f }, { 180.0077362f, 118.3763504f }, { 200.0077362f, 118.381813f }, { 219.993042f, 119.8475647f } }};
    const std::array<glm::vec2, 7> previous = {{ { 0.0f, 0.0f }, { 119.9911194f, 118.9346466f }, { 139.9955597f, 118.9336853f }, { 160.0f, 118.9336853f }, { 180.0038452f, 118.9336853f }, { 200.0038452f, 118.9350433f }, { 0.0f, 0.0f } }};
    for (size_t bone = 0; bone < 7; ++bone) for (int axis = 0; axis < 2; ++axis) {
	CHECK (state.ropePoints[bone].current[axis] == Catch::Approx (corrected[bone][axis]).margin (2e-4f));
	CHECK (state.ropePoints[bone].previous[axis] == Catch::Approx (previous[bone][axis]).margin (2e-4f));
    }
}

TEST_CASE ("Ordinary descendants use animated locals after the rope solve", "[mdl][animation][physics][rope][coupled]") {
    // Native coupled01: bind local Y10, enabled child track Y14, endpoint rotation0.5.
    auto data = ropeRig ();
    for (auto& bone : data.bones) bone.type = 3;
    data.ikGroups[0].nodes[0].branches[0].flags = 5;
    data.bones[3].rope->stretch = true;
    data.controls = {{ .bone = 3, .bindWorld = glm::translate (glm::mat4 (1), glm::vec3 (75, 0, 0)) }};
    data.bones.push_back ({ .parent = 3,
	.bindLocal = glm::translate (glm::mat4 (1), glm::vec3 (0, 10, 0)),
	.inverseBindWorld = glm::translate (glm::mat4 (1), glm::vec3 (-45, -10, 0)) });
    MdlAnimationClip clip;
    clip.boneFrames.resize (5);
    clip.boneFrames[4] = {{ .translation = { 0, 14, 0 } }};
    clip.controlFrames = {{ { .translation = { 75, 0, 0 }, .rotation = glm::angleAxis (0.5f, glm::vec3 (0, 0, 1)) } }};
    MdlPhysicsState state;
    MdlPhysicsStep physics { .state = state, .model = glm::translate (glm::mat4 (1), glm::vec3 (160, 60, 0)),
	.rope = MdlRopeStep { .gravity = { 0, -90, 0 }, .deltaTime = 0.04950379953f } };
    const auto pose = MdlAnimationEvaluator::evaluate (data, {{ .animation = &clip }}, &physics);
    REQUIRE (state.ropePoints.size () == 5);
    CHECK (pose.worldBones[4][3].x == Catch::Approx (68.29940033f).margin (2e-4f));
    CHECK (pose.worldBones[4][3].y == Catch::Approx (12.26536942f).margin (2e-4f));
    CHECK (pose.worldBones[4][0].x == Catch::Approx (0.8760977983f).margin (2e-6f));
    CHECK (pose.worldBones[4][0].y == Catch::Approx (0.4786144197f).margin (2e-6f));
    CHECK (pose.skinBones[4][3].x == Catch::Approx (33.66114044f).margin (2e-4f));
    CHECK (pose.skinBones[4][3].y == Catch::Approx (-18.03325653f).margin (2e-4f));
    for (int route = 0; route < 5; ++route) {
	auto rejected = data;
	if (route == 0) rejected.bones[4].type = 1;
	if (route == 1) rejected.bones[4].type = 2;
	if (route == 2) rejected.bones[4].parent = 4;
	if (route == 3) rejected.bones[4].rope = MdlBoneRope {};
	if (route == 4) rejected.bones[2].type = 0;
	MdlPhysicsState rejectedState;
	MdlPhysicsStep rejectedStep { .state = rejectedState, .rope = physics.rope };
	static_cast<void> (MdlAnimationEvaluator::evaluate (rejected, {{ .animation = &clip }}, &rejectedStep));
	CHECK (rejectedState.ropePoints.empty ());
	CHECK (rejectedState.ropeWorldBones.empty ());
    }
}

TEST_CASE ("Whole-skeleton single rope retains nonzero-root admission", "[mdl][animation][physics][rope][coupled]") {
    auto data = ropeRig ();
    const auto original = data;
    // Permute the old accepted synthetic skeleton; new topology gates must not
    // narrow this existing whole-skeleton path even for non-native bone order.
    for (uint32_t old = 0; old < 4; ++old) {
        const uint32_t bone = (old + 1) % 4;
        data.bones[bone] = original.bones[old];
        data.bones[bone].parent = old ? static_cast<int32_t> (old) : -1;
        data.boneLengths[bone] = original.boneLengths[old];
        data.boneDirections[bone].clear ();
        if (old < 3) data.boneDirections[bone][(old + 2) % 4] = { 1, 0, 0 };
    }
    data.ikGroups[0].root = data.ikGroups[0].nodes[0].root = 1;
    data.ikGroups[0].nodes[0].branches[0].bones = { 1, 2, 3, 0 };
    MdlPhysicsState state;
    MdlPhysicsStep physics { .state = state, .rope = MdlRopeStep { .gravity = { 0, -90, 0 }, .deltaTime = 0.01f } };
    static_cast<void> (MdlAnimationEvaluator::evaluate (data, {}, &physics));
    REQUIRE (state.ropePoints.size () == 4);
    CHECK (state.ropePoints[1].initialized);
}

TEST_CASE ("Serial ropes admit a pinned working-pose endpoint without a control", "[mdl][animation][physics][rope][serial-uncontrolled]") {
    // Source contract: flags4 takes the existing endpoint pose, flags5 a control.
    // This synthetic caller regression does not claim a native numeric capture.
    MdlAnimationData data;
    data.boneLengths = { 0, 20, 20, 20, 20, 20, 20 };
    data.boneDirections.resize (7);
    for (uint32_t bone = 0; bone < 7; ++bone) {
	data.bones.push_back ({ .type = 3, .parent = static_cast<int32_t> (bone) - 1,
	    .bindLocal = glm::translate (glm::mat4 (1), glm::vec3 (bone ? 20 : -60, 0, 0)),
	    .inverseBindWorld = glm::translate (glm::mat4 (1), glm::vec3 (60 - 20 * static_cast<int> (bone), 0, 0)) });
	if (bone) data.boneDirections[bone - 1][bone] = { 1, 0, 0 };
    }
    data.bones[3].ikFollowEnd = true;
    data.bones[3].rope = data.bones[6].rope = MdlBoneRope { .gravity = true, .mass = 1 };
    data.ikGroups = {{ .nodes = {
	{ .root = 0, .branches = {{ .flags = 4, .maxLength = 60, .bones = { 0, 1, 2, 3 } }} },
	{ .root = 3, .branches = {{ .flags = 4, .maxLength = 60, .bones = { 3, 4, 5, 6 } }} },
    } }};
    MdlPhysicsState state;
    MdlPhysicsStep physics { .state = state,
	.rope = MdlRopeStep { .gravity = { 0, -90, 0 }, .deltaTime = 0.1f } };
    const auto first = MdlAnimationEvaluator::evaluate (data, {}, &physics);
    REQUIRE (state.ropePoints.size () == 7);
    REQUIRE (state.ropeWorldBones.size () == 7);
    CHECK (state.ropePoints[0].initialized);
    CHECK (state.ropePoints[3].initialized);
    CHECK_FALSE (state.ropePoints[6].initialized);
    for (size_t bone = 1; bone < 6; ++bone) {
	CHECK (first.worldBones[bone][3].y == Catch::Approx (-0.9f).margin (1e-6f));
	CHECK (state.ropePoints[bone].previous.y == 0.0f);
    }
    CHECK (glm::vec3 (first.worldBones[6][3]) == glm::vec3 (60, 0, 0));
    CHECK (first.worldBones[6][0] == glm::vec4 (1, 0, 0, 0));
    CHECK (first.worldBones[6][1] == glm::vec4 (0, 1, 0, 0));
    const auto previousPoints = state.ropePoints;
    const auto second = MdlAnimationEvaluator::evaluate (data, {}, &physics);
    for (size_t bone = 1; bone < 6; ++bone) {
	// Reverse traversal refreshes the second node root from its retained working pose.
	const auto current = bone == 3 ? glm::vec3 (first.worldBones[bone][3]) : previousPoints[bone].current;
	const auto predicted = 2.0f * current - previousPoints[bone].previous
	    + glm::vec3 (0, -0.9f, 0);
	for (int axis = 0; axis < 3; ++axis) {
	    CHECK (second.worldBones[bone][3][axis] == Catch::Approx (predicted[axis]).margin (2e-5f));
	    CHECK (state.ropePoints[bone].previous[axis] == current[axis]);
	}
    }
    // The terminal target follows its retained working pose, clamped from the prior shared root.
    const glm::vec3 origin (first.worldBones[3][3]);
    const glm::vec3 delta = glm::vec3 (first.worldBones[6][3]) - origin;
    const glm::vec3 target = origin + delta / glm::length (delta) * 60.0f;
    for (int axis = 0; axis < 3; ++axis)
	CHECK (second.worldBones[6][3][axis] == Catch::Approx (target[axis]).margin (2e-5f));
    CHECK (second.worldBones[6][0] == first.worldBones[6][0]);
    CHECK (second.worldBones[6][1] == first.worldBones[6][1]);
    for (size_t bone = 0; bone < 7; ++bone) {
	const auto skin = second.worldBones[bone] * data.bones[bone].inverseBindWorld;
	for (int column = 0; column < 4; ++column) CHECK (second.skinBones[bone][column] == skin[column]);
    }
    for (int route = 0; route < 5; ++route) {
	auto rejected = data;
	if (route == 0) rejected.bones[6].ikAngleLimits = true;
	if (route == 1) rejected.ikGroups[0].nodes[1].branches[0].bones[0] = 2;
	if (route == 2) rejected.ikGroups[0].hasDependencies = true;
	if (route == 3) rejected.bones[4].type = 0;
	if (route == 4) rejected.ikGroups[0].nodes[1].branches[0].flags = 5;
	MdlPhysicsState rejectedState;
	MdlPhysicsStep rejectedStep { .state = rejectedState, .rope = physics.rope };
	const auto unchanged = MdlAnimationEvaluator::evaluate (rejected, {}, &rejectedStep);
	CHECK (rejectedState.ropePoints.empty ());
	CHECK (rejectedState.ropeWorldBones.empty ());
	CHECK (unchanged.worldBones[4][3].y == 0.0f);
    }
}

TEST_CASE ("Two pinned rope branches retain native initialization and ordered root fold", "[mdl][animation][physics][rope][siblings]") {
    // Native multibranch01 (40e2ce02): first four complete updates in both authored orders.
    struct Frame {
        float dt;
        std::array<glm::mat4, 5> world;
        std::array<glm::vec3, 5> current;
        std::array<glm::vec3, 5> previous;
    };
    for (int order = 0; order < 2; ++order) {
        const std::vector<Frame> frames = order == 0 ? std::vector<Frame> {
        { 0.0313628986f,
            {{ glm::mat4 (0.505301237f, 0.862942994f, 0.0f, 0.0f, -0.862942994f, 0.505301237f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (0.999995649f, 0.00295078289f, 0.0f, 0.0f, -0.00295078289f, 0.999995649f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 30.0f, -0.0885238647f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (0.630851209f, -0.775903881f, 0.0f, 0.0f, 0.775903881f, 0.630851209f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -160.0f, -90.0885239f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 40.0f, 0.0f, 1.0f) }},
            {{ glm::vec3 (160.0f, 90.0f, 0.0f), glm::vec3 (190.0f, 89.9114761f, 0.0f), glm::vec3 (220.0f, 90.0f, 0.0f), glm::vec3 (150.139236f, 107.400154f, 0.0f), glm::vec3 (160.0f, 130.0f, 0.0f) }},
            {{ glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (190.0f, 90.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) }} },
        { 0.0326396003f,
            {{ glm::mat4 (0.911316216f, -0.411707103f, 0.0f, 0.0f, 0.411707103f, 0.911316216f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (0.999958634f, 0.00909716729f, 0.0f, 0.0f, -0.00909716729f, 0.999958634f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 30.0f, -0.272926331f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (-0.516904712f, 0.856042862f, 0.0f, 0.0f, -0.856042862f, -0.516904712f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 140.278473f, 124.704422f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 40.0f, 0.0f, 1.0f) }},
            {{ glm::vec3 (160.0f, 90.0f, 0.0f), glm::vec3 (189.998764f, 89.7271042f, 0.0f), glm::vec3 (220.0f, 90.0f, 0.0f), glm::vec3 (166.440048f, 108.934776f, 0.0f), glm::vec3 (160.0f, 130.0f, 0.0f) }},
            {{ glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (190.0f, 89.9114761f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (150.139236f, 107.400154f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) }} },
        { 0.0329070017f,
            {{ glm::mat4 (0.911134541f, -0.412108958f, 0.0f, 0.0f, 0.412108958f, 0.911134541f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (0.999829113f, 0.0184861738f, 0.0f, 0.0f, -0.0184861738f, 0.999829113f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 29.9975281f, -0.554725647f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (0.653395295f, 0.757016897f, 0.0f, 0.0f, -0.757016897f, 0.653395295f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 22.74086f, 20.3719406f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 40.0f, 0.0f, 1.0f) }},
            {{ glm::vec3 (160.0f, 90.0f, 0.0f), glm::vec3 (189.994873f, 89.4456024f, 0.0f), glm::vec3 (220.0f, 90.0f, 0.0f), glm::vec3 (169.800812f, 107.43399f, 0.0f), glm::vec3 (160.0f, 130.0f, 0.0f) }},
            {{ glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (189.998764f, 89.7271042f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (166.440048f, 108.934776f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) }} },
        { 0.0330086015f,
            {{ glm::mat4 (0.937816441f, -0.347131491f, 0.0f, 0.0f, 0.347131491f, 0.937816441f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (0.99951607f, 0.0311075822f, 0.0f, 0.0f, -0.0311075822f, 0.99951607f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 29.9909821f, -0.933959961f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (0.878189564f, 0.478312761f, 0.0f, 0.0f, -0.478312761f, 0.878189564f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 13.1615753f, 15.835144f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 40.0f, 0.0f, 1.0f) }},
            {{ glm::vec3 (160.0f, 90.0f, 0.0f), glm::vec3 (189.985504f, 89.0676727f, 0.0f), glm::vec3 (220.0f, 90.0f, 0.0f), glm::vec3 (167.844238f, 108.397491f, 0.0f), glm::vec3 (160.0f, 130.0f, 0.0f) }},
            {{ glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (189.994873f, 89.4456024f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (169.800812f, 107.43399f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) }} }
        } : std::vector<Frame> {
        { 0.0313628986f,
            {{ glm::mat4 (0.253604293f, -0.967308104f, 0.0f, 0.0f, 0.967308104f, 0.253604293f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (0.92541641f, 0.378951788f, 0.0f, 0.0f, -0.378951788f, 0.92541641f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -160.0f, -90.0885239f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 19.9114761f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 40.0f, 0.0f, 1.0f) }},
            {{ glm::vec3 (160.0f, 90.0f, 0.0f), glm::vec3 (188.292297f, 80.0227203f, 0.0f), glm::vec3 (220.0f, 90.0f, 0.0f), glm::vec3 (160.0f, 110.0f, 0.0f), glm::vec3 (160.0f, 130.0f, 0.0f) }},
            {{ glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (160.0f, 110.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) }} },
        { 0.0326396003f,
            {{ glm::mat4 (0.987826228f, 0.155561432f, 0.0f, 0.0f, -0.155561432f, 0.987826228f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (-0.913038373f, -0.407873094f, 0.0f, 0.0f, 0.407873094f, -0.913038373f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 216.584595f, 69.9495544f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 19.9041214f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 40.0f, 0.0f, 1.0f) }},
            {{ glm::vec3 (160.0f, 90.0f, 0.0f), glm::vec3 (189.710175f, 94.1599121f, 0.0f), glm::vec3 (220.0f, 90.0f, 0.0f), glm::vec3 (160.0f, 110.0f, 0.0f), glm::vec3 (160.0f, 130.0f, 0.0f) }},
            {{ glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (188.292297f, 80.0227203f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (160.0f, 110.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) }} },
        { 0.0329070017f,
            {{ glm::mat4 (0.965214014f, 0.261461139f, 0.0f, 0.0f, -0.261461139f, 0.965214014f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (0.845955193f, -0.533254027f, 0.0f, 0.0f, 0.533254027f, 0.845955193f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 31.1280518f, 18.199646f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 19.9025421f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 40.0f, 0.0f, 1.0f) }},
            {{ glm::vec3 (160.0f, 90.0f, 0.0f), glm::vec3 (187.233231f, 102.583771f, 0.0f), glm::vec3 (220.0f, 90.0f, 0.0f), glm::vec3 (160.0f, 110.0f, 0.0f), glm::vec3 (160.0f, 130.0f, 0.0f) }},
            {{ glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (189.710175f, 94.1599121f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (160.0f, 110.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) }} },
        { 0.0330086015f,
            {{ glm::mat4 (0.939139247f, 0.343536705f, 0.0f, 0.0f, -0.343536705f, 0.939139247f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (0.860030234f, -0.510243058f, 0.0f, 0.0f, 0.510243058f, 0.860030234f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 24.7562866f, 20.9095688f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, 0.0f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 19.9019394f, 0.0f, 1.0f),
               glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 40.0f, 0.0f, 1.0f) }},
            {{ glm::vec3 (160.0f, 90.0f, 0.0f), glm::vec3 (187.382248f, 102.256119f, 0.0f), glm::vec3 (220.0f, 90.0f, 0.0f), glm::vec3 (160.0f, 110.0f, 0.0f), glm::vec3 (160.0f, 130.0f, 0.0f) }},
            {{ glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (187.233231f, 102.583771f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (160.0f, 110.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) }} }
        };
        MdlAnimationData data;
        const std::array<glm::vec3, 5> local = {{{0,0,0},{30,0,0},{30,0,0},{0,20,0},{0,20,0}}};
        const std::array<glm::vec3, 5> bind = {{{0,0,0},{30,0,0},{60,0,0},{0,20,0},{0,40,0}}};
        const int parents[] = {-1,0,1,0,3};
        data.boneLengths = {0,30,30,20,20};
        data.boneDirections.resize (5);
        for (uint32_t bone = 0; bone < 5; ++bone) {
            data.bones.push_back ({.type=3, .parent=parents[bone],
                .bindLocal=glm::translate (glm::mat4 (1), local[bone]),
                .inverseBindWorld=glm::translate (glm::mat4 (1), -bind[bone])});
            if (bone) data.boneDirections[parents[bone]][bone] = glm::normalize (local[bone]);
        }
        data.bones[2].rope = data.bones[4].rope = MdlBoneRope {.gravity=true, .mass=1};
        data.ikGroups = {{.nodes={{.root=0, .branches={
            {.flags=4, .maxLength=60, .bones={0,1,2}},
            {.flags=4, .maxLength=40, .bones={0,3,4}}
        }}}}};
        if (order) std::reverse (data.ikGroups[0].nodes[0].branches.begin (), data.ikGroups[0].nodes[0].branches.end ());
        MdlPhysicsState state;
        size_t frameIndex=0;
        for (const auto& frame : frames) {
            MdlPhysicsStep physics {.state=state, .model=glm::translate (glm::mat4 (1), glm::vec3 (160,90,0)),
                .rope=MdlRopeStep {.gravity={0,-90,0}, .deltaTime=frame.dt}};
            const auto pose = MdlAnimationEvaluator::evaluate (data, {}, &physics);
            REQUIRE (state.ropePoints.size () == 5);
            REQUIRE (state.ropeWorldBones.size () == 5);
            for (size_t bone=0; bone<5; ++bone) {
                CHECK (state.ropePoints[bone].initialized == (bone == 0));
                const auto expectedSkin = frame.world[bone] * data.bones[bone].inverseBindWorld;
                for (int column=0; column<4; ++column) for (int axis=0; axis<4; ++axis) {
                    CHECK (pose.worldBones[bone][column][axis] == Catch::Approx (frame.world[bone][column][axis]).margin (2e-4f));
                    CHECK (pose.skinBones[bone][column][axis] == Catch::Approx (expectedSkin[column][axis]).margin (2e-4f));
                }
                for (int axis=0; axis<3; ++axis) {
                    CHECK (state.ropePoints[bone].current[axis] == Catch::Approx (frame.current[bone][axis]).margin (2e-4f));
                    CHECK (state.ropePoints[bone].previous[axis] == Catch::Approx (frame.previous[bone][axis]).margin (2e-4f));
                }
            }
            if (frameIndex++ == 0) {
                const size_t unseeded = order == 0 ? 3 : 1;
                CHECK (state.ropePoints[unseeded].previous == glm::vec3 (0));
                CHECK (pose.worldBones[unseeded][3].x == -160.0f);
            }
        }
        for (int route=0; route<7; ++route) {
            auto rejected=data;
            auto& branches=rejected.ikGroups[0].nodes[0].branches;
            if (route==0) branches.push_back (branches[0]);
            if (route==1) branches[1].flags=5;
            if (route==2) rejected.bones[branches[1].bones.back ()].spring=MdlBoneSpring {};
            if (route==3) branches[1].bones[1]=branches[0].bones[1];
            if (route==4) rejected.ikGroups[0].hasDependencies=true;
            if (route==5) rejected.bones[1].type=0;
            if (route==6) rejected.controls.push_back ({.bone=2});
            MdlPhysicsState rejectedState;
            MdlPhysicsStep step {.state=rejectedState, .rope=MdlRopeStep {.gravity={0,-90,0}, .deltaTime=.03f}};
            const auto pose=MdlAnimationEvaluator::evaluate (rejected, {}, &step);
            CHECK (rejectedState.ropePoints.empty ());
            CHECK (rejectedState.ropeWorldBones.empty ());
        }
    }
}

TEST_CASE ("Kinematic graph replays native owner traversal and full graph state", "[mdl][animation][physics][rope][graphs]") {
    // Native oracle03, wallpaper64.exe40e2ce02: four consecutive positive outer publications.
    struct Frame {
        float dt;
        std::vector<glm::mat4> world;
        std::vector<glm::mat4> owners;
        std::vector<glm::vec3> current;
        std::vector<glm::vec3> previous;
    };
    const std::vector<Frame> frames = {
        { 0.0484232008f,
            { glm::mat4 (0.806113958f, 0.591760337f, 0.0f, 0.0f, -0.591760337f, 0.806113958f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 30.0f, -0.211029053f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, -0.211029053f, 0.0f, 1.0f), glm::mat4 (0.659066319f, -0.752084851f, 0.0f, 0.0f, 0.752084851f, 0.659066319f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -160.0f, -90.2110291f, 0.0f, 1.0f), glm::mat4 (0.93778348f, 0.342317641f, 0.0f, 0.0f, -0.342317641f, 0.93778348f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 0.0f, 50.0f, 0.0f, 1.0f), glm::mat4 (-0.821567178f, -0.570111156f, 0.0f, 0.0f, 0.570111156f, -0.821567178f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -160.0f, -90.2110291f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -30.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.946666598f, 0.322214693f, 0.0f, 0.0f, -0.322214693f, 0.946666598f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 80.0f, -0.211029053f, 0.0f, 1.0f), glm::mat4 (0.978408456f, -0.198333204f, -0.0f, 0.0f, 0.198333204f, 0.978408456f, 0.0f, 0.0f, 0.0f, -0.0f, 0.998308122f, 0.0f, 110.0f, 10.0f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (160.0f, 90.0f, 0.0f), glm::vec3 (190.0f, 89.7889709f, 0.0f), glm::vec3 (220.0f, 89.7889709f, 0.0f), glm::vec3 (147.770538f, 111.804588f, 0.0f), glm::vec3 (160.0f, 140.0f, 0.0f), glm::vec3 (145.297134f, 87.029213f, 0.0f), glm::vec3 (130.0f, 90.0f, 0.0f), glm::vec3 (245.440735f, 91.7472687f, 0.0f), glm::vec3 (270.0f, 100.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (190.0f, 90.0f, 0.0f), glm::vec3 (220.0f, 90.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (240.0f, 90.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } },
        { 0.031936001f,
            { glm::mat4 (0.51012218f, -0.860101819f, 0.0f, 0.0f, 0.860101819f, 0.51012218f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 30.0f, -0.513847351f, 0.0f, 1.0f), glm::mat4 (0.992053211f, 0.125818849f, 0.0f, 0.0f, -0.125818849f, 0.992053211f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, -0.513847351f, 0.0f, 1.0f), glm::mat4 (-0.524587393f, 0.851356924f, 0.0f, 0.0f, -0.851356924f, -0.524587393f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 135.541077f, 133.51738f, 0.0f, 1.0f), glm::mat4 (0.93778342f, 0.3423177f, 0.0f, 0.0f, -0.3423177f, 0.93778342f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 0.0f, 50.0f, 0.0f, 1.0f), glm::mat4 (0.886180997f, 0.46333921f, 0.0f, 0.0f, -0.46333921f, 0.886180997f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 130.594269f, 83.966629f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -30.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.94530201f, 0.326196432f, 0.0f, 0.0f, -0.326196432f, 0.94530201f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 90.8814697f, 3.40274811f, 0.0f, 1.0f), glm::mat4 (0.978408456f, -0.198333234f, -0.0f, 0.0f, 0.198333234f, 0.978408456f, 0.0f, 0.0f, 0.0f, -0.0f, 0.998308122f, 0.0f, 110.0f, 10.0f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (160.002197f, 89.9999619f, 0.0f), glm::vec3 (189.998901f, 89.4861908f, 0.0f), glm::vec3 (219.998901f, 89.4861526f, 0.0f), glm::vec3 (167.990097f, 113.689499f, 0.0f), glm::vec3 (160.0f, 140.0f, 0.0f), glm::vec3 (146.152466f, 95.7606049f, 0.0f), glm::vec3 (130.0f, 90.0f, 0.0f), glm::vec3 (245.455765f, 91.6334381f, 0.0f), glm::vec3 (270.0f, 100.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (190.0f, 89.7889709f, 0.0f), glm::vec3 (220.0f, 89.7889709f, 0.0f), glm::vec3 (147.770538f, 111.804588f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (145.297134f, 87.029213f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (245.440735f, 91.7472687f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } },
        { 0.0319325998f,
            { glm::mat4 (0.8919245f, -0.45218429f, 0.0f, 0.0f, 0.45218429f, 0.8919245f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 29.9978027f, -0.908363342f, 0.0f, 1.0f), glm::mat4 (0.995819747f, 0.0913401768f, 0.0f, 0.0f, -0.0913401768f, 0.995819747f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, -0.908439636f, 0.0f, 1.0f), glm::mat4 (0.655984163f, 0.75477469f, 0.0f, 0.0f, -0.75477469f, 0.655984163f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 28.2096558f, 25.4826355f, 0.0f, 1.0f), glm::mat4 (0.93778348f, 0.342317641f, 0.0f, 0.0f, -0.342317641f, 0.93778348f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 0.0f, 50.0f, 0.0f, 1.0f), glm::mat4 (0.763186932f, 0.646177828f, 0.0f, 0.0f, -0.646177828f, 0.763186932f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -12.9922028f, 14.4002228f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -30.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.944014907f, 0.329902768f, 0.0f, 0.0f, -0.329902768f, 0.944014907f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 85.4707947f, 1.42783356f, 0.0f, 1.0f), glm::mat4 (0.978408456f, -0.198333189f, -0.0f, 0.0f, 0.198333189f, 0.978408456f, 0.0f, 0.0f, 0.0f, -0.0f, 0.998308122f, 0.0f, 110.0f, 10.0f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (160.006866f, 89.999794f, 0.0f), glm::vec3 (189.996567f, 89.0918427f, 0.0f), glm::vec3 (219.996567f, 89.0915604f, 0.0f), glm::vec3 (172.244797f, 111.799751f, 0.0f), glm::vec3 (160.0f, 140.0f, 0.0f), glm::vec3 (146.710999f, 96.9438477f, 0.0f), glm::vec3 (130.0f, 90.0f, 0.0f), glm::vec3 (245.470551f, 91.5227356f, 0.0f), glm::vec3 (270.0f, 100.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (189.998901f, 89.4861908f, 0.0f), glm::vec3 (220.0f, 89.4861526f, 0.0f), glm::vec3 (167.990097f, 113.689499f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (146.152466f, 95.7606049f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (245.455765f, 91.6334381f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } },
        { 0.0318192989f,
            { glm::mat4 (0.939767003f, -0.34181568f, 0.0f, 0.0f, 0.34181568f, 0.939767003f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 29.9942322f, -1.39363098f, 0.0f, 1.0f), glm::mat4 (0.994373083f, 0.10593494f, 0.0f, 0.0f, -0.10593494f, 0.994373083f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 60.0f, -1.39415741f, 0.0f, 1.0f), glm::mat4 (0.877442181f, 0.479682446f, 0.0f, 0.0f, -0.479682446f, 0.877442181f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 16.4994965f, 19.8188782f, 0.0f, 1.0f), glm::mat4 (0.93778342f, 0.342317671f, 0.0f, 0.0f, -0.342317671f, 0.93778342f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 0.0f, 50.0f, 0.0f, 1.0f), glm::mat4 (0.906648278f, 0.421887249f, 0.0f, 0.0f, -0.421887249f, 0.906648278f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -12.7304688f, 8.03596497f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -30.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.942665756f, 0.333738327f, 0.0f, 0.0f, -0.333738327f, 0.942665756f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 85.4853363f, 1.32090759f, 0.0f, 1.0f), glm::mat4 (0.978408456f, -0.198333219f, -0.0f, 0.0f, 0.198333219f, 0.978408456f, 0.0f, 0.0f, 0.0f, -0.0f, 0.998308122f, 0.0f, 110.0f, 10.0f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (160.016159f, 89.9992523f, 0.0f), glm::vec3 (189.991913f, 88.6071167f, 0.0f), glm::vec3 (219.991913f, 88.6058426f, 0.0f), glm::vec3 (169.8284f, 112.993568f, 0.0f), glm::vec3 (160.0f, 140.0f, 0.0f), glm::vec3 (146.020279f, 95.3951111f, 0.0f), glm::vec3 (130.0f, 90.0f, 0.0f), glm::vec3 (245.483582f, 91.4046936f, 0.0f), glm::vec3 (270.0f, 100.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (189.996567f, 89.0918427f, 0.0f), glm::vec3 (220.0f, 89.0915604f, 0.0f), glm::vec3 (172.244797f, 111.799751f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (146.710999f, 96.9438477f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (245.470551f, 91.5227356f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } }
    };
    MdlAnimationData data;
    const std::vector<glm::vec3> local = { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (30.0f, 0.0f, 0.0f), glm::vec3 (30.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 20.0f, 0.0f), glm::vec3 (0.0f, 20.0f, 0.0f), glm::vec3 (-15.0f, 0.0f, 0.0f), glm::vec3 (-15.0f, 0.0f, 0.0f), glm::vec3 (20.0f, 0.0f, 0.0f), glm::vec3 (20.0f, 0.0f, 0.0f) };
    const std::vector<int> parents = { -1, 0, 1, 0, 3, 0, 5, 2, 7 };
    const std::vector<uint32_t> types = { 3, 3, 3, 3, 3, 3, 3, 3, 3 };
    data.boneLengths = { 0.0f, 30.0f, 30.0f, 20.0f, 20.0f, 15.0f, 15.0f, 20.0f, 20.0f };
    data.boneDirections.resize (local.size ());
    std::vector<glm::mat4> bind;
    for (uint32_t bone = 0; bone < local.size (); ++bone) {
        const auto matrix = glm::translate (glm::mat4 (1), local[bone]);
        bind.push_back ((parents[bone] < 0 ? glm::mat4 (1) : bind[parents[bone]]) * matrix);
        data.bones.push_back ({ .type=types[bone], .parent=parents[bone], .bindLocal=matrix, .inverseBindWorld=glm::inverse (bind.back ()) });
        if (parents[bone] >= 0) data.boneDirections[parents[bone]][bone] = glm::normalize (local[bone]);
    }
    data.bones[2].rope = MdlBoneRope { .gravity=true, .mass=1.0f, .stretch=false };
    data.bones[2].ikFollowEnd = true;
    data.bones[4].rope = MdlBoneRope { .gravity=true, .mass=1.0f, .stretch=true };
    data.bones[6].rope = MdlBoneRope { .gravity=true, .mass=1.0f, .stretch=false };
    data.bones[8].rope = MdlBoneRope { .gravity=true, .mass=1.0f, .stretch=true };
    data.controls.push_back ({ .bone=4, .bindWorld=glm::rotate (glm::translate (glm::mat4 (1), glm::vec3 (0.0f, 50.0f, 0.0f)), 0.35f, glm::vec3 (0,0,1)) });
    data.controls.push_back ({ .bone=8, .bindWorld=glm::rotate (glm::translate (glm::mat4 (1), glm::vec3 (110.0f, 10.0f, 0.0f)), -0.2f, glm::vec3 (0,0,1)) });
    data.ikGroups = { { .root=0, .nodes={ { .root=0, .branches={ { .flags=4, .maxLength=60.0f, .bones={ 0, 1, 2 } }, { .flags=5, .maxLength=40.0f, .bones={ 0, 3, 4 } }, { .flags=4, .maxLength=30.0f, .bones={ 0, 5, 6 } } } }, { .root=2, .branches={ { .flags=5, .maxLength=40.0f, .bones={ 2, 7, 8 } } } } } } };
    MdlAnimationClip clip;
    const std::vector<size_t> owners = { 0 };
    MdlPhysicsState state;
    for (const auto& frame : frames) {
        MdlPhysicsStep physics { .state=state, .model=glm::translate (glm::mat4 (1), glm::vec3 (160,90,0)),
            .rope=MdlRopeStep { .gravity={0,-90,0}, .deltaTime=frame.dt } };
        const auto pose = MdlAnimationEvaluator::evaluate (data, {{ .animation=&clip }}, &physics);
        REQUIRE (state.ropePoints.size () == data.bones.size ());
        REQUIRE (pose.publishedWorldBones.size () == data.bones.size ());
        for (size_t bone=0; bone<data.bones.size (); ++bone) {
            const auto found = std::find (owners.begin (), owners.end (), bone);
            const auto& published = found == owners.end () ? frame.world[bone] : frame.owners[found-owners.begin ()];
            const auto skin = frame.world[bone] * data.bones[bone].inverseBindWorld;
            for (int column=0; column<4; ++column) for (int axis=0; axis<4; ++axis) {
                CHECK (pose.worldBones[bone][column][axis] == Catch::Approx (frame.world[bone][column][axis]).margin (3e-4f));
                CHECK (pose.skinBones[bone][column][axis] == Catch::Approx (skin[column][axis]).margin (3e-4f));
                CHECK (pose.publishedWorldBones[bone][column][axis] == Catch::Approx (published[column][axis]).margin (3e-4f));
            }
            for (int axis=0; axis<3; ++axis) {
                CHECK (state.ropePoints[bone].current[axis] == Catch::Approx (frame.current[bone][axis]).margin (3e-4f));
                CHECK (state.ropePoints[bone].previous[axis] == Catch::Approx (frame.previous[bone][axis]).margin (3e-4f));
            }
        }
    }
}

TEST_CASE ("Kinematic owners replays native owner traversal and full graph state", "[mdl][animation][physics][rope][graphs]") {
    // Native oracle03, wallpaper64.exe40e2ce02: four consecutive positive outer publications.
    struct Frame {
        float dt;
        std::vector<glm::mat4> world;
        std::vector<glm::mat4> owners;
        std::vector<glm::vec3> current;
        std::vector<glm::vec3> previous;
    };
    const std::vector<Frame> frames = {
        { 0.0484232008f,
            { glm::mat4 (0.999944329f, -0.010550865f, 0.0f, 0.0f, 0.010550865f, 0.999944329f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -40.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -20.0f, -0.211029053f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -0.211029053f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 9.78897095f, 0.0f, 1.0f), glm::mat4 (0.999944329f, 0.0105510158f, 0.0f, 0.0f, -0.0105510158f, 0.999944329f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 20.0f, 9.78897095f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 39.9994431f, 9.99999714f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 39.9994431f, 23.9999962f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 9.78897095f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -40.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (120.000557f, 89.9999924f, 0.0f), glm::vec3 (139.999451f, 89.7889786f, 0.0f), glm::vec3 (160.0f, 89.7889709f, 0.0f), glm::vec3 (160.0f, 99.7889709f, 0.0f), glm::vec3 (180.0f, 99.7889709f, 0.0f), glm::vec3 (199.999451f, 100.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (140.0f, 90.0f, 0.0f), glm::vec3 (160.0f, 90.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (180.0f, 100.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } },
        { 0.031936001f,
            { glm::mat4 (0.999670088f, -0.0256845392f, 0.0f, 0.0f, 0.0256845392f, 0.999670088f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -40.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -20.0010986f, -0.513832092f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -0.513847351f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 9.48615265f, 0.0f, 1.0f), glm::mat4 (0.999670029f, 0.0256862231f, 0.0f, 0.0f, -0.0256862231f, 0.999670029f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 20.0f, 9.48615265f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 39.9967003f, 9.99996185f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 39.9967003f, 23.9999619f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 9.48615265f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -40.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (120.003296f, 89.9999161f, 0.0f), glm::vec3 (139.998352f, 89.4862518f, 0.0f), glm::vec3 (159.998352f, 89.4861526f, 0.0f), glm::vec3 (160.0f, 99.4861526f, 0.0f), glm::vec3 (180.0f, 99.4862366f, 0.0f), glm::vec3 (199.996704f, 99.9999619f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (139.999451f, 89.7889786f, 0.0f), glm::vec3 (160.0f, 89.7889709f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (180.0f, 99.7889709f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } },
        { 0.0319325998f,
            { glm::mat4 (0.998970151f, -0.0453719124f, 0.0f, 0.0f, 0.0453719124f, 0.998970151f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -40.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -20.0027466f, -0.908248901f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.00329589844f, -0.908439636f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.00329589844f, 9.09156036f, 0.0f, 1.0f), glm::mat4 (0.998969615f, 0.0453840531f, 0.0f, 0.0f, -0.0453840531f, 0.998969615f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 20.0f, 9.09172821f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 39.986393f, 9.99972725f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 39.986393f, 23.9997272f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.00329589844f, 9.09156036f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -40.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (120.008926f, 89.9995956f, 0.0f), glm::vec3 (139.992523f, 89.0921555f, 0.0f), glm::vec3 (159.992523f, 89.0915604f, 0.0f), glm::vec3 (159.996704f, 99.0915604f, 0.0f), glm::vec3 (179.996704f, 99.0920486f, 0.0f), glm::vec3 (199.986389f, 99.9997253f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (139.998352f, 89.4862518f, 0.0f), glm::vec3 (159.998352f, 89.4861526f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (180.0f, 99.4862366f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } },
        { 0.0318192989f,
            { glm::mat4 (0.997579813f, -0.0695309937f, 0.0f, 0.0f, 0.0695309937f, 0.997579813f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -40.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -20.0133057f, -1.39306641f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.0133056641f, -1.39415741f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.0133056641f, 8.60584259f, 0.0f, 1.0f), glm::mat4 (0.997578681f, 0.069547005f, 0.0f, 0.0f, -0.069547005f, 0.997578681f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 19.9934082f, 8.60673523f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 39.962429f, 9.99889183f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 39.962429f, 23.9988918f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -0.0133056641f, 8.60584259f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -40.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (120.017548f, 89.9987793f, 0.0f), glm::vec3 (139.977921f, 88.6081543f, 0.0f), glm::vec3 (159.977921f, 88.6058426f, 0.0f), glm::vec3 (159.986694f, 98.6058426f, 0.0f), glm::vec3 (179.986694f, 98.6079559f, 0.0f), glm::vec3 (199.962433f, 99.9988937f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (139.992523f, 89.0921555f, 0.0f), glm::vec3 (159.992523f, 89.0915604f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (179.996704f, 99.0920486f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } }
    };
    MdlAnimationData data;
    const std::vector<glm::vec3> local = { glm::vec3 (-40.0f, 0.0f, 0.0f), glm::vec3 (20.0f, 0.0f, 0.0f), glm::vec3 (20.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 10.0f, 0.0f), glm::vec3 (20.0f, 0.0f, 0.0f), glm::vec3 (20.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 10.0f, 0.0f) };
    const std::vector<int> parents = { -1, 0, 1, 2, 3, 4, 5 };
    const std::vector<uint32_t> types = { 3, 3, 3, 3, 3, 3, 0 };
    data.boneLengths = { 0.0f, 20.0f, 20.0f, 10.0f, 20.0f, 20.0f, 10.0f };
    data.boneDirections.resize (local.size ());
    std::vector<glm::mat4> bind;
    for (uint32_t bone = 0; bone < local.size (); ++bone) {
        const auto matrix = glm::translate (glm::mat4 (1), local[bone]);
        bind.push_back ((parents[bone] < 0 ? glm::mat4 (1) : bind[parents[bone]]) * matrix);
        data.bones.push_back ({ .type=types[bone], .parent=parents[bone], .bindLocal=matrix, .inverseBindWorld=glm::inverse (bind.back ()) });
        if (parents[bone] >= 0) data.boneDirections[parents[bone]][bone] = glm::normalize (local[bone]);
    }
    data.bones[2].rope = MdlBoneRope { .gravity=true, .mass=1.0f, .stretch=false };
    data.bones[2].ikFollowEnd = true;
    data.bones[5].rope = MdlBoneRope { .gravity=true, .mass=1.0f, .stretch=false };
    data.ikGroups = { { .root=3, .nodes={ { .root=3, .branches={ { .flags=4, .maxLength=40.0f, .bones={ 3, 4, 5 } } } } } }, { .root=0, .nodes={ { .root=0, .branches={ { .flags=4, .maxLength=40.0f, .bones={ 0, 1, 2 } } } } } } };
    MdlAnimationClip clip;
    clip.boneFrames.resize (7);
    clip.boneFrames[0] = {{ .translation=glm::vec3 (-40.0f, 0.0f, 0.0f) }};
    clip.boneFrames[1] = {{ .translation=glm::vec3 (20.0f, 0.0f, 0.0f) }};
    clip.boneFrames[2] = {{ .translation=glm::vec3 (20.0f, 0.0f, 0.0f) }};
    clip.boneFrames[3] = {{ .translation=glm::vec3 (0.0f, 10.0f, 0.0f) }};
    clip.boneFrames[4] = {{ .translation=glm::vec3 (20.0f, 0.0f, 0.0f) }};
    clip.boneFrames[5] = {{ .translation=glm::vec3 (20.0f, 0.0f, 0.0f) }};
    clip.boneFrames[6] = {{ .translation=glm::vec3 (0.0f, 14.0f, 0.0f) }};
    const std::vector<size_t> owners = { 3, 0 };
    MdlPhysicsState state;
    for (const auto& frame : frames) {
        MdlPhysicsStep physics { .state=state, .model=glm::translate (glm::mat4 (1), glm::vec3 (160,90,0)),
            .rope=MdlRopeStep { .gravity={0,-90,0}, .deltaTime=frame.dt } };
        const auto pose = MdlAnimationEvaluator::evaluate (data, {{ .animation=&clip }}, &physics);
        REQUIRE (state.ropePoints.size () == data.bones.size ());
        REQUIRE (pose.publishedWorldBones.size () == data.bones.size ());
        for (size_t bone=0; bone<data.bones.size (); ++bone) {
            const auto found = std::find (owners.begin (), owners.end (), bone);
            const auto& published = found == owners.end () ? frame.world[bone] : frame.owners[found-owners.begin ()];
            const auto skin = frame.world[bone] * data.bones[bone].inverseBindWorld;
            for (int column=0; column<4; ++column) for (int axis=0; axis<4; ++axis) {
                CHECK (pose.worldBones[bone][column][axis] == Catch::Approx (frame.world[bone][column][axis]).margin (3e-4f));
                CHECK (pose.skinBones[bone][column][axis] == Catch::Approx (skin[column][axis]).margin (3e-4f));
                CHECK (pose.publishedWorldBones[bone][column][axis] == Catch::Approx (published[column][axis]).margin (3e-4f));
            }
            for (int axis=0; axis<3; ++axis) {
                CHECK (state.ropePoints[bone].current[axis] == Catch::Approx (frame.current[bone][axis]).margin (3e-4f));
                CHECK (state.ropePoints[bone].previous[axis] == Catch::Approx (frame.previous[bone][axis]).margin (3e-4f));
            }
        }
    }
}

TEST_CASE ("Kinematic mixed replays native owner traversal and full graph state", "[mdl][animation][physics][rope][graphs]") {
    // Native oracle03, wallpaper64.exe40e2ce02: four consecutive positive outer publications.
    struct Frame {
        float dt;
        std::vector<glm::mat4> world;
        std::vector<glm::mat4> owners;
        std::vector<glm::vec3> current;
        std::vector<glm::vec3> previous;
    };
    const std::vector<Frame> frames = {
        { 0.0484232008f,
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.968769491f, 0.247962981f, 0.0f, 0.0f, -0.247962981f, 0.968769491f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 10.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.999944329f, 0.010550865f, 0.0f, 0.0f, -0.010550865f, 0.999944329f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 30.0f, -0.211029053f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 50.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (-0.125373244f, -0.992109716f, 0.0f, 0.0f, 0.992109716f, -0.125373244f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.298836708f, 17.4896393f, 0.0f, 1.0f), glm::mat4 (0.998308122f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 20.1410294f, 14.9821777f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 50.0f, 10.0f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 10.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (170.0f, 90.0f, 0.0f), glm::vec3 (189.998886f, 89.7891464f, 0.0f), glm::vec3 (210.0f, 90.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (190.0f, 90.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } },
        { 0.031936001f,
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.968998492f, 0.247066647f, 0.0f, 0.0f, -0.247066647f, 0.968998492f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 10.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.999670625f, 0.0256635062f, 0.0f, 0.0f, -0.0256635062f, 0.999670625f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 29.9977722f, -0.513496399f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 50.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (-0.122003317f, -0.992529631f, 0.0f, 0.0f, 0.992529631f, -0.122003317f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.199128151f, 17.4339581f, 0.0f, 1.0f), glm::mat4 (0.998308122f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 20.0497208f, 14.9938889f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 50.0f, 10.0f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 10.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (170.0f, 90.0f, 0.0f), glm::vec3 (189.993423f, 89.4870682f, 0.0f), glm::vec3 (210.0f, 90.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (189.998886f, 89.7891464f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } },
        { 0.0319325998f,
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.969956756f, 0.243277341f, 0.0f, 0.0f, -0.243277341f, 0.969956756f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 10.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.998974979f, 0.0452654846f, 0.0f, 0.0f, -0.0452654846f, 0.998974979f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 29.9879608f, -0.906784058f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 50.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (-0.120815754f, -0.992674947f, 0.0f, 0.0f, 0.992674947f, -0.120815754f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.164028168f, 17.4141808f, 0.0f, 1.0f), glm::mat4 (0.998308122f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 20.0175266f, 14.9978657f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 50.0f, 10.0f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 10.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (170.0f, 90.0f, 0.0f), glm::vec3 (189.979584f, 89.0965424f, 0.0f), glm::vec3 (210.0f, 90.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (189.993423f, 89.4870682f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } },
        { 0.0318192989f,
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.971310318f, 0.237815663f, 0.0f, 0.0f, -0.237815663f, 0.971310318f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 10.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (0.997618556f, 0.0689723864f, 0.0f, 0.0f, -0.0689723864f, 0.997618556f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 29.965744f, -1.38510895f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 50.0f, 0.0f, 0.0f, 1.0f), glm::mat4 (-0.120396733f, -0.99272573f, 0.0f, 0.0f, 0.99272573f, -0.120396733f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.15166378f, 17.4071903f, 0.0f, 1.0f), glm::mat4 (0.998308122f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 0.0f, 0.0f, 0.0f, 0.998308122f, 0.0f, 20.0061798f, 14.9992504f, 0.0f, 1.0f), glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 50.0f, 10.0f, 0.0f, 1.0f) },
            { glm::mat4 (1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 10.0f, 0.0f, 0.0f, 1.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (170.0f, 90.0f, 0.0f), glm::vec3 (189.95282f, 88.6270752f, 0.0f), glm::vec3 (210.0f, 90.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) },
            { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (189.979584f, 89.0965424f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 0.0f, 0.0f) } }
    };
    MdlAnimationData data;
    const std::vector<glm::vec3> local = { glm::vec3 (0.0f, 0.0f, 0.0f), glm::vec3 (10.0f, 0.0f, 0.0f), glm::vec3 (20.0f, 0.0f, 0.0f), glm::vec3 (20.0f, 0.0f, 0.0f), glm::vec3 (0.0f, 20.0f, 0.0f), glm::vec3 (0.0f, 20.0f, 0.0f), glm::vec3 (0.0f, 10.0f, 0.0f) };
    const std::vector<int> parents = { -1, 0, 1, 2, 1, 4, 3 };
    const std::vector<uint32_t> types = { 0, 3, 3, 3, 3, 3, 0 };
    data.boneLengths = { 0.0f, 10.0f, 20.0f, 20.0f, 20.0f, 20.0f, 10.0f };
    data.boneDirections.resize (local.size ());
    std::vector<glm::mat4> bind;
    for (uint32_t bone = 0; bone < local.size (); ++bone) {
        const auto matrix = glm::translate (glm::mat4 (1), local[bone]);
        bind.push_back ((parents[bone] < 0 ? glm::mat4 (1) : bind[parents[bone]]) * matrix);
        data.bones.push_back ({ .type=types[bone], .parent=parents[bone], .bindLocal=matrix, .inverseBindWorld=glm::inverse (bind.back ()) });
        if (parents[bone] >= 0) data.boneDirections[parents[bone]][bone] = glm::normalize (local[bone]);
    }
    data.bones[3].rope = MdlBoneRope { .gravity=true, .mass=1.0f, .stretch=false };
    data.bones[5].rope = MdlBoneRope { .gravity=false, .mass=1.0f, .stretch=false };
    data.controls.push_back ({ .bone=5, .bindWorld=glm::rotate (glm::translate (glm::mat4 (1), glm::vec3 (20.0f, 15.0f, 0.0f)), 0.0f, glm::vec3 (0,0,1)) });
    data.ikGroups = { { .root=1, .nodes={ { .root=1, .branches={ { .flags=4, .maxLength=40.0f, .bones={ 1, 2, 3 } }, { .flags=1, .maxLength=40.0f, .bones={ 1, 4, 5 } } } } } } };
    MdlAnimationClip clip;
    const std::vector<size_t> owners = { 1 };
    MdlPhysicsState state;
    for (const auto& frame : frames) {
        MdlPhysicsStep physics { .state=state, .model=glm::translate (glm::mat4 (1), glm::vec3 (160,90,0)),
            .rope=MdlRopeStep { .gravity={0,-90,0}, .deltaTime=frame.dt } };
        const auto pose = MdlAnimationEvaluator::evaluate (data, {{ .animation=&clip }}, &physics);
        REQUIRE (state.ropePoints.size () == data.bones.size ());
        REQUIRE (pose.publishedWorldBones.size () == data.bones.size ());
        for (size_t bone=0; bone<data.bones.size (); ++bone) {
            const auto found = std::find (owners.begin (), owners.end (), bone);
            const auto& published = found == owners.end () ? frame.world[bone] : frame.owners[found-owners.begin ()];
            const auto skin = frame.world[bone] * data.bones[bone].inverseBindWorld;
            for (int column=0; column<4; ++column) for (int axis=0; axis<4; ++axis) {
                CHECK (pose.worldBones[bone][column][axis] == Catch::Approx (frame.world[bone][column][axis]).margin (3e-4f));
                CHECK (pose.skinBones[bone][column][axis] == Catch::Approx (skin[column][axis]).margin (3e-4f));
                CHECK (pose.publishedWorldBones[bone][column][axis] == Catch::Approx (published[column][axis]).margin (3e-4f));
            }
            for (int axis=0; axis<3; ++axis) {
                CHECK (state.ropePoints[bone].current[axis] == Catch::Approx (frame.current[bone][axis]).margin (3e-4f));
                CHECK (state.ropePoints[bone].previous[axis] == Catch::Approx (frame.previous[bone][axis]).margin (3e-4f));
            }
        }
    }
}

TEST_CASE ("Rope working references remain separate from the fixed bind reconstruction", "[mdl][animation][physics][rope][graphs]") {
    // Native setup1401fbae0 copies raw bind worlds into328; caller initializes2c8 from optional references.
    auto data = ropeRig ();
    for (auto& bone : data.bones) bone.type = 3;
    data.bones[0].referenceLocal = glm::rotate (
        glm::translate (glm::mat4 (1), glm::vec3 (-45, 10, 0)), glm::half_pi<float> (), glm::vec3 (0, 0, 1)
    );
    MdlPhysicsState state;
    MdlPhysicsStep step { .state=state, .rope=MdlRopeStep { .deltaTime=0.03f } };
    const auto pose = MdlAnimationEvaluator::evaluate (data, {}, &step);
    REQUIRE (state.ropePoints.size () == 4);
    for (size_t bone=0; bone<4; ++bone) {
        CHECK (pose.worldBones[bone][3].x == Catch::Approx (-45).margin (2e-5f));
        CHECK (pose.worldBones[bone][3].y == Catch::Approx (10+30*bone).margin (2e-5f));
        CHECK (pose.worldBones[bone][0].x == Catch::Approx (0).margin (2e-5f));
        CHECK (pose.worldBones[bone][0].y == Catch::Approx (1).margin (2e-5f));
    }
}
