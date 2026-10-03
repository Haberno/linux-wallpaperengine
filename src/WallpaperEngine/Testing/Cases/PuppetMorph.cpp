#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <fstream>

#include "WallpaperEngine/Data/Model/MdlMorph.h"
#include "WallpaperEngine/Data/Parsers/MdlAnimationParser.h"
#include "WallpaperEngine/Data/Parsers/MdlParser.h"

using namespace WallpaperEngine::Data::Model;

TEST_CASE ("Puppet morph modifiers select radial or signed bone-axis falloff", "[mdl][morph-modifiers]") {
    const glm::mat4 inverseBone (1.0f);
    for (const uint32_t rule : { 0u, 1u, 4u, 5u }) {
        const MdlMorphModifier modifier { .rule = rule, .range = { 0, 10 } };
        CHECK (evaluatePuppetMorphModifier (modifier, { -3, 4, 100 }, inverseBone) == Catch::Approx (.5f));
        CHECK (evaluatePuppetMorphModifier (modifier, { 0, 0, 100 }, inverseBone) == 0.0f);
        CHECK (evaluatePuppetMorphModifier (modifier, { 6, 8, 0 }, inverseBone) == 1.0f);
        CHECK (evaluatePuppetMorphModifier (modifier, { 9, 12, 0 }, inverseBone) == 1.0f);
    }
    for (const uint32_t rule : { 2u, 3u, 6u, 7u }) {
        const MdlMorphModifier modifier { .rule = rule, .range = { 0, 10 } };
        CHECK (evaluatePuppetMorphModifier (modifier, { -3, 4, 100 }, inverseBone) == 0.0f);
        CHECK (evaluatePuppetMorphModifier (modifier, { 2.5f, 40, 100 }, inverseBone) == Catch::Approx (.15625f));
        CHECK (evaluatePuppetMorphModifier (modifier, { 5, 0, 0 }, inverseBone) == Catch::Approx (.5f));
        CHECK (evaluatePuppetMorphModifier (modifier, { 10, 0, 0 }, inverseBone) == 1.0f);
        CHECK (evaluatePuppetMorphModifier (modifier, { 15, 0, 0 }, inverseBone) == 1.0f);
    }
}

TEST_CASE ("Puppet morph falloff uses the animated bone frame and unmorphed skinned position", "[mdl][morph-modifiers]") {
    const auto boneWorld = glm::scale (
        glm::rotate (glm::translate (glm::mat4 (1), glm::vec3 (20, -10, 7)),
            glm::radians (90.0f), glm::vec3 (0, 0, 1)), glm::vec3 (2, 3, 1));
    const auto inverseBone = glm::inverse (boneWorld);
    const MdlMorphModifier axis { .rule = 2, .range = { -2, 6 } };
    const MdlMorphModifier radial { .rule = 0, .range = { 0, 10 } };
    const glm::vec3 skinned = boneWorld * glm::vec4 (2, 4, 90, 1);
    CHECK (evaluatePuppetMorphModifier (axis, skinned, inverseBone) == Catch::Approx (.5f));
    const glm::vec3 radialSkinned = boneWorld * glm::vec4 (-3, 4, 90, 1);
    CHECK (evaluatePuppetMorphModifier (radial, radialSkinned, inverseBone) == Catch::Approx (.5f));
    // Blend skinning occurs before modifier evaluation, not a per-influence
    // blend of independently evaluated nonlinear falloffs.
    const glm::vec3 blended = .5f * glm::vec3 (0, 0, 0) + .5f * glm::vec3 (5, 0, 0);
    CHECK (evaluatePuppetMorphModifier (radial, blended, glm::mat4 (1)) == Catch::Approx (.15625f));
    CHECK (evaluatePuppetMorphModifier (axis, { -4, 0, 0 }, glm::mat4 (1)) == 0.0f);
    CHECK (evaluatePuppetMorphModifier (axis, { 8, 0, 0 }, glm::mat4 (1)) == 1.0f);
}

TEST_CASE ("Puppet morph offsets decode signed packed positions independently of alpha", "[mdl][morph-deformation]") {
    MdlSubmesh body { .flags = 0x1000, .vertexTag = 0x0181000e, .morphScale = 40, .morphVertexCount = 2 };
    body.morphTargets.push_back (MdlMorphTarget {
        .positionDeltas = { 32767, -32768, 0, 0, 16384, -16384 }, .alphaValues = { -32768, 0 },
    });
    const std::vector<float> weights { .75f };
    const auto targets = preparePuppetMorphTargets (body, weights, 1, {});
    REQUIRE (targets.size () == 1);
    const auto first = evaluatePuppetMorphOffset (body, targets, 1, glm::vec3 (0));
    CHECK (first.x == Catch::Approx (30));
    CHECK (first.y == Catch::Approx (-30));
    CHECK (first.z == 0);
    const auto second = evaluatePuppetMorphOffset (body, targets, 2, glm::vec3 (0));
    CHECK (second.x == 0);
    CHECK (second.y == Catch::Approx (30 * 16384.0f / 32767.0f));
    CHECK (second.z == Catch::Approx (-30 * 16384.0f / 32767.0f));
    for (float index : { 0.0f, -1.0f, 3.0f })
        CHECK (evaluatePuppetMorphOffset (body, targets, index, glm::vec3 (0)) == glm::vec3 (0));
    body.flags = 0;
    CHECK (preparePuppetMorphTargets (body, weights, 1, {}).empty ());
}

