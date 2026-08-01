#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/MaterialParser.h"
#include "WallpaperEngine/Data/Parsers/ObjectParser.h"
#include "WallpaperEngine/Data/Parsers/WallpaperParser.h"
#include "WallpaperEngine/FileSystem/Container.h"

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::JSON::parseCompatible;
using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Data::Parsers::MaterialParser;
using WallpaperEngine::Data::Parsers::ObjectParser;
using WallpaperEngine::Data::Parsers::WallpaperParser;
using WallpaperEngine::FileSystem::Container;

TEST_CASE ("optional tolerates authored type drift") {
    // workshop 3758354038 authors text "padding" as a vector string where older scenes
    // store a number; a mismatched optional must default, not std::terminate the engine
    const auto data = JSON::parse (R"({"padding": "32.00000 32.00000", "maxrows": 1})");

    REQUIRE (data.optional ("padding", 7) == 7);
    REQUIRE_FALSE (data.optional<int> ("padding").has_value ());
    REQUIRE (data.optional ("maxrows", 0) == 1);
    REQUIRE (data.optional ("missing", 3) == 3);
}

TEST_CASE ("dependencies tolerate the structured authoring form") {
    // workshop 3594400060, 2726424530 and 2787541254 author dependencies as
    // {"id": 104, "index": 0, "type": "collisionmodel"} instead of a bare id. the implicit
    // conversion to int threw type_error.302, and parseDependencies runs in both the parse
    // attempt and its fallback, so the wallpaper died outright
    const auto data = JSON::parse (
	R"({"id": 12, "name": "thing", "solid": true,
	    "dependencies": [7, {"id": 104, "index": 0, "type": "collisionmodel"}, {"index": 1}]})"
    );
    const Project project {};

    const auto object = ObjectParser::parse (data, project);
    REQUIRE (object->dependencies == std::vector {7, 104});
}

TEST_CASE ("orthographic camera layers provide animated projection settings") {
    auto filesystem = std::make_unique<Container> ();
    filesystem->getVFS ().add (
	"scene.json",
	R"({
	    "camera": {"center":"0 0 -1", "eye":"0 0 0", "up":"0 1 0"},
	    "general": {"orthogonalprojection":{"width":3840,"height":2160}},
	    "objects": [{
		"id":1640, "name":"opening camera", "camera":"default",
		"origin":{"value":"0 0 500"},
		"zoom":{"value":3,"animation":{
		    "c0":[{"frame":0,"value":3},{"frame":36,"value":1}],
		    "options":{"fps":12,"length":60,"mode":"single"}
		}}
	    }]
	})"
    );

    Project project {};
    project.type = Project::Type_Scene;
    project.assetLocator = std::make_unique<WallpaperEngine::Assets::AssetLocator> (std::move (filesystem));

    const auto wallpaper = WallpaperParser::parse (JSON ("scene.json"), project);
    REQUIRE (wallpaper->is<Scene> ());
    const auto* scene = wallpaper->as<Scene> ();
    REQUIRE (scene->camera.objectIds == std::vector { 1640 });
    REQUIRE (scene->camera.projection.zoom->animation != nullptr);
    CHECK (scene->camera.projection.zoom->evaluateFloat (0.0f) == Catch::Approx (3.0f));
    CHECK (scene->camera.projection.zoom->evaluateFloat (3.0f) == Catch::Approx (1.0f));
}

TEST_CASE ("missing image effects are skipped without discarding neighboring effects") {
    auto filesystem = std::make_unique<Container> ();
    filesystem->getVFS ().add ("effects/before.json", R"({"name":"before","passes":[]})");
    filesystem->getVFS ().add ("effects/after.json", R"({"name":"after","passes":[]})");

    Project project {};
    project.assetLocator = std::make_unique<WallpaperEngine::Assets::AssetLocator> (std::move (filesystem));

    const auto data = JSON::parse (
	R"({
	    "id": 42,
	    "name": "text with optional effects",
	    "text": "still visible",
	    "effects": [
		{"id": 1, "file": "effects/before.json"},
		{"id": 2, "file": "effects/missing-workshop-dependency.json"},
		{"id": 3},
		{"id": 4, "file": "effects/after.json"}
	    ]
	})"
    );

    const auto object = ObjectParser::parse (data, project);
    REQUIRE (object->is<Text> ());
    const auto* text = object->as<Text> ();
    REQUIRE (text->effects.size () == 2);
    CHECK (text->effects[0]->effect->name == "before");
    CHECK (text->effects[1]->effect->name == "after");
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

TEST_CASE ("Wallpaper Engine JSON comments and trailing commas are accepted narrowly") {
    const auto data = parseCompatible (
	R"json({
	    // Comments may appear between values.
	    "array": [1, /* inline */ 2,],
	    "lineCommentBeforeClose": [1, // still a trailing comma
	    ],
	    "object": {"value": 3, /* and between a trailing comma and its close */},
	    "literal": ",] // not a comment /* either */",
	})json",
	"test.json"
    );

    REQUIRE (data["array"].size () == 2);
    REQUIRE (data["lineCommentBeforeClose"].size () == 1);
    REQUIRE (data["object"]["value"] == 3);
    REQUIRE (data["literal"] == ",] // not a comment /* either */");
    REQUIRE_THROWS_AS (parseCompatible (R"({"still":"broken",oops})"), JSON::parse_error);
    REQUIRE_THROWS_AS (parseCompatible (R"({"unterminated": true /* comment})"), JSON::parse_error);
}
