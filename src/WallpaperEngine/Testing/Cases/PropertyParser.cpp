#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <limits>

#include "WallpaperEngine/Data/Builders/ColorBuilder.h"
#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Model/PropertyAnimation.h"
#include "WallpaperEngine/Data/Parsers/PropertyParser.h"
#include "WallpaperEngine/Data/Parsers/UserSettingParser.h"

using WallpaperEngine::Data::Builders::ColorBuilder;
using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Model::DynamicValue;
using WallpaperEngine::Data::Model::PropertyAnimation;
using WallpaperEngine::Data::Model::PropertyKeyframe;
using WallpaperEngine::Data::Parsers::PropertyParser;
using WallpaperEngine::Data::Parsers::UserSettingParser;

TEST_CASE ("Linked property curves share their parent's pause, seek and mirror clock", "[property-animation]") {
    PropertyAnimation parent { .fps = 10.0f, .length = 10.0f, .mode = "mirror", .relative = false,
	.playing = false };
    const auto setting = UserSettingParser::parse (JSON::parse (R"({"value":0,
        "animation":{"c0":[{"frame":0,"value":0},{"frame":10,"value":1}],
        "options":{"parent":{"key":"angles"},"fps":10,"length":10,"mode":"mirror",
        "events":[{"frame":5,"name":"middle"}]}}})"), {});
    auto& child = *setting->animation;
    CHECK (child.parentKey == "angles");
    child.timelineParent = &parent;
    CHECK (child.frameAt (20.0f) == 0.0f);
    CHECK (child.takeEvents (20.0f).empty ());
    parent.play (20.0f);
    CHECK (child.evaluateFloat (0.0f, 20.5f) == Catch::Approx (0.5f));
    REQUIRE (child.takeEvents (20.5f).size () == 1);
    parent.pause (21.25f);
    CHECK (child.frameAt (99.0f) == Catch::Approx (7.5f));
    child.play (22.0f);
    CHECK (parent.isPlaying (22.0f));
    CHECK (child.frameAt (22.25f) == Catch::Approx (5.0f));
    child.setFrame (3.0f, 23.0f);
    CHECK (parent.frameAt (23.0f) == Catch::Approx (3.0f));
    child.stop (23.0f);
    CHECK_FALSE (parent.isPlaying (23.0f));
}

TEST_CASE ("Named property animations start paused and emit markers once", "[property-animation]") {
    const auto setting = UserSettingParser::parse (JSON::parse (R"({"value":1,
        "animation":{"c0":[{"frame":0,"value":0},{"frame":90,"value":0}],"relative":true,
        "options":{"name":"bubbles","fps":30,"length":90,"mode":"single","startpaused":true,
        "events":[{"frame":0,"name":"on"},{"frame":30,"name":"off"}]}}})"), {});
    auto& animation = *setting->animation;
    CHECK (animation.name == "bubbles");
    CHECK_FALSE (animation.isPlaying (12.0f));
    CHECK (animation.takeEvents (12.0f).empty ());
    animation.play (12.0f);
    const auto started = animation.takeEvents (12.0f);
    REQUIRE (started.size () == 1);
    CHECK (started[0].name == "on");
    CHECK (animation.takeEvents (12.5f).empty ());
    const auto stopped = animation.takeEvents (13.1f);
    REQUIRE (stopped.size () == 1);
    CHECK (stopped[0].name == "off");
    CHECK (animation.takeEvents (13.1f).empty ());
    CHECK_FALSE (animation.isPlaying (16.0f));
    animation.play (16.0f);
    REQUIRE (animation.takeEvents (16.0f).size () == 1);
    CHECK (animation.frameAt (16.0f) == 0.0f);
}

TEST_CASE ("Property animation pause preserves mirror direction and rate continuity", "[property-animation]") {
    PropertyAnimation animation { .fps = 10.0f, .length = 10.0f, .mode = "mirror", .relative = false };
    CHECK (animation.frameAt (1.5f) == Catch::Approx (5.0f));
    animation.pause (1.5f);
    CHECK (animation.frameAt (50.0f) == Catch::Approx (5.0f));
    animation.play (50.0f);
    CHECK (animation.frameAt (50.2f) == Catch::Approx (3.0f).margin (0.0001f));
    animation.setRate (2.0f, 50.2f);
    CHECK (animation.frameAt (50.3f) == Catch::Approx (1.0f).margin (0.0001f));
    animation.setFrame (4.0f, 51.0f);
    CHECK (animation.frameAt (51.1f) == Catch::Approx (6.0f).margin (0.0001f));
    animation.stop (51.1f);
    CHECK (animation.frameAt (100.0f) == 0.0f);
    CHECK_FALSE (animation.isPlaying (100.0f));
}