TEST_CASE ("Puppet morph target upload respects sparse masks and eleven target capacity", "[mdl][morph-deformation]") {
    MdlSubmesh body { .flags = 0x1000, .vertexTag = 0x01810008, .morphScale = 1, .morphVertexCount = 1 };
    body.morphTargets.resize (12);
    for (auto& target : body.morphTargets) target.positionDeltas = { 32767, 0, 0 };
    std::vector<float> weights (12, 1);
    weights[0] = 0;
    const auto targets = preparePuppetMorphTargets (body, weights, 0xfff, {});
    REQUIRE (targets.size () == 11);
    CHECK (targets.front ().target == &body.morphTargets[0]);
    CHECK (targets.back ().target == &body.morphTargets[10]);
    CHECK (evaluatePuppetMorphOffset (body, targets, 1, glm::vec3 (0)).x == 10);
    const auto sparse = preparePuppetMorphTargets (body, weights, 0xffe, {});
    REQUIRE (sparse.size () == 11);
    CHECK (sparse.back ().target == &body.morphTargets[11]);
    CHECK (evaluatePuppetMorphOffset (body, sparse, 1, glm::vec3 (0)).x == 11);
    CHECK (preparePuppetMorphTargets (body, weights, 0, {}).empty ());
    body.vertexTag = 0x01800009;
    CHECK (preparePuppetMorphTargets (body, weights, 1, {}).empty ());
}

TEST_CASE ("Puppet morph target modifiers share original posed position and use current bone frames", "[mdl][morph-deformation]") {
    MdlSubmesh body { .flags = 0x3000, .vertexTag = 0x0181000e, .morphScale = 40, .morphVertexCount = 1 };
    body.morphTargets = {
        MdlMorphTarget { .positionDeltas = { 32767, 0, 0 },
            .modifier = MdlMorphModifier { .boneIndex = 0, .rule = 2, .range = { 0, 10 } } },
        MdlMorphTarget { .positionDeltas = { 32767, 0, 0 },
            .modifier = MdlMorphModifier { .boneIndex = 1, .rule = 2, .range = { 0, 10 } } },
    };
    const std::vector<float> weights { .5f, .5f };
    const std::vector<glm::mat4> world { glm::mat4 (1), glm::translate (glm::mat4 (1), glm::vec3 (5, 0, 0)) };
    const auto targets = preparePuppetMorphTargets (body, weights, 3, world);
    REQUIRE (targets.size () == 2);
    // Raw skin weights total2: position2.5 becomes5 before modifier evaluation.
    // Target0 contributes40*.5*smoothstep(0,10,5)=10; target1 remains0.
    const glm::vec3 originalPosed = glm::mat4 (2) * glm::vec4 (2.5f, 0, 0, 1);
    CHECK (evaluatePuppetMorphOffset (body, targets, 1, originalPosed).x == Catch::Approx (10));
    CHECK (preparePuppetMorphTargets (body, weights, 3, std::span (world).first (1)).empty ());
    body.morphTargets[1].modifier.reset ();
    CHECK (preparePuppetMorphTargets (body, weights, 3, world).empty ());
}

TEST_CASE ("Reze layered puppet retains its authored morph deformation", "[.][mdl][morph-deformation-installed]") {
    const auto* path = std::getenv ("WPE_TEST_MORPH_LAYOUT_MDL");
    if (!path) SKIP ("Set WPE_TEST_MORPH_LAYOUT_MDL to the extracted Reze sl_puppet.mdl");
    std::ifstream input (path, std::ios::binary);
    REQUIRE (input.good ());
    const std::vector<char> bytes { std::istreambuf_iterator<char> (input), std::istreambuf_iterator<char> () };
    const auto mesh = WallpaperEngine::Data::Parsers::MdlParser::parse (bytes, path);
    const auto data = WallpaperEngine::Data::Parsers::MdlAnimationParser::parse (bytes, path);
    REQUIRE (mesh.submeshes.size () == 1);
    const auto& body = mesh.submeshes.front ();
    REQUIRE (body.morphTargets.size () == 1);
    CHECK (body.morphTargets.front ().id == 437);
    CHECK (body.morphVertexCount == 19);
    std::vector<MdlActiveAnimation> layers;
    for (const uint32_t id : { 1678u, 2078u, 258u, 3194u }) {
        const auto clip = std::find_if (data.animations.begin (), data.animations.end (),
            [id] (const auto& value) { return value.id == id; });
        REQUIRE (clip != data.animations.end ());
        layers.push_back ({ .animation = &*clip, .weight = 1, .additive = id != 1678, .frame = 30 });
    }
    const auto pose = MdlAnimationEvaluator::evaluate (data, layers);
    CHECK (pose.puppetMorphMask == 1);
    REQUIRE (pose.puppetMorphWeights.size () == 1);
    CHECK (pose.puppetMorphWeights.front () == Catch::Approx (1));
    const auto targets = preparePuppetMorphTargets (body, pose.puppetMorphWeights, pose.puppetMorphMask, pose.worldBones);
    REQUIRE (targets.size () == 1);
    size_t changed = 0;
    float maximum = 0;
    for (size_t vertex = 0; vertex < body.vertices.size () * sizeof (float) / body.strideBytes; ++vertex) {
        const float index = body.vertices[vertex * body.strideBytes / sizeof (float) + 3];
        const float distance = glm::length (evaluatePuppetMorphOffset (body, targets, index, glm::vec3 (0)));
        changed += distance > 0.000001f;
        maximum = std::max (maximum, distance);
    }
    CHECK (changed == 19);
    CHECK (maximum == Catch::Approx (7.95975f).margin (0.0001f));
}

