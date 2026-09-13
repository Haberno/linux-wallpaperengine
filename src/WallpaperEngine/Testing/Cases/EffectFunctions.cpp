#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Model/Effect.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/EffectParser.h"
#include "WallpaperEngine/FileSystem/Container.h"

using WallpaperEngine::Data::JSON::JSON;
using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Data::Parsers::EffectParser;

namespace {
EffectUniquePtr loadFunctions (const JSON& definition) {
    auto filesystem = std::make_unique<WallpaperEngine::FileSystem::Container> ();
    filesystem->getVFS ().add ("effects/functions.json", definition.dump ());
    Project project {};
    project.assetLocator = std::make_unique<WallpaperEngine::Assets::AssetLocator> (std::move (filesystem));
    return EffectParser::load (project, "effects/functions.json");
}
}

TEST_CASE ("effect clear functions resolve only declared enabled FBO names", "[effect][functions]") {
    const auto effect = loadFunctions (JSON::parse (R"({
	"passes":[],
	"fbos":[{"name":"A"},{"name":"B"},{"name":"disabled","conditions":[{"GATE":1}]}],
	"functions":{
	    "clearB":{"action":"clear","fbos":["B"]},
	    "mixed":{"action":"clear","fbos":["missing","B",false,4,null,"","A","B","disabled"]},
	    "wrongAction":{"action":"Clear","fbos":["A"]},
	    "unknownAction":{"action":"copy","fbos":["A"]},
	    "noAction":{"fbos":["A"]},
	    "badAction":{"action":true,"fbos":["A"]},
	    "empty":{"action":"clear","fbos":[]},
	    "noMatches":{"action":"clear","fbos":["missing","disabled"]},
	    "noArray":{"action":"clear","fbos":"A"},
	    "noTargets":{"action":"clear"},
	    "":{"action":"clear","fbos":["A"]},
	    "notObject":[]
	}
    })"));
    REQUIRE (effect->functions.size () == 2);
    CHECK (effect->functions.at ("clearB") == std::vector<std::string> { "B" });
    CHECK (effect->functions.at ("mixed") == std::vector<std::string> { "B", "A", "B" });
}

TEST_CASE ("effect clear functions ignore non-object function tables", "[effect][functions]") {
    for (const auto& table : { JSON (), JSON (2), JSON ("clear"), JSON::array () }) {
	const auto effect = loadFunctions (JSON { { "passes", JSON::array () }, { "functions", table } });
	CHECK (effect->functions.empty ());
    }
    const auto effect = loadFunctions (JSON { { "passes", JSON::array () } });
    CHECK (effect->functions.empty ());
}

TEST_CASE ("effect FBO clear colors retain four floats and default missing channels to zero", "[effect][functions]") {
    const auto effect = loadFunctions (JSON::parse (R"({"passes":[],"fbos":[
	{"name":"rgba","clear":"0.25 0.5 -1 2"},
	{"name":"missing"},
	{"name":"wrongType","clear":[1,1,1,1]},
	{"name":"partial","clear":"0.5 0.25"},
	{"name":"empty","clear":""}
    ]})"));
    REQUIRE (effect->fbos.size () == 5);
    CHECK (effect->fbos[0]->clear == glm::vec4 (0.25f, 0.5f, -1.0f, 2.0f));
    CHECK (effect->fbos[0]->clearOnCreate);
    CHECK (effect->fbos[1]->clear == glm::vec4 (0.0f));
    CHECK_FALSE (effect->fbos[1]->clearOnCreate);
    CHECK (effect->fbos[2]->clear == glm::vec4 (0.0f));
    CHECK_FALSE (effect->fbos[2]->clearOnCreate);
    CHECK (effect->fbos[3]->clear == glm::vec4 (0.5f, 0.25f, 0.0f, 0.0f));
    CHECK_FALSE (effect->fbos[3]->clearOnCreate);
    CHECK (effect->fbos[4]->clear == glm::vec4 (0.0f));
    CHECK (effect->fbos[4]->clearOnCreate);
}

TEST_CASE ("effect initial clear activation follows native space-separated channel parsing", "[effect][functions]") {
    const auto effect = loadFunctions (JSON::parse (R"({"passes":[],"fbos":[
	{"name":"three","clear":"0.5 0.25 1"},
	{"name":"trailing","clear":"0.5 0.25 1 "},
	{"name":"space","clear":" "},
	{"name":"malformed","clear":"0.5 invalid 1 0.25"}
    ]})"));
    REQUIRE (effect->fbos.size () == 4);
    CHECK_FALSE (effect->fbos[0]->clearOnCreate);
    CHECK (effect->fbos[0]->clear == glm::vec4 (0.5f, 0.25f, 1.0f, 0.0f));
    CHECK (effect->fbos[1]->clearOnCreate);
    CHECK (effect->fbos[1]->clear == glm::vec4 (0.5f, 0.25f, 1.0f, 0.0f));
    CHECK_FALSE (effect->fbos[2]->clearOnCreate);
    CHECK (effect->fbos[3]->clearOnCreate);
    CHECK (effect->fbos[3]->clear == glm::vec4 (0.5f, 0.0f, 1.0f, 0.25f));
}
