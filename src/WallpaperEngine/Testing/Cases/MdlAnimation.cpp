#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "WallpaperEngine/Data/Model/MdlAnimation.h"
#include "WallpaperEngine/Data/Parsers/MdlAnimationParser.h"

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

std::vector<char> makeAnimatedModelSections () {
    std::vector<char> data;

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

TEST_CASE ("MDL bone controls retain their extra animation pose tracks", "[mdl][animation]") {
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
    REQUIRE (parsed.animations.size () == 2);
    for (size_t clip = 0; clip < 2; ++clip) {
	const auto& animation = parsed.animations[clip];
	REQUIRE (animation.controlFrames.size () == 2);
	CHECK (animation.controlFrames[0][1].translation.x == Catch::Approx (2));
	CHECK (animation.controlFrames[0][1].translation.y == Catch::Approx (clip + 1));
	CHECK (animation.controlFrames[1][1].translation.x == Catch::Approx (3));
	CHECK (animation.boneFrames[1][1].translation.x == Catch::Approx (1));
    }
    patchU32 (data, controlledBoneOffset, 8);
    CHECK_THROWS (MdlAnimationParser::parse (data, "invalid-control.mdl"));
}

TEST_CASE ("MDLA events follow versioned tracks and cropped clip metadata", "[mdl][animation]") {
    auto data = makeAnimatedModelSections ();
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
    appendValue<uint8_t> (data, 1); // v4 constraint tracks
    for (int bone = 0; bone < 2; ++bone) {
	appendValue<uint32_t> (data, 1);
	appendValue<float> (data, 1);
	appendValue<uint16_t> (data, 1);
	appendValue<uint16_t> (data, 0);
	appendValue<uint32_t> (data, 61 * sizeof (float));
	for (int frame = 0; frame < 61; ++frame) appendValue<float> (data, 1);
    }
    for (int component = 0; component < 6; ++component) appendValue<float> (data, 0); // v5 bounds
    appendValue<uint8_t> (data, 1); // v6 scalar tracks
    for (int bone = 0; bone < 2; ++bone) {
	appendValue<uint32_t> (data, 0);
	appendValue<uint32_t> (data, 61 * sizeof (float));
	for (int frame = 0; frame < 61; ++frame) appendValue<float> (data, 1);
    }
    appendValue<uint16_t> (data, 0); // source clip/range descriptor, flags bit 0
    for (const uint32_t field : { 128u, 188u, 0u, UINT32_MAX }) appendValue<uint32_t> (data, field);
    appendValue<uint32_t> (data, 2);
    appendValue<float> (data, 59.0f / 24.0f);
    appendString (data, R"({"frame":59,"name":"end"})");
    appendValue<float> (data, 45.0f / 24.0f);
    appendString (data, R"({"frame":45,"name":"cry"})");
    patchU32 (data, endField, data.size ());

    const auto parsed = MdlAnimationParser::parse (data, "events.mdl");
    REQUIRE (parsed.animations.size () == 1);
    const auto& clip = parsed.animations.front ();
    CHECK (clip.flags == 0x401);
    REQUIRE (clip.events.size () == 2);
    CHECK (clip.events[0].frame == Catch::Approx (59));
    CHECK (clip.events[0].name == "end");
    CHECK (clip.events[1].frame == Catch::Approx (45));
    CHECK (clip.events[1].name == "cry");

    data.pop_back (); // A truncated event must not corrupt the usable bone data.
    patchU32 (data, endField, data.size ());
    const auto truncated = MdlAnimationParser::parse (data, "truncated-event.mdl");
    REQUIRE (truncated.animations.size () == 1);
    CHECK (truncated.animations.front ().events.empty ());
    CHECK (truncated.animations.front ().boneFrames.size () == 2);
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

TEST_CASE ("legacy constrained puppets use one shared resolved reference pose") {
    MdlAnimationData animationData;
    const glm::mat4 bind = glm::translate (glm::mat4 (1.0f), { 1000.0f, -400.0f, 0.0f });
    animationData.bones.push_back (
	{
	    .name = "constrained",
	    .bindLocal = bind,
	    .inverseBindWorld = glm::inverse (bind),
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