TEST_CASE ("Property animation events cross loop and mirror boundaries in order", "[property-animation]") {
    PropertyAnimation animation { .fps = 10.0f, .length = 10.0f, .mode = "loop", .relative = false,
        .events = {{0.0f, "start"}, {5.0f, "middle"}, {10.0f, "end"}} };
    const auto initial = animation.takeEvents (0.0f);
    REQUIRE (initial.size () == 1);
    CHECK (initial[0].name == "start");
    const auto loop = animation.takeEvents (1.2f);
    REQUIRE (loop.size () == 3);
    CHECK (loop[0].name == "middle");
    CHECK (loop[1].name == "end");
    CHECK (loop[2].name == "start");
    animation.mode = "mirror";
    animation.setFrame (10.0f, 2.0f);
    const auto mirror = animation.takeEvents (3.1f);
    REQUIRE (mirror.size () == 3);
    CHECK (mirror[0].name == "end");
    CHECK (mirror[1].name == "middle");
    CHECK (mirror[2].name == "start");
}

TEST_CASE ("Bool properties without a value default to false") {
    const JSON propertyData = {
	{ "type", "bool" },
	{ "text", "Enabled" },
    };

    const auto property = PropertyParser::parse (propertyData, "enabled");

    REQUIRE (property != nullptr);
    CHECK (property->getType () == DynamicValue::Boolean);
    CHECK_FALSE (property->getBool ());
}

TEST_CASE ("Directory properties are parsed as file-like properties") {
    const JSON propertyData = {
	{ "type", "directory" },
	{ "text", "Folder" },
    };

    const auto property = PropertyParser::parse (propertyData, "folder");

    REQUIRE (property != nullptr);
    CHECK (property->dump ().find ("folder - file") != std::string::npos);
}

TEST_CASE ("Scalar property animations evaluate absolute and relative values") {
    PropertyAnimation absolute {
	.channels = { { 0, { PropertyKeyframe { 0.0f, 1.0f }, PropertyKeyframe { 30.0f, 0.0f } } } },
	.fps = 30.0f,
	.length = 30.0f,
	.mode = "single",
	.relative = false,
    };
    CHECK (absolute.evaluateFloat (0.25f, 0.5f) == Catch::Approx (0.5f));
    CHECK (absolute.evaluateFloat (0.25f, 2.0f) == Catch::Approx (0.0f));

    PropertyAnimation relative {
	.channels = { { 0, { PropertyKeyframe { 0.0f, 0.0f }, PropertyKeyframe { 30.0f, 0.5f } } } },
	.fps = 30.0f,
	.length = 30.0f,
	.mode = "single",
	.relative = true,
    };
    CHECK (relative.evaluateFloat (0.25f, 0.5f) == Catch::Approx (0.5f));
}

TEST_CASE ("Relative angle animations resolve from the authored transform value") {
    // Tui and La (workshop 3642693674) rotate five times over a 75-second
    // timeline. The body stores four full turns in the base value and offsets
    // it with a relative c2 channel; all complete turns are visually identical.
    const auto data = JSON::parse (
	R"({
	    "value": "0 0 -31.41593",
	    "animation": {
		"c0": [{"frame": 0, "value": 0}, {"frame": 2249, "value": 0}],
		"c1": [{"frame": 0, "value": 0}, {"frame": 2249, "value": 0}],
		"c2": [{"frame": 0, "value": 6.2831898}, {"frame": 2249, "value": -25.132741}],
		"options": {"fps": 30, "length": 2250, "mode": "loop", "wraploop": true},
		"relative": true
	    }
	})"
    );

    const auto setting = UserSettingParser::parse (data, {});
    REQUIRE (setting->animation != nullptr);
    CHECK (setting->evaluateVec3 (0.0f).z == Catch::Approx (-25.1327402f));
    CHECK (setting->evaluateVec3 (2249.0f / 30.0f).z == Catch::Approx (-56.548671f));
    CHECK (setting->evaluateVec3 (75.0f).z == Catch::Approx (-25.1327402f));
}

