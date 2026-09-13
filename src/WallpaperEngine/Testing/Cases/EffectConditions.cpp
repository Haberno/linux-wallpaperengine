#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Model/Effect.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/ObjectParser.h"
#include "WallpaperEngine/FileSystem/Container.h"

using WallpaperEngine::Data::JSON::JSON;
using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Data::Parsers::ObjectParser;

namespace {
ImageEffectUniquePtr loadEffect (const JSON& definition, const JSON& combos, const JSON& overrides = JSON::array ()) {
    auto filesystem = std::make_unique<WallpaperEngine::FileSystem::Container> ();
    filesystem->getVFS ().add ("effects/probe.json", definition.dump ());
    filesystem->getVFS ().add ("materials/probe.json", R"({"passes":[
	{"shader":"probe","combos":{"GATE":1}}, {"shader":"probe_second"}
    ]})");
    Project project {};
    project.assetLocator = std::make_unique<WallpaperEngine::Assets::AssetLocator> (std::move (filesystem));
    const auto object = ObjectParser::parse (JSON {
	{ "id", 1 }, { "name", "condition probe" }, { "text", "probe" },
	{ "effects", JSON::array ({ { { "file", "effects/probe.json" }, { "combos", combos },
	    { "passes", overrides } } }) },
    }, project);
    auto* text = object->as<Text> ();
    REQUIRE (text != nullptr);
    REQUIRE (text->effects.size () == 1);
    return std::move (text->effects.front ());
}
}

TEST_CASE ("effect conditions use native integer comparisons and a two-level AND", "[effect][conditions]") {
    struct Case {
	const char* combos;
	const char* conditions;
	bool enabled;
    };
    for (const auto& test : std::vector<Case> {
	     { R"({"A":2})", R"([{"A":2}])", true },
	     { R"({"A":2})", R"([{"A":1}])", false },
	     { R"({"A":2.9})", R"([{"A":2.1}])", true },
	     { R"({"A":-2.9})", R"([{"A":-2.1}])", true },
	     { R"({"A":2})", R"([{"A":{"op":"ge","value":2}}])", true },
	     { R"({"A":1})", R"([{"A":{"op":"ge","value":2}}])", false },
	     { R"({"A":2})", R"([{"A":{"op":"gt","value":2}}])", false },
	     { R"({"A":3})", R"([{"A":{"op":"gt","value":2}}])", true },
	     { R"({"A":2})", R"([{"A":{"op":"le","value":2}}])", true },
	     { R"({"A":3})", R"([{"A":{"op":"le","value":2}}])", false },
	     { R"({"A":2})", R"([{"A":{"op":"lt","value":2}}])", false },
	     { R"({"A":1})", R"([{"A":{"op":"lt","value":2}}])", true },
	     { R"({"A":2})", R"([{"A":{"op":"anything","value":2}}])", true },
	     { R"({"A":1})", R"([{"A":{"op":"anything","value":2}}])", false },
	     { R"({"A":2})", R"([{"A":{"value":2}}])", true },
	     { R"({"A":2})", R"([{"A":{"op":true,"value":2}}])", true },
	     { R"({"A":1,"B":2})", R"([{"A":1,"B":2},{"A":{"op":"gt","value":0}}])", true },
	     { R"({"A":1,"B":2})", R"([{"A":1,"B":3}])", false },
	     { R"({"A":1,"B":2})", R"([{"A":1},{"B":3}])", false },
	     { R"({})", R"([{"missing":0}])", true },
	     { R"({})", R"([{"missing":1}])", false },
	     { R"({"A":true,"B":"2","C":null,"D":[]})", R"([{"A":0,"B":0,"C":0,"D":0}])", true },
	     { R"([])", R"([{"A":0}])", true },
	     { R"({"A":0})", R"([{"A":{"op":"ge"}}])", true },
	     { R"({"A":1})", R"([{"A":{"value":"1"}}])", false },
	     { R"({"A":9})", R"([{"A":true},{"A":"9"},{"A":null},{"A":[]}])", true },
	     { R"({"A":9})", R"([null,2,false,"ignored",[],{}])", true },
	     { R"({"A":9})", R"([])", true },
	     { R"({"A":9})", R"({"A":0})", true },
	     { R"({"A":9})", R"(null)", true },
	 }) {
	INFO ("combos=" << test.combos << " conditions=" << test.conditions);
	JSON definition = JSON::parse (R"({"passes":[],"fbos":[{"name":"conditional"}]})");
	definition["fbos"][0]["conditions"] = JSON::parse (test.conditions);
	const auto effect = loadEffect (definition, JSON::parse (test.combos));
	CHECK (effect->effect->fbos.size () == (test.enabled ? 1 : 0));
    }
}

TEST_CASE ("effect FBO and bind conditions filter before required-field parsing", "[effect][conditions]") {
    const auto effect = loadEffect (JSON::parse (R"({
	"fbos":[
	    {"name":"always"},
	    {"name":"enabled","conditions":[{"GATE":1}]},
	    {"conditions":[{"GATE":0}]}
	],
	"passes":[{"material":"materials/probe.json","bind":[
	    {"index":0,"name":"always"},
	    {"index":1,"name":"enabled","conditions":[{"GATE":1}]},
	    {"conditions":[{"GATE":0}]}
	]}]
    })"), JSON::parse (R"({"GATE":1})"));
    REQUIRE (effect->effect->fbos.size () == 2);
    CHECK (effect->effect->fbos[0]->name == "always");
    CHECK (effect->effect->fbos[1]->name == "enabled");
    REQUIRE (effect->effect->passes.size () == 1);
    CHECK (effect->effect->passes[0]->binds == TextureMap { { 0, "always" }, { 1, "enabled" } });
}

TEST_CASE ("disabled effect passes retain original slots without loading their materials", "[effect][conditions]") {
    const auto effect = loadEffect (JSON::parse (R"({"passes":[
	{"material":"materials/does-not-exist.json","conditions":[{"GATE":1}]},
	{"command":"copy","source":"_rt_a","target":"_rt_b"},
	{"material":"materials/probe.json","bind":[
	    {"index":1,"name":"_rt_disabled","conditions":[{"GATE":1}]}
	]},
	{"command":"copy","conditions":[{"GATE":1}]}
    ]})"), JSON::parse (R"({"GATE":0})"), JSON::parse (R"([
	{"id":10,"combos":{"GATE":1}}, {"id":11}, {"id":12}, {"id":13}
    ])"));
    REQUIRE (effect->effect->passes.size () == 4);
    CHECK_FALSE (effect->effect->passes[0]->enabled);
    CHECK_FALSE (effect->effect->passes[0]->material.has_value ());
    CHECK (effect->effect->passes[1]->enabled);
    CHECK (effect->effect->passes[1]->command == Command_Copy);
    CHECK (effect->effect->passes[2]->enabled);
    REQUIRE (effect->effect->passes[2]->material.has_value ());
    CHECK (effect->effect->passes[2]->material.value ()->passes.size () == 2);
    CHECK (effect->effect->passes[2]->binds.empty ());
    CHECK_FALSE (effect->effect->passes[3]->enabled);
    CHECK_FALSE (effect->effect->passes[3]->command.has_value ());
    REQUIRE (effect->passOverrides.size () == 4);
    CHECK (effect->passOverrides[2]->id == 12);
}
