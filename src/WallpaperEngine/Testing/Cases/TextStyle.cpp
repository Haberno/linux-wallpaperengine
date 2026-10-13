#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "WallpaperEngine/Render/Objects/TextStyle.h"

using namespace WallpaperEngine::Render::Objects;
using Catch::Approx;

TEST_CASE ("Native font styles force MSDF and retain disabled defaults", "[text-style]") {
    TextStyleInput input;
    auto result = computeTextStyleUniforms (input, 32);
    REQUIRE_FALSE (result.msdf);
    REQUIRE (result.renderVar0 == glm::vec4 (24, 0, 0, 0));
    input.outline = true; // msdf remains explicitly false
    input.outlineWidth = 6.8099999f;
    result = computeTextStyleUniforms (input, 32);
    REQUIRE (result.msdf);
    REQUIRE (result.enabled == glm::ivec3 (1, 0, 0));
    REQUIRE (result.renderVar0.y == Approx (1.6344f));
    input.outlineWidth = -5;
    REQUIRE (computeTextStyleUniforms (input, 32).renderVar0.y == Approx (.24f));
}

TEST_CASE ("Native shadow units match the installed snow bamboo clock", "[text-style]") {
    TextStyleInput input;
    input.shadow = true;
    input.shadowOpacity = .56999999f;
    input.shadowOffset = { -4.75f, 5.15f };
    const auto result = computeTextStyleUniforms (input, 36);
    REQUIRE (result.msdf);
    REQUIRE (result.enabled == glm::ivec3 (0, 0, 1));
    REQUIRE (result.renderVar0.w == Approx (1.28f));
    REQUIRE (result.renderVar1.w == Approx (-1.0133333f));
    REQUIRE (result.renderVar2.w == Approx (1.0986667f));
    REQUIRE (result.renderVar3.x == Approx (.56999999f));
}

TEST_CASE ("Native style caps preserve negative offsets and raw opacity", "[text-style]") {
    TextStyleInput input;
    input.outline = input.blur = input.shadow = true;
    input.outlineWidth = 14;
    input.blurRadius = 8;
    input.shadowRadius = 49;
    input.shadowOffset = { -100, 100 };
    input.shadowOpacity = 2;
    const auto result = computeTextStyleUniforms (input, 12);
    REQUIRE (result.enabled == glm::ivec3 (1, 1, 1));
    REQUIRE (result.renderVar0 == glm::vec4 (24, 0, 5.12f, 6));
    REQUIRE (result.renderVar1.w == Approx (-64));
    REQUIRE (result.renderVar2.w == Approx (6));
    REQUIRE (result.renderVar3.x == 2);
    input.outline = input.shadow = false;
    input.blurRadius = -.5;
    REQUIRE (computeTextStyleUniforms (input, 32).renderVar0.z == Approx (.0024f));
}