TEST_CASE ("Property animations follow authored Bezier handles") {
    // Cyberpunk: Edgerunner-Lucy (workshop 3521337568) eases its opening
    // camera zoom from 3 to 1 over three seconds with normalized 0.833 handles.
    const auto data = JSON::parse (
	R"({
	    "value": 3,
	    "animation": {
		"c0": [
		    {"frame": 0, "value": 3,
		     "front": {"enabled": true, "x": 0.83333331, "y": 0}},
		    {"frame": 36, "value": 1,
		     "back": {"enabled": true, "x": -0.83333331, "y": 0}}
		],
		"options": {"fps": 12, "length": 60, "mode": "single"}
	    }
	})"
    );

    const auto setting = UserSettingParser::parse (data, {});
    REQUIRE (setting->animation != nullptr);
    CHECK (setting->evaluateFloat (0.0f) == Catch::Approx (3.0f));
    CHECK (setting->evaluateFloat (0.75f) > 2.5f);
    CHECK (setting->evaluateFloat (1.5f) == Catch::Approx (2.0f));
    CHECK (setting->evaluateFloat (2.25f) < 1.5f);
    CHECK (setting->evaluateFloat (3.0f) == Catch::Approx (1.0f));
    CHECK (setting->evaluateFloat (10.0f) == Catch::Approx (1.0f));

    const auto originData = JSON::parse (
	R"({
	    "value": "-244.73788 1468.74146 500",
	    "animation": {
		"c0": [{"frame":0,"value":159.19997},{"frame":36,"value":244.73788}],
		"c1": [{"frame":0,"value":1.59997},{"frame":36,"value":-1468.74146}],
		"c2": [{"frame":0,"value":0},{"frame":36,"value":0}],
		"options": {"fps":12,"length":60,"mode":"single"},
		"relative": true
	    }
	})"
    );
    const auto origin = UserSettingParser::parse (originData, {});
    CHECK (origin->evaluateVec3 (0.0f).x == Catch::Approx (-85.53791f));
    CHECK (origin->evaluateVec3 (0.0f).y == Catch::Approx (1470.34143f));
    CHECK (origin->evaluateVec3 (3.0f).x == Catch::Approx (0.0f).margin (0.0001f));
    CHECK (origin->evaluateVec3 (3.0f).y == Catch::Approx (0.0f).margin (0.0001f));
}

TEST_CASE ("Wrapped property animations return smoothly instead of holding and snapping") {
    // Sukuna Jujutsu Kaisen | Shinjuku Showdown Arc (4K), 3601846258,
    // HandDownLayer 67: the return stroke is generated by wraploop.
    const auto data = JSON::parse (R"({
        "value":"0 0 0", "animation":{
            "c2":[
                {"frame":8,"value":-0.1024953,"front":{"enabled":true,"x":0.97276264,"y":0}},
                {"frame":522,"value":0.18420318,"back":{"enabled":true,"x":-0.97276264,"y":0},
                 "front":{"enabled":true,"x":1,"y":0}}
            ],
            "options":{"fps":200,"length":1000,"mode":"loop","wraploop":true},"relative":true
        }
    })");
    const auto setting = UserSettingParser::parse (data, {});
    const float peak = setting->evaluateVec3 (2.61f).z;
    CHECK (peak == Catch::Approx (0.18420318f));
    CHECK (setting->evaluateVec3 (3.5f).z < peak - 0.05f);
    CHECK (setting->evaluateVec3 (4.5f).z < -0.05f);
    CHECK (setting->evaluateVec3 (4.999f).z == Catch::Approx (setting->evaluateVec3 (5.001f).z).margin (0.00001f));
    CHECK (setting->evaluateVec3 (3.5f) == setting->evaluateVec3 (8.5f));
    CHECK (setting->evaluateVec3 (-1.5f) == setting->evaluateVec3 (3.5f));
}

TEST_CASE ("Mirrored property animations keep reversing after the first sweep") {
    // Super Mario Voxel (2924081598), Root 228: a 10-second sweep each way.
    const auto setting = UserSettingParser::parse (
	JSON::parse (R"({
        "value":"0 0 0","animation":{
            "c1":[
                {"frame":0,"value":-0.57595867,"front":{"enabled":true,"x":0.50166667,"y":0}},
                {"frame":300,"value":0.57595867,"back":{"enabled":true,"x":-0.50166667,"y":0}}
            ],"options":{"fps":30,"length":300,"mode":"mirror"},"relative":true
        }
    })"),
	{}
    );
    CHECK (setting->evaluateVec3 (10.0f).y == Catch::Approx (0.57595867f));
    CHECK (setting->evaluateVec3 (12.5f).y < setting->evaluateVec3 (10.0f).y);
    CHECK (setting->evaluateVec3 (17.5f).y < 0.0f);
    CHECK (setting->evaluateVec3 (20.0f).y == Catch::Approx (-0.57595867f));
    CHECK (setting->evaluateVec3 (22.5f).y > setting->evaluateVec3 (20.0f).y);
    CHECK (setting->evaluateVec3 (7.5f) == setting->evaluateVec3 (12.5f));
    CHECK (setting->evaluateVec3 (2.5f) == setting->evaluateVec3 (42.5f));
}

