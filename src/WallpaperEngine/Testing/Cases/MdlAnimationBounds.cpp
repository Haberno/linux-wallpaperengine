#include "WallpaperEngine/Data/Model/MdlAnimationBounds.h"
#include "WallpaperEngine/Render/Objects/ModelBounds.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "WallpaperEngine/Data/Parsers/MdlAnimationParser.h"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <glm/gtc/matrix_transform.hpp>

using namespace WallpaperEngine::Data::Model;

TEST_CASE ("Animation volumes union enabled clips independently of blend and playhead", "[animation-volume]") {
    MdlAnimationClip first, second, disabled, legacy;
    first.bounds = { { 2, 3, 4 }, { 5, 6, 7 } };
    second.bounds = { { -8, -9, -10 }, { -1, 2, 3 } };
    disabled.bounds = { glm::vec3 (-100), glm::vec3 (100) };
    const MdlAnimationBounds fallback { glm::vec3 (-50), glm::vec3 (50) };
    const auto result = evaluateMdlAnimationBounds ({
        { .animation = &first, .weight = 0.0f, .frame = 999.0f },
        { .animation = &second, .weight = .25f, .additive = true },
        { .animation = &disabled, .enabled = false },
        { .animation = nullptr },
    }, fallback);
    REQUIRE (result.has_value ());
    CHECK (result->minimum == glm::vec3 (-8, -9, -10));
    CHECK (result->maximum == glm::vec3 (5, 6, 7));
    // A zero-volume legacy clip still contributes its corners when another
    // enabled clip supplies the native nonzero-X admission condition.
    const auto mixed = evaluateMdlAnimationBounds ({ { .animation = &first }, { .animation = &legacy } }, fallback);
    REQUIRE (mixed.has_value ());
    CHECK (mixed->minimum == glm::vec3 (0));
    CHECK (mixed->maximum == first.bounds.maximum);
    const auto none = evaluateMdlAnimationBounds ({ { .animation = &disabled, .enabled = false } }, fallback);
    REQUIRE (none.has_value ());
    CHECK (none->minimum == fallback.minimum);
    CHECK (evaluateMdlAnimationBounds ({ { .animation = &legacy } }, fallback)->maximum == fallback.maximum);
    CHECK_FALSE (evaluateMdlAnimationBounds ({}, {}).has_value ());
    legacy.bounds = { { 2, -10, -10 }, { 2, 10, 10 } };
    CHECK (evaluateMdlAnimationBounds ({ { .animation = &legacy } }, fallback)->minimum == fallback.minimum);
}

TEST_CASE ("Animation volume culling uses the actual draw projection", "[animation-volume]") {
    using WallpaperEngine::Render::Objects::ModelBounds;
    ModelBounds::Box box;
    box.include ({ -1, -1, -1 }); box.include ({ 1, 1, 1 });
    const auto orthogonal = glm::ortho (-160.0f, 160.0f, -90.0f, 90.0f, -1000.0f, 1000.0f);
    const auto perspective = glm::perspective (glm::radians (60.0f), 320.0f / 180.0f, 5.0f, 15000.0f);
    const auto virtualView = glm::translate (glm::mat4 (1), glm::vec3 (0, 0, -155.88457f));
    const auto view = glm::lookAt (glm::vec3 (0, 0, 100), glm::vec3 (0), glm::vec3 (0, 1, 0));
    for (const auto projection : { orthogonal, perspective * view, perspective * virtualView }) {
        CHECK (box.intersects (projection));
        CHECK_FALSE (box.intersects (projection * glm::translate (glm::mat4 (1), glm::vec3 (10000, 0, 0))));
        CHECK (ModelBounds::Box {}.intersects (projection));
    }
}

TEST_CASE ("Installed Octillery clip volumes retain independent subclip bounds", "[.][animation-volume-installed]") {
    const char* path = std::getenv ("WPE_TEST_ANIMATION_VOLUME_MDL");
    REQUIRE (path != nullptr);
    std::ifstream input (path, std::ios::binary);
    REQUIRE (input.good ());
    const std::vector<char> data ((std::istreambuf_iterator<char> (input)), std::istreambuf_iterator<char> ());
    const auto parsed = WallpaperEngine::Data::Parsers::MdlAnimationParser::parse (data, path);
    const float expected[][6] = {
        { -62.403568267822266f, -9.074137687683105f, -92.41336822509766f, 60.16674041748047f, 98.99061584472656f, 253.93478393554688f },
        { -62.403568267822266f, -6.599850654602051f, -63.002323150634766f, 59.99330520629883f, 97.06790161132812f, 253.93478393554688f },
        { -59.65110778808594f, -9.074137687683105f, -92.41336822509766f, 60.16674041748047f, 98.99061584472656f, 253.93478393554688f },
    };
    REQUIRE (parsed.animations.size () == 3);
    for (size_t clip = 0; clip < 3; ++clip) {
        for (int axis = 0; axis < 3; ++axis) {
            CHECK (parsed.animations[clip].bounds.minimum[axis] == Catch::Approx (expected[clip][axis]));
            CHECK (parsed.animations[clip].bounds.maximum[axis] == Catch::Approx (expected[clip][axis + 3]));
        }
    }
}
