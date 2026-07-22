#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/MaterialParser.h"

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::JSON::parseCompatible;
using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Data::Parsers::MaterialParser;

TEST_CASE ("optional tolerates authored type drift") {
    // workshop 3758354038 authors text "padding" as a vector string where older scenes
    // store a number; a mismatched optional must default, not std::terminate the engine
    const auto data = JSON::parse (R"({"padding": "32.00000 32.00000", "maxrows": 1})");

    REQUIRE (data.optional ("padding", 7) == 7);
    REQUIRE_FALSE (data.optional<int> ("padding").has_value ());
    REQUIRE (data.optional ("maxrows", 0) == 1);
    REQUIRE (data.optional ("missing", 3) == 3);
}

TEST_CASE ("alpha-to-coverage material blending is preserved") {
    REQUIRE (MaterialParser::parseBlendMode ("alphatocoverage") == BlendingMode_AlphaToCoverage);
}

TEST_CASE ("omitted depth state uses 3D model defaults only in model context") {
    const auto material = JSON::parse (R"({"passes":[{"shader":"generic4"}]})");
    const auto explicitDisabled = JSON::parse (
	R"({"passes":[{"shader":"generic4","depthtest":"disabled","depthwrite":"disabled"}]})"
    );
    const Project project {};

    const auto imageMaterial = MaterialParser::parse (material, "image.json", project);
    REQUIRE (imageMaterial->passes.front ()->depthtest == DepthtestMode_Disabled);
    REQUIRE (imageMaterial->passes.front ()->depthwrite == DepthwriteMode_Disabled);

    const auto modelMaterial = MaterialParser::parse (material, "model.json", project, true);
    REQUIRE (modelMaterial->passes.front ()->depthtest == DepthtestMode_Enabled);
    REQUIRE (modelMaterial->passes.front ()->depthwrite == DepthwriteMode_Enabled);

    const auto overrideMaterial = MaterialParser::parse (explicitDisabled, "model.json", project, true);
    REQUIRE (overrideMaterial->passes.front ()->depthtest == DepthtestMode_Disabled);
    REQUIRE (overrideMaterial->passes.front ()->depthwrite == DepthwriteMode_Disabled);
}

TEST_CASE ("Wallpaper Engine JSON trailing commas are accepted narrowly") {
    const auto data = parseCompatible (R"({"array":[1,2,],"object":{"value":3,},"literal":",]"})", "test.json");

    REQUIRE (data["array"].size () == 2);
    REQUIRE (data["object"]["value"] == 3);
    REQUIRE (data["literal"] == ",]");
    REQUIRE_THROWS_AS (parseCompatible (R"({"still":"broken",oops})"), JSON::parse_error);
}
