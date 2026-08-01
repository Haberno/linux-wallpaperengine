#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <limits>

#include "WallpaperEngine/Data/Builders/ColorBuilder.h"
#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Model/PropertyAnimation.h"
#include "WallpaperEngine/Data/Parsers/PropertyParser.h"
#include "WallpaperEngine/Data/Parsers/UserSettingParser.h"

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Builders::ColorBuilder;
using WallpaperEngine::Data::Model::DynamicValue;
using WallpaperEngine::Data::Model::PropertyAnimation;
using WallpaperEngine::Data::Model::PropertyKeyframe;
using WallpaperEngine::Data::Parsers::PropertyParser;
using WallpaperEngine::Data::Parsers::UserSettingParser;

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
    // camera zoom from 3 to 1 over three seconds with 0.833-second handles.
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