TEST_CASE ("Puppet morph alpha multiplies signed target factors before interpolation", "[mdl][morph-alpha]") {
    MdlSubmesh body { .flags = 0x1000, .vertexTag = 0x0181000e, .morphScale = 40, .morphVertexCount = 3 };
    body.morphTargets = {
        MdlMorphTarget { .positionDeltas = { 32767, 0, 0, 0, 0, 0, 0, 0, 0 },
            .alphaValues = { 0, 32767, -32768 } },
        MdlMorphTarget { .positionDeltas = std::vector<int16_t> (9), .alphaValues = { 0, 0, 0 } },
    };
    std::vector<float> weights { .5f, .25f };
    auto targets = preparePuppetMorphTargets (body, weights, 3, {});
    auto value = evaluatePuppetMorph (body, targets, 1, glm::vec3 (0));
    CHECK (value.x == Catch::Approx (20));
    CHECK (value.w == Catch::Approx (.375f)); // .5 * .75, not a sum or mean.
    CHECK (evaluatePuppetMorph (body, targets, 2, glm::vec3 (0)).w == Catch::Approx (.75f));
    CHECK (evaluatePuppetMorph (body, targets, 3, glm::vec3 (0)).w == 0);
    for (float index : { 0.0f, -1.0f, 4.0f })
        CHECK (evaluatePuppetMorph (body, targets, index, glm::vec3 (0)).w == 1);
    CHECK (evaluatePuppetMorph (body, {}, 1, glm::vec3 (0)).w == 1);
    weights = { .75f, 0 };
    targets = preparePuppetMorphTargets (body, weights, 3, {});
    CHECK (evaluatePuppetMorph (body, targets, 3, glm::vec3 (0)).w == Catch::Approx (-.5f));
    // Native leaves the product unclamped until the fragment/blending stage.
    body.flags |= 0x2000;
    for (auto& target : body.morphTargets)
        target.modifier = MdlMorphModifier { .boneIndex = 0, .rule = 2, .range = { 0, 10 } };
    const std::vector<glm::mat4> world { glm::translate (glm::mat4 (1), glm::vec3 (5, 0, 0)) };
    weights = { 1, .5f };
    targets = preparePuppetMorphTargets (body, weights, 3, world);
    CHECK (evaluatePuppetMorph (body, targets, 1, { 5, 0, 0 }).w == 1);
    CHECK (evaluatePuppetMorph (body, targets, 1, { 10, 0, 0 }).w == Catch::Approx (.375f));
    CHECK (evaluatePuppetMorph (body, targets, 1, { 15, 0, 0 }).w == 0);
}

TEST_CASE ("Puppet clipping mask alpha ignores the position modifier", "[mdl][morph-mask-alpha]") {
    MdlSubmesh body { .flags = 0x3000, .vertexTag = 0x0181000e, .morphScale = 1, .morphVertexCount = 1 };
    body.morphTargets = { MdlMorphTarget { .positionDeltas = { 0, 0, 0 }, .alphaValues = { 0 },
        .modifier = MdlMorphModifier { .boneIndex = 0, .rule = 2, .range = { 0, 10 } } } };
    const std::vector<float> weights { .5f };
    const std::vector<glm::mat4> world { glm::mat4 (1) };
    const auto targets = preparePuppetMorphTargets (body, weights, 1, world);
    float maskAlpha = 0;
    CHECK (evaluatePuppetMorph (body, targets, 1, { 0, 0, 0 }, &maskAlpha).w == 1);
    CHECK (maskAlpha == Catch::Approx (.5f));
    CHECK (evaluatePuppetMorph (body, targets, 1, { 5, 0, 0 }, &maskAlpha).w == Catch::Approx (.75f));
    CHECK (maskAlpha == Catch::Approx (.5f));
    CHECK (evaluatePuppetMorph (body, targets, 1, { 10, 0, 0 }, &maskAlpha).w == Catch::Approx (.5f));
    CHECK (maskAlpha == Catch::Approx (.5f));
    CHECK (evaluatePuppetMorph (body, targets, 0, {}, &maskAlpha).w == 1);
    CHECK (maskAlpha == 1);
}
