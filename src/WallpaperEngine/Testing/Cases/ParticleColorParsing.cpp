#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/ObjectParser.h"

#ifdef CHECK
#undef CHECK
#endif

#include <catch2/catch_test_macros.hpp>

using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Parsers::ObjectParser;

namespace {
ObjectUniquePtr parseParticle (const JSON& definition, const JSON& overrides = nullptr) {
    Project project {};
    JSON layer = {{"id", 1}, {"name", "color parser fixture"}, {"particle", definition}};
    if (!overrides.is_null ()) {
	layer["instanceoverride"] = overrides;
    }
    return ObjectParser::parse (layer, project);
}

void checkReference (const JSON& definition, const glm::vec3& expected, bool hasColor = true) {
    const auto object = parseParticle (definition);
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    CHECK (particle->hasColor == hasColor);
    CHECK (particle->colorReference == expected);
}
}

TEST_CASE ("particle color override absence retains the native disabled sentinel", "[particle][color][parser]") {
    const ParticleData defaults {};
    CHECK (defaults.colorReference == glm::vec3 (1));
    CHECK_FALSE (defaults.hasColor);
    for (const auto& overrides : {JSON (nullptr), JSON::object (), JSON::parse (R"({"colorn":"-1 -2 -3"})")}) {
	const auto object = parseParticle (JSON::object (), overrides);
	const auto* particle = object->as<Particle> ();
	REQUIRE (particle != nullptr);
	CHECK (particle->instanceOverride.colorn->value->getVec3 ()
	       == (overrides.contains ("colorn") ? glm::vec3 (-1, -2, -3) : glm::vec3 (-1)));
	CHECK (particle->instanceOverride.color->value->getVec3 () == glm::vec3 (1));
    }
    const auto object = parseParticle (JSON::object (), JSON::parse (R"({"colorn":"1 1 1"})"));
    CHECK (object->as<Particle> ()->instanceOverride.colorn->value->getVec3 () == glm::vec3 (1));
}

TEST_CASE ("legacy particle color is byte normalized once with legacy precedence", "[particle][color][parser]") {
    const struct { const char* color; glm::vec3 expected; } cases[] = {
	{"1 1 1", glm::vec3 (.00392f)},
	{"191.25 63.75 127.5", {.75f, .25f, .5f}},
	{"-255 510 0", {-1, 2, 0}},
    };
    for (const auto& test : cases) {
	CAPTURE (test.color);
	const auto object = parseParticle (JSON::object (), {{"color", test.color}, {"colorn", "0.9 0.8 0.7"}});
	const auto* particle = object->as<Particle> ();
	REQUIRE (particle != nullptr);
	CHECK (particle->instanceOverride.colorn->value->getVec3 () == test.expected);
	CHECK (particle->instanceOverride.color->value->getVec3 () == glm::vec3 (1));
    }
}

TEST_CASE ("normalized particle color keeps property and script bindings", "[particle][color][parser]") {
    Project project {};
    project.properties["tint"] = std::make_shared<PropertyColor> (PropertyData {"tint", "Tint"}, "0.2 0.4 0.6");
    const auto object = ObjectParser::parse (JSON::parse (R"({"id":2,"name":"bound color","particle":{
        "initializer":[{"name":"colorrandom","min":{"value":"0.2 0.4 0.6","user":"tint",
        "script":"export function update(value) { return value; }"}}]},
        "instanceoverride":{"colorn":{"value":"0.1234567 0.7654321 0.3333333","user":"tint",
        "script":"export function update(value) { return value; }",
        "scriptproperties":{"gain":{"value":0.5}},
        "animation":{"options":{"length":2},"c0":[{"frame":0,"value":0.1},{"frame":2,"value":0.9}]}}}})"), project);
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    const auto& setting = *particle->instanceOverride.colorn;
    CHECK (setting.property == project.properties["tint"]);
    REQUIRE (setting.value->getScriptSource ().has_value ());
    CHECK (*setting.value->getScriptSource () == "export function update(value) { return value; }");
    REQUIRE (setting.value->getProperties ().contains ("gain"));
    CHECK (setting.value->getProperties ().at ("gain")->value->getFloat () == .5f);
    REQUIRE (setting.animation != nullptr);
    CHECK (setting.animation->channels.at (0).size () == 2);
    project.properties["tint"]->update (glm::vec3 (.1234567f, .7654321f, .3333333f), DynamicValue::User);
    CHECK (setting.value->getVec3 () == glm::vec3 (.1234567f, .7654321f, .3333333f));
    const auto* initializer = particle->initializers[0]->as<ColorRandomInitializer> ();
    REQUIRE (initializer != nullptr);
    CHECK (initializer->min->property == project.properties["tint"]);
    CHECK (initializer->min->value->getScriptSource () == setting.value->getScriptSource ());
    CHECK (initializer->min->value->getVec3 () == setting.value->getVec3 ());
}

