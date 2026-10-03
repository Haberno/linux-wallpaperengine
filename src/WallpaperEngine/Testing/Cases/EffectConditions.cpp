#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Model/Effect.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/ObjectParser.h"
#include "WallpaperEngine/Data/Parsers/EffectParser.h"
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

TEST_CASE ("effect fit distinguishes omission from an authored zero", "[effect][fbo][fit]") {
    const auto instance = loadEffect (JSON::parse (R"({"passes":[],"fbos":[
        {"name":"omitted"}, {"name":"null","fit":null}, {"name":"string","fit":"0"},
        {"name":"zero","fit":0}, {"name":"one","fit":1}, {"name":"limit","fit":4096},
        {"name":"outside","fit":4097}, {"name":"sentinel","fit":-1}
    ]})"), JSON::object ());
    const auto& targets = instance->effect->fbos;
    REQUIRE (targets.size () == 8);
    for (const size_t index : { 0, 1, 2, 7 }) CHECK (targets[index]->fit == 0xffff);
    CHECK (targets[3]->fit == 0);
    CHECK (targets[4]->fit == 1);
    CHECK (targets[5]->fit == 4096);
    CHECK (targets[6]->fit == 4097);
}

TEST_CASE ("only normalized cursor ripple targets receive float feedback", "[effect][cursor-ripple]") {
    for (const bool customized : { false, true }) {
        for (const std::string shader : { "effects/cursorripple_apply_force", "effects/cursorripple_simulate_force" }) {
            CAPTURE (customized, shader);
            auto filesystem = std::make_unique<WallpaperEngine::FileSystem::Container> ();
            filesystem->getVFS ().add ("materials/ripple.json", JSON {
                { "passes", JSON::array ({ { { "shader", shader } } }) }
            }.dump ());
            const std::string equations = customized ? "void main() { gl_FragColor = vec4(0); }" :
                "float timeAmt = min(1.0 / 30.0, g_Frametime) / 0.02;\n"
                "float pointerMoveAmt = v_PointDelta.x;\ngl_FragColor = albedo + colorAdd;\n"
                "float drop = max(1.001 / 255.0, decay / 255.0 * (g_Frametime / 0.02) * g_RippleDecay);\n"
                "albedo = force;\n";
            filesystem->getVFS ().add ("shaders/" + shader + ".frag", equations);
            filesystem->getVFS ().add ("effects/ripple.json", R"({
                "passes":[{"material":"materials/ripple.json","target":"history"},
                          {"material":"materials/ripple.json","target":"explicit"}],
                "fbos":[{"name":"history","format":"rgba8888"},
                        {"name":"unrelated","format":"rgba8888"},
                        {"name":"explicit","format":"rg1616f"}]
            })");
            Project project {};
            project.assetLocator = std::make_unique<WallpaperEngine::Assets::AssetLocator> (std::move (filesystem));
            const auto effect = WallpaperEngine::Data::Parsers::EffectParser::load (project, "effects/ripple.json");
            REQUIRE (effect->fbos.size () == 3);
            CHECK (effect->fbos[0]->format == (customized ? "rgba8888" : "rgba16161616f"));
            CHECK (effect->fbos[1]->format == "rgba8888");
            CHECK (effect->fbos[2]->format == "rg1616f");
        }
    }
}

TEST_CASE ("effect targets retain authored extents and native omitted sentinels", "[effect][fbo][size]") {
    const auto instance = loadEffect (JSON::parse (R"({"passes":[],"fbos":[
	{"name":"glitter","format":"r8","width":256,"height":256},
	{"name":"width","format":"rgba8888","width":128},
	{"name":"fallback","format":"rgba8888","width":4097,"height":null}
    ]})"), JSON::object ());
    const auto& targets = instance->effect->fbos;
    REQUIRE (targets.size () == 3);
    CHECK (targets[0]->width == 256);
    CHECK (targets[0]->height == 256);
    CHECK (targets[1]->width == 128);
    CHECK (targets[1]->height == 0xffff);
    CHECK (targets[2]->width == 4097);
    CHECK (targets[2]->height == 0xffff);
}
