#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Model/PropertyAnimation.h"
#include "WallpaperEngine/Data/Parsers/PropertyParser.h"

using WallpaperEngine::Data::JSON::JSON;
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