TEST_CASE ("Property wrap closure trims excess keys and replaces an existing end key") {
    const auto setting = UserSettingParser::parse (
	JSON::parse (R"({
        "value":0,"animation":{
            "c0":[{"frame":0,"value":2,"front":{"enabled":true,"x":0.5,"y":1}},
                  {"frame":5,"value":4},{"frame":10,"value":6},{"frame":20,"value":8}],
            "options":{"fps":10,"length":10,"mode":"loop","wraploop":true}
        }
    })"),
	{}
    );
    const auto& keys = setting->animation->channels.at (0);
    REQUIRE (keys.size () == 3);
    CHECK (keys.back ().frame == 10.0f);
    CHECK (keys.back ().value == 2.0f);
    CHECK (keys.back ().incoming.offset == glm::vec2 (-0.5f, -1.0f));
}

TEST_CASE ("Property curve handles depend on the segment span rather than the playback FPS") {
    PropertyAnimation animation {
	.channels = { { 0,
			{ PropertyKeyframe { 0, 0, {}, { true, { 1, 0 } } },
			  PropertyKeyframe { 100, 1, { true, { -1, 0 } }, {} } } } },
	.fps = 10,
	.length = 100,
	.mode = "single",
	.relative = false,
    };
    const float slow = animation.evaluateFloat (0, 2.5f);
    animation.fps = 200;
    CHECK (animation.evaluateFloat (0, 0.125f) == Catch::Approx (slow));
    CHECK (slow < 0.2f); // Ease in, unlike linear 0.25.
    animation.channels.at (0).back ().incoming.enabled = false;
    CHECK (animation.evaluateFloat (0, 0.125f) < 0.25f); // A single enabled handle still shapes the curve.
}

TEST_CASE ("Short property rotations interpolate whole-frame curve samples") {
    // Bunk (2134765860), Fan auto 238: the native samples at 0, pi, 2*pi
    // produce uniform rotation even though the two-key curve has easing handles.
    const auto setting = UserSettingParser::parse (JSON::parse (R"({
        "value":"0 0 0", "animation":{
            "c2":[
                {"frame":0,"value":0,"front":{"enabled":true,"x":1,"y":0}},
                {"frame":2,"value":6.2831855,"back":{"enabled":true,"x":-1,"y":0}}
            ],"options":{"fps":4,"length":2,"mode":"loop"},"relative":true
        }
    })"), {});
    for (int tick = 0; tick < 32; ++tick) {
        const float expected = static_cast<float> (tick % 16) / 16.0f * 6.2831855f;
        CHECK (setting->evaluateVec3 (static_cast<float> (tick) / 32.0f).z
            == Catch::Approx (expected).margin (0.00001f));
    }
    setting->animation->mode = "single";
    CHECK (setting->evaluateVec3 (0.5f).z == Catch::Approx (6.2831855f));
    CHECK (setting->evaluateVec3 (1.0f).z == Catch::Approx (6.2831855f));
}

TEST_CASE ("Scripted values retain their last finite result") {
    DynamicValue intensity (2.1f);
    intensity.update (std::numeric_limits<float>::quiet_NaN (), DynamicValue::UpdateSource::Script);
    CHECK (intensity.getFloat () == Catch::Approx (2.1f));
    intensity.update (std::numeric_limits<float>::infinity (), DynamicValue::UpdateSource::Script);
    CHECK (intensity.getFloat () == Catch::Approx (2.1f));

    DynamicValue color (glm::vec3 (0.2f, 0.4f, 0.6f));
    color.update (
	glm::vec3 (0.8f, std::numeric_limits<float>::quiet_NaN (), 1.0f),
	DynamicValue::UpdateSource::Script
    );
    CHECK (color.getVec3 () == glm::vec3 (0.2f, 0.4f, 0.6f));
}

TEST_CASE ("Integer-looking normalized colors are not treated as byte colors") {
    const auto white = ColorBuilder::parse ("1 1 1");
    CHECK (white.r == Catch::Approx (1.0f));
    CHECK (white.g == Catch::Approx (1.0f));
    CHECK (white.b == Catch::Approx (1.0f));
    CHECK (white.a == Catch::Approx (1.0f));

    const auto green = ColorBuilder::parse ("0 1 0");
    CHECK (green.r == Catch::Approx (0.0f));
    CHECK (green.g == Catch::Approx (1.0f));
    CHECK (green.b == Catch::Approx (0.0f));
}

TEST_CASE ("Legacy byte colors remain supported") {
    const auto orange = ColorBuilder::parse ("255 128 0 255");
    CHECK (orange.r == Catch::Approx (1.0f));
    CHECK (orange.g == Catch::Approx (128.0f / 255.0f));
    CHECK (orange.b == Catch::Approx (0.0f));
    CHECK (orange.a == Catch::Approx (1.0f));
}
