#include <catch2/catch_test_macros.hpp>
#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/WallpaperParser.h"
#include "WallpaperEngine/FileSystem/Container.h"

using WallpaperEngine::Data::JSON::JSON;
using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Data::Parsers::WallpaperParser;
using WallpaperEngine::FileSystem::Container;

namespace {
bool authoredModelPolicy (JSON objects, bool perspective = false) {
    auto filesystem = std::make_unique<Container> ();
    JSON data = {
        { "camera", {{ "eye", "0 0 1" }, { "center", "0 0 0" }, { "up", "0 1 0" }} },
        { "general", {{ "orthogonalprojection", perspective ? JSON (nullptr)
              : JSON {{ "width", 320 }, { "height", 180 }} }} },
        { "objects", std::move (objects) }
    };
    filesystem->getVFS ().add ("scene.json", data.dump ());
    Project project {};
    project.type = Project::Type_Scene;
    project.assetLocator = std::make_unique<WallpaperEngine::Assets::AssetLocator> (std::move (filesystem));
    const auto wallpaper = WallpaperParser::parse (JSON ("scene.json"), project);
    REQUIRE (wallpaper->is<Scene> ());
    return wallpaper->as<Scene> ()->hasModelLayer;
}
}

TEST_CASE ("MSAA admission uses the raw model key independently of projection and visibility", "[msaa][parser]") {
    for (const bool perspective : { false, true }) {
        CHECK_FALSE (authoredModelPolicy (JSON::array (), perspective));
        // Text wins the fork's type-dispatch order; the native pre-scan still
        // sees any present, non-null model before layer parsing or setup.
        JSON object = {{ "id", 1 }, { "name", "policy marker" }, { "text", "M" }, { "visible", false }};
        CHECK_FALSE (authoredModelPolicy (JSON::array ({ object }), perspective));
        object["model"] = nullptr;
        CHECK_FALSE (authoredModelPolicy (JSON::array ({ object }), perspective));
        for (const JSON marker : { JSON ("models/example.mdl"), JSON (""), JSON (false), JSON (0) }) {
            object["model"] = marker;
            CHECK (authoredModelPolicy (JSON::array ({ object }), perspective));
        }
        object.erase ("model");
        object["nested"] = JSON {{ "model", "models/nested.mdl" }};
        CHECK_FALSE (authoredModelPolicy (JSON::array ({ object }), perspective));
    }
}

TEST_CASE ("MSAA admission is per scene after a model scene is parsed", "[msaa][parser]") {
    const JSON model = {{ "id", 1 }, { "name", "marker" }, { "text", "M" }, { "model", "models/example.mdl" }};
    CHECK (authoredModelPolicy (JSON::array ({ model })));
    CHECK_FALSE (authoredModelPolicy (JSON::array ()));
    CHECK (authoredModelPolicy (JSON::array ({ model })));
}
