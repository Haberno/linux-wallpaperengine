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
	    // A side branch must retain its authored parent-relative placement.
	    data.bones.push_back ({
		.parent = 1, .bindLocal = glm::translate (glm::mat4 (1), glm::vec3 (0, 5, 0)),
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
