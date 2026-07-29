#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <limits>

#include "WallpaperEngine/Data/Builders/ColorBuilder.h"
#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Model/PropertyAnimation.h"
#include "WallpaperEngine/Data/Parsers/PropertyParser.h"

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Builders::ColorBuilder;
using WallpaperEngine::Data::Model::DynamicValue;
using WallpaperEngine::Data::Model::PropertyAnimation;
using WallpaperEngine::Data::Model::PropertyKeyframe;
using WallpaperEngine::Data::Parsers::PropertyParser;

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