TEST_CASE ("particle reference uses the last color initializer with five decimal rounding", "[particle][color][parser]") {
    checkReference (JSON::parse (R"({"colorn":"0.2 0.4 0.6","initializer":[{"name":"sizerandom"}]})"),
		    glm::vec3 (1), false);
    checkReference (JSON::parse (R"({"initializer":[{"name":"colorrandom","min":"1 2 3","max":"1 2 3"}]})"),
		    {.00392f, .00784f, .01176f});
    checkReference (JSON::parse (R"({"initializer":[{"name":"colorrandom"}]})"), glm::vec3 (.5f));
    checkReference (JSON::parse (R"({"initializer":[
        {"name":"colorrandom","min":"255 0 0","max":"255 0 0"},
        {"name":"colorrandom","min":"0 0 0","max":"51 102 153"},{"name":"sizerandom"}],
        "operator":[{"name":"colorchange","startvalue":"0 1 0"}]})"), {.1f, .2f, .3f});
    checkReference (JSON::parse (R"({"initializer":[
        {"name":"colorrandom","min":"255 0 0","max":"255 0 0"},
        {"name":"colorlist","colors":["0.1234567 0.7654321 1.3333333","0 1 0"]}]})"),
		    {.12346f, .76543f, 1.33333f});
}

TEST_CASE ("particle reference takes the first colorchange only without a color initializer", "[particle][color][parser]") {
    checkReference (JSON::parse (R"({"initializer":[{"name":"sizerandom"}],"operator":[
        {"name":"movement"},{"name":"colorchange","startvalue":"0.1234567 0.7654321 1.3333333"},
        {"name":"colorchange","startvalue":"0 1 0"}]})"), {.12346f, .76543f, 1.33333f});
    checkReference (JSON::parse (R"({"operator":[{"name":"colorchange"}]})"), glm::vec3 (1));
}

TEST_CASE ("particle HSV reference clamps range midpoints before RGB conversion", "[particle][color][parser]") {
    checkReference (JSON::parse (R"({"initializer":[{"name":"hsvcolorrandom"}]})"), {.1875f, .75f, .75f});
    checkReference (JSON::parse (R"({"initializer":[{"name":"hsvcolorrandom",
        "saturationmin":null,"valuemin":null}]})"), {.25f, .5f, .5f});
    checkReference (JSON::parse (R"({"initializer":[{"name":"hsvcolorrandom", "huemin":0,"huemax":0.5,
        "saturationmin":1,"saturationmax":1,"valuemin":1,"valuemax":1}]})"), {.5f, 1, 0});
    checkReference (JSON::parse (R"({"initializer":[{"name":"hsvcolorrandom", "huemin":1,"huemax":1.5,
        "saturationmin":1,"saturationmax":3,"valuemin":1,"valuemax":3}]})"), {1, 0, 0});
}

TEST_CASE ("particle colorlist reference distinguishes missing and unusable lists", "[particle][color][parser]") {
    checkReference (JSON::parse (R"({"initializer":[{"name":"colorlist"}],
        "operator":[{"name":"colorchange","startvalue":"0 1 0"}]})"), glm::vec3 (1));
    for (const auto& colors : {JSON (nullptr), JSON::array (), JSON::parse (R"([3,"1 2"])")}) {
	checkReference ({{"initializer", JSON::array ({{{"name", "colorlist"}, {"colors", colors}}})},
	    {"operator", JSON::array ({{{"name", "colorchange"}, {"startvalue", "0 1 0"}}})}}, {0, 1, 0});
    }
    checkReference (JSON::parse (R"({"initializer":[
        {"name":"colorrandom","min":"51 102 153","max":"51 102 153"},
        {"name":"colorlist","colors":[]}]})"), {.2f, .4f, .6f});
    checkReference (JSON::parse (R"({"initializer":[{"name":"colorlist","colors":[7,"","0 1 0"]}]})"), glm::vec3 (0));
}

TEST_CASE ("colorrandom parses byte endpoints and the native exponent defaults", "[particle][color][parser]") {
    const auto object = parseParticle (JSON::parse (R"({"initializer":[
        {"name":"colorrandom"},{"name":"colorrandom","exponent":null},
        {"name":"colorrandom","exponent":2.5,"min":"1 1 1","max":"0.5 1 0"}]})"));
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->initializers.size () == 3);
    const float expected[] = {1, 0, 2.5f};
    for (size_t i = 0; i < 3; ++i) {
	const auto* initializer = particle->initializers[i]->as<ColorRandomInitializer> ();
	REQUIRE (initializer != nullptr);
	CHECK (initializer->exponent == expected[i]);
    }
    const auto* authored = particle->initializers[2]->as<ColorRandomInitializer> ();
    CHECK (authored->min->value->getVec3 () == glm::vec3 (1.0f / 255.0f));
    CHECK (authored->max->value->getVec3 () == glm::vec3 (.5f / 255.0f, 1.0f / 255.0f, 0));
}
