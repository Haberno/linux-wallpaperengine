#include "WallpaperEngine/Render/Objects/CParticle.h"

#ifdef CHECK
#undef CHECK
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include "WallpaperEngine/Data/Parsers/ObjectParser.h"
#include "WallpaperEngine/FileSystem/Container.h"
#include "WallpaperEngine/Render/Utils/NoiseUtils.h"

using WallpaperEngine::Render::Objects::calculateParticleEmissionRate;
using WallpaperEngine::Render::Objects::calculateParticleAudioResponse;
using WallpaperEngine::Render::Objects::calculateControlPointAttraction;
using WallpaperEngine::Render::Objects::calculateParticleSimulationDelta;
using WallpaperEngine::Render::Objects::calculateRopeTrailVisualValue;
using WallpaperEngine::Render::Objects::convertParticleRotationForRender;
using WallpaperEngine::Render::Objects::resolveParticleControlPoint;
using WallpaperEngine::Render::Objects::calculateFixedParticleOrientation;
using WallpaperEngine::Render::Objects::calculateBillboardParticleOrientation;
using WallpaperEngine::Render::Objects::ParticleInstance;

TEST_CASE ("particle lifetime floors the final scaled sample", "[particle][lifetime]") {
    using WallpaperEngine::Render::Objects::calculateParticleLifetime;
    const struct { float minimum, maximum, exponent, fraction, multiplier, expected; } cases[] = {
        { 0, 0, 1, .5f, 1, .001f },
        { -1, -1, 1, .5f, 1, .001f },
        { .0002f, .0002f, 1, .5f, 1, .001f },
        { .001f, .001f, 1, .5f, 1, .001f },
        { .002f, .002f, 1, .5f, .25f, .001f },
        { .002f, .002f, 1, .5f, 0, .001f },
        { -.002f, -.002f, 1, .5f, -2, .004f },
        { -1, .002f, 0, .5f, 1, .002f },
        { 1, 5, 2, .5f, 2, 4 },
        { 5, 1, 1, .25f, 1, 4 }
    };
    for (const auto& value : cases) {
        CAPTURE (value.minimum, value.maximum, value.exponent, value.multiplier);
        CHECK (calculateParticleLifetime (value.minimum, value.maximum, value.exponent,
                                         value.fraction, value.multiplier)
               == Catch::Approx (value.expected).margin (0.0000001f));
    }
    ParticleInstance particle;
    CHECK (particle.lifetime == 1.0f);
    particle.alive = true;
    particle.lifetime = calculateParticleLifetime (0, 0, 1, .5f, 1);
    particle.age = .0005f;
    CHECK (particle.isAlive ());
    CHECK (particle.getLifetimePos () == Catch::Approx (.5f));
    particle.age = .0015f;
    CHECK_FALSE (particle.isAlive ());
}

TEST_CASE ("spark lifetimes retain the authored native exponent", "[particle][lifetime]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":379,"particle":{"initializer":[
            {"name":"lifetimerandom","min":1,"max":5},
            {"name":"lifetimerandom","min":1,"max":5,"exponent":0},
            {"name":"lifetimerandom","min":1,"max":5,"exponent":10},
            {"name":"lifetimerandom","min":1,"max":5,"exponent":{"value":4}}
        ]}})"), project
    );
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->initializers.size () == 4);
    const float exponents[] = { 1, 0, 10, 4 };
    for (size_t i = 0; i < particle->initializers.size (); ++i) {
        const auto* initializer = particle->initializers[i]->as<LifetimeRandomInitializer> ();
        REQUIRE (initializer != nullptr);
        CHECK (initializer->exponent->value->getFloat () == exponents[i]);
    }
}

TEST_CASE ("turbulent velocity uses the native scalar time field", "[particle][noise]") {
    using WallpaperEngine::Render::Utils::simplexNoise1D;
    // Values independently decoded from native 14027b090 and its permutation.
    const struct { float point; float expected; } samples[] = {
        { -1.25f, -.435677737f }, { -1.0f, 0 }, { -.75f, .294581980f },
        { -.5f, -.187470704f }, { -.25f, -.555988610f }, { 0, 0 },
        { .1f, .303085059f }, { .25f, .599402666f }, { .4f, .589547396f },
        { .5f, .437431663f }, { .75f, .010546131f }, { .9f, -.034237221f },
        { 1.0f, 0 }, { 1.25f, .097989067f }, { 255.75f, -.555988610f },
        { 256.25f, .599402666f }, { -256.75f, .294581980f }
    };
    for (const auto& sample : samples) {
        CAPTURE (sample.point);
        CHECK (simplexNoise1D (sample.point) == Catch::Approx (sample.expected).margin (0.000002f));
    }
}

TEST_CASE ("turbulence simplex preserves native corner hashing and rank ties", "[particle][noise]") {
    using WallpaperEngine::Render::Utils::simplexNoise3D;
    CHECK (simplexNoise3D (0, 0, 0) == 0.0f);
    CHECK (simplexNoise3D (.05f, 0, 0) == Catch::Approx (.203925595f).margin (0.000002f));
    // Conventional final-corner hashing produces .680892885 here.
    CHECK (simplexNoise3D (.4f, .4f, .4f) == Catch::Approx (.005308409f).margin (0.000002f));
    // Conventional tie ranking with the native hash produces -.304392606.
    CHECK (simplexNoise3D (.1f, .4f, .4f) == Catch::Approx (-.213692456f).margin (0.000002f));
    CHECK (simplexNoise3D (.4f, .1f, .4f) == Catch::Approx (.412096322f).margin (0.000002f));
    CHECK (simplexNoise3D (.4f, .4f, .1f) == Catch::Approx (.136049703f).margin (0.000002f));
}

TEST_CASE ("turbulence simplex samples all coordinate orders and negative cells", "[particle][noise]") {
    using WallpaperEngine::Render::Utils::simplexNoise3D;
    const struct { glm::vec3 point; float expected; } samples[] = {
        { { .3f, .2f, .1f }, .747236490f }, { { .3f, .1f, .2f }, .632986784f },
        { { .2f, .3f, .1f }, .686017990f }, { { .1f, .3f, .2f }, .512414038f },
        { { .2f, .1f, .3f }, .459884644f }, { { .1f, .2f, .3f }, .399469018f },
        { { -.3f, -.2f, -.1f }, -.496379316f }, { { -.7f, .25f, 1.1f }, -.915374756f },
        { { -1, -2, -3 }, 0 }, { { 12.125f, -7.25f, 3.75f }, .520894349f },
        { { 1.3f, 2.1f, 3.2f }, .628044486f }, { { 256.1f, .2f, -.3f }, .237908944f }
    };
    for (const auto& sample : samples) {
        CAPTURE (sample.point.x, sample.point.y, sample.point.z);
        CHECK (simplexNoise3D (sample.point.x, sample.point.y, sample.point.z)
               == Catch::Approx (sample.expected).margin (0.000002f));
    }
}

TEST_CASE ("turbulence parses native lifetime defaults and authored ranges", "[particle][turbulence]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":23,"particle":{"operator":[
            {"name":"turbulence"},
            {"name":"turbulence","blendinstart":0.2,"blendinend":0.4,"blendoutstart":0.6,"blendoutend":0.8}
        ]}})"), project
    );
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 2);
    const auto* defaults = particle->operators[0]->as<TurbulenceOperator> ();
    const auto* authored = particle->operators[1]->as<TurbulenceOperator> ();
    REQUIRE (defaults != nullptr);
    REQUIRE (authored != nullptr);
    CHECK (defaults->blendTimes == glm::vec4 (0, 0, 1, 1));
    CHECK (authored->blendTimes == glm::vec4 (.2f, .4f, .6f, .8f));
}

TEST_CASE ("turbulence keeps absent projection settings distinct from explicit zero and null", "[particle][turbulence]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":24,"particle":{"operator":[
            {"name":"turbulence"},
            {"name":"turbulence","scale":0,"timescale":0,"speedmin":0,"speedmax":0,"mask":"0 0 0"},
            {"name":"turbulence","scale":null,"timescale":null,"speedmin":null,"speedmax":null,"mask":null},
            {"name":"turbulence","scale":{"value":-0.25},"timescale":2,"speedmin":-3,"speedmax":4,"mask":"1 -2 3"}
        ]}})"), project
    );
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 4);
    const auto* absent = particle->operators[0]->as<TurbulenceOperator> ();
    REQUIRE (absent != nullptr);
    CHECK (absent->scale == nullptr);
    CHECK (absent->timeScale == nullptr);
    CHECK (absent->speedMin == nullptr);
    CHECK (absent->speedMax == nullptr);
    CHECK (absent->mask == nullptr);
    for (size_t i = 1; i <= 3; ++i) {
        const auto* op = particle->operators[i]->as<TurbulenceOperator> ();
        REQUIRE (op != nullptr);
        REQUIRE (op->scale != nullptr);
        REQUIRE (op->timeScale != nullptr);
        REQUIRE (op->speedMin != nullptr);
        REQUIRE (op->speedMax != nullptr);
        REQUIRE (op->mask != nullptr);
        CHECK (op->scale->value->getFloat () == (i == 3 ? -.25f : 0.0f));
        CHECK (op->timeScale->value->getFloat () == (i == 3 ? 2.0f : 0.0f));
        CHECK (op->speedMin->value->getFloat () == (i == 3 ? -3.0f : 0.0f));
        CHECK (op->speedMax->value->getFloat () == (i == 3 ? 4.0f : 0.0f));
        CHECK (op->mask->value->getVec3 () == (i == 3 ? glm::vec3 (1, -2, 3) : glm::vec3 (0)));
    }
}

TEST_CASE ("turbulence audio distinguishes missing defaults from null and authored values", "[particle][turbulence]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":25,"particle":{"operator":[
            {"name":"turbulence"},
            {"name":"turbulence","audioprocessingmode":null,"audioprocessingbounds":null,
             "audioprocessingexponent":null,"audioprocessingfrequencystart":null,"audioprocessingfrequencyend":null},
            {"name":"turbulence","audioprocessingmode":3,"audioprocessingbounds":"1 0.8",
             "audioprocessingexponent":0.5,"audioprocessingfrequencystart":-1,"audioprocessingfrequencyend":99}
        ]}})"), project
    );
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 3);
    for (size_t i = 0; i < 3; ++i) {
        const auto* op = particle->operators[i]->as<TurbulenceOperator> ();
        REQUIRE (op != nullptr);
        CHECK (op->audioProcessingMode->value->getInt () == (i == 2 ? 3 : 0));
        CHECK (op->audioProcessingBounds->value->getVec2 ()
               == (i == 0 ? glm::vec2 (.8f, 1) : i == 1 ? glm::vec2 (0) : glm::vec2 (1, .8f)));
        CHECK (op->audioProcessingExponent->value->getFloat () == (i == 0 ? 2.0f : i == 1 ? 0.0f : .5f));
        CHECK (op->audioProcessingFrequencyStart->value->getInt () == (i == 2 ? -1 : 0));
        CHECK (op->audioProcessingFrequencyEnd->value->getInt () == (i == 0 ? 1 : i == 1 ? 0 : 99));
    }
}

TEST_CASE ("angular movement parses native lifetime defaults and authored ranges", "[particle][angularmovement]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":22,"particle":{"operator":[
            {"name":"angularmovement"},
            {"name":"angularmovement","force":"40 80 -120","drag":10,
             "blendinstart":0.2,"blendinend":0.4,"blendoutstart":0.6,"blendoutend":0.8}
        ]}})"), project
    );
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 2);
    const auto* defaults = particle->operators[0]->as<AngularMovementOperator> ();
    const auto* authored = particle->operators[1]->as<AngularMovementOperator> ();
    REQUIRE (defaults != nullptr);
    REQUIRE (authored != nullptr);
    CHECK (defaults->force->value->getVec3 () == glm::vec3 (0));
    CHECK (defaults->drag->value->getFloat () == 0.0f);
    CHECK (defaults->blendTimes == glm::vec4 (0, 0, 1, 1));
    CHECK (authored->force->value->getVec3 () == glm::vec3 (40, 80, -120));
    CHECK (authored->drag->value->getFloat () == 10.0f);
    CHECK (authored->blendTimes == glm::vec4 (.2f, .4f, .6f, .8f));
}

TEST_CASE ("angular force precedes rotation while drag uses its separate clock", "[particle][angularmovement]") {
    using WallpaperEngine::Render::Objects::applyParticleAngularMovement;
    ParticleInstance particle;
    particle.rotation = {.25f, -.5f, 1};
    particle.angularVelocity = {2, -4, 6};
    applyParticleAngularMovement (particle, {40, 80, -120}, 10, .1f, .05f, .5f);
    CHECK (particle.rotation.x == Catch::Approx (.45f));
    CHECK (particle.rotation.y == Catch::Approx (-.5f));
    CHECK (particle.rotation.z == Catch::Approx (1.0f));
    CHECK (particle.angularVelocity == glm::vec3 (3, 0, 0));
    particle.angularVelocity = {2, -4, 6};
    applyParticleAngularMovement (particle, {40, 80, -120}, 10, .1f, 0, .5f);
    CHECK (particle.angularVelocity == glm::vec3 (4, 0, 0));
    CHECK (particle.rotation.x == Catch::Approx (.65f));
}

TEST_CASE ("angular drag caps before blending and preserves native residual velocity", "[particle][angularmovement]") {
    using WallpaperEngine::Render::Objects::applyParticleAngularMovement;
    ParticleInstance particle;
    particle.angularVelocity.z = 8;
    applyParticleAngularMovement (particle, {}, 100, .05f, .05f, .5f);
    CHECK (particle.rotation.z == Catch::Approx (.2f));
    CHECK (particle.angularVelocity.z == Catch::Approx (4.00000048f).epsilon (0).margin (.0000001f));
    particle.angularVelocity.z = 8;
    applyParticleAngularMovement (particle, {}, -100, .05f, .05f, .5f);
    CHECK (particle.angularVelocity.z == Catch::Approx (28.0f));
}

TEST_CASE ("angular pause and inactive envelopes preserve unwrapped state", "[particle][angularmovement]") {
    using WallpaperEngine::Render::Objects::applyParticleAngularMovement;
    ParticleInstance particle;
    particle.rotation = {7, -8, 9};
    particle.angularVelocity = {2, -4, 6};
    applyParticleAngularMovement (particle, {40, 80, -120}, 10, .1f, .05f, 0);
    CHECK (particle.rotation == glm::vec3 (7, -8, 9));
    CHECK (particle.angularVelocity == glm::vec3 (2, -4, 6));
    applyParticleAngularMovement (particle, {40, 80, -120}, 10, 0, 0, 1);
    CHECK (particle.rotation == glm::vec3 (7, -8, 9));
    CHECK (particle.angularVelocity == glm::vec3 (2, -4, 6));
}

TEST_CASE ("vortex v2 preserves its own blending and projection defaults", "[particle][vortex]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":21,"particle":{"operator":[
            {"name":"vortex_v2"}, {"name":"vortex"},
            {"name":"vortex_v2","controlpoint":-1,"flags":259,"speedinner":20,"audioprocessingexponent":3,
             "audioprocessingfrequencystart":8,"audioprocessingfrequencyend":4,
             "blendinstart":0.2,"blendinend":0.4,"blendoutstart":0.6,"blendoutend":0.8}
        ]}})"), project
    );
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 3);
    const auto* defaults = particle->operators[0]->as<VortexOperator> ();
    const auto* legacy = particle->operators[1]->as<VortexOperator> ();
    const auto* authored = particle->operators[2]->as<VortexOperator> ();
    REQUIRE (defaults != nullptr);
    REQUIRE (legacy != nullptr);
    REQUIRE (authored != nullptr);
    CHECK (defaults->nativeV2);
    CHECK_FALSE (legacy->nativeV2);
    CHECK (defaults->distanceInner == nullptr);
    CHECK (defaults->distanceOuter == nullptr);
    CHECK (defaults->speedInner == nullptr);
    CHECK (defaults->ringRadius == nullptr);
    CHECK (defaults->ringWidth == nullptr);
    CHECK (defaults->ringPullDistance == nullptr);
    CHECK (defaults->ringPullForce == nullptr);
    CHECK (defaults->blendTimes == glm::vec4 (0, 0, 1, 1));
    CHECK (defaults->audioProcessingBounds->value->getVec2 () == glm::vec2 (.8f, 1));
    CHECK (defaults->audioProcessingExponent->value->getFloat () == 2.0f);
    CHECK (defaults->audioProcessingFrequencyStart->value->getInt () == 0);
    CHECK (defaults->audioProcessingFrequencyEnd->value->getInt () == 1);
    CHECK (authored->audioProcessingExponent->value->getFloat () == 3.0f);
    CHECK (authored->audioProcessingFrequencyStart->value->getInt () == 8);
    CHECK (authored->audioProcessingFrequencyEnd->value->getInt () == 4);
    CHECK (legacy->audioProcessingBounds->value->getVec2 () == glm::vec2 (.8f, 1));
    CHECK (legacy->audioProcessingExponent->value->getFloat () == 2.0f);
    CHECK (legacy->audioProcessingFrequencyStart->value->getInt () == 0);
    CHECK (legacy->audioProcessingFrequencyEnd->value->getInt () == 1);
    CHECK (legacy->distanceInner == nullptr);
    CHECK (authored->controlPoint == 7);
    CHECK (authored->flags == 3);
    CHECK (authored->speedInner->value->getFloat () == 20.0f);
    CHECK (authored->blendTimes == glm::vec4 (.2f, .4f, .6f, .8f));
}

TEST_CASE ("vortex audio distinguishes missing defaults from explicit null", "[particle][vortex][vortex-audio]") {
    for (const auto* name : { "vortex", "vortex_v2" }) {
        WallpaperEngine::Data::Model::Project project {};
        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":392,"particle":{"operator":[
            {}, {"audioprocessingmode":null,"audioprocessingbounds":null,
                 "audioprocessingexponent":null,"audioprocessingfrequencystart":null,"audioprocessingfrequencyend":null},
            {"audioprocessingmode":3,"audioprocessingbounds":"1 0.8",
             "audioprocessingexponent":0.5,"audioprocessingfrequencystart":-1,"audioprocessingfrequencyend":99}
        ]}})");
        for (auto& node : data["particle"]["operator"]) node["name"] = name;
        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
        const auto* particle = object->as<Particle> ();
        REQUIRE (particle != nullptr);
        REQUIRE (particle->operators.size () == 3);
        for (size_t i = 0; i < 3; ++i) {
            const auto* op = particle->operators[i]->as<VortexOperator> ();
            REQUIRE (op != nullptr);
            CHECK (op->audioProcessingMode->value->getInt () == (i == 2 ? 3 : 0));
            CHECK (op->audioProcessingBounds->value->getVec2 ()
                   == (i == 0 ? glm::vec2 (.8f, 1) : i == 1 ? glm::vec2 (0) : glm::vec2 (1, .8f)));
            CHECK (op->audioProcessingExponent->value->getFloat () == (i == 0 ? 2.0f : i == 1 ? 0.0f : .5f));
            CHECK (op->audioProcessingFrequencyStart->value->getInt () == (i == 2 ? -1 : 0));
            CHECK (op->audioProcessingFrequencyEnd->value->getInt () == (i == 0 ? 1 : i == 1 ? 0 : 99));
        }
    }
}

TEST_CASE ("legacy vortex preserves native radial attenuation and ignores V2-only flags", "[particle][vortex][vortex-legacy]") {
    using WallpaperEngine::Render::Objects::calculateParticleLegacyVortex;
    const glm::vec3 position (30, 0, 40), axis (0, 0, 1);
    for (const int flags : { 0, 2, 4, 6 }) {
        const auto velocity = calculateParticleLegacyVortex (position, axis, flags, { 100, 200 }, { 400, 0 }, .025f);
        CHECK (velocity.x == Catch::Approx (0));
        CHECK (velocity.y == Catch::Approx (6));
        CHECK (velocity.z == Catch::Approx (0));
    }
    for (const int flags : { 1, 3, 5, 7 }) {
        CHECK (calculateParticleLegacyVortex (position, axis, flags, { 100, 200 }, { 400, 0 }, .025f).y
               == Catch::Approx (10));
    }
    CHECK (calculateParticleLegacyVortex ({ 0, 0, 40 }, axis, 0, { 100, 200 }, { 400, 0 }, .025f)
           == glm::vec3 (0));
    CHECK (calculateParticleLegacyVortex ({ 40, 0, 0 }, { 0, -1, 0 }, 0, { 100, 200 }, { 400, 0 }, .025f).z
           == Catch::Approx (10));
}

TEST_CASE ("legacy vortex falloff keeps signed and equal native ranges", "[particle][vortex][vortex-legacy]") {
    using WallpaperEngine::Render::Objects::calculateParticleLegacyVortex;
    const struct { float distance, minimum, maximum, expected; } cases[] = {
        { .5f, .25f, .75f, 5 }, { 40, 60, 20, 5 }, { 10, 60, 20, 0 }, { 70, 60, 20, 10 },
        { 40, 40, 40, 10 }, { 40.5f, 40, 40, 5 }, { 41, 40, 40, 0 }
    };
    for (const auto& entry : cases) {
        INFO ("distance=" << entry.distance << ", minimum=" << entry.minimum << ", maximum=" << entry.maximum);
        CHECK (calculateParticleLegacyVortex ({ entry.distance, 0, 0 }, { 0, 0, 1 }, 0,
               { entry.minimum, entry.maximum }, { 400, 0 }, .025f).y == Catch::Approx (entry.expected));
    }
    CHECK (calculateParticleLegacyVortex ({ 40, 0, 0 }, { 0, 0, 1 }, 0, { 100, 200 }, { -400, 0 }, .0125f).y
           == Catch::Approx (-5));
}

TEST_CASE ("legacy vortex preserves missing projection defaults and native point bounds", "[particle][vortex][vortex-legacy]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":393,"particle":{"operator":[
            {"name":"vortex"},
            {"name":"vortex","distanceinner":null,"distanceouter":null,"speedinner":null,"controlpoint":-1},
            {"name":"vortex","distanceinner":-2,"distanceouter":3,"speedinner":-4,"controlpoint":99}
        ]}})"), project);
    const auto* definition = object->as<Particle> ();
    REQUIRE (definition != nullptr);
    REQUIRE (definition->operators.size () == 3);
    const auto* omitted = definition->operators[0]->as<VortexOperator> ();
    const auto* null = definition->operators[1]->as<VortexOperator> ();
    const auto* authored = definition->operators[2]->as<VortexOperator> ();
    REQUIRE (omitted != nullptr);
    REQUIRE (null != nullptr);
    REQUIRE (authored != nullptr);
    CHECK (omitted->distanceInner == nullptr);
    CHECK (omitted->distanceOuter == nullptr);
    CHECK (omitted->speedInner == nullptr);
    REQUIRE (null->distanceInner != nullptr);
    REQUIRE (null->distanceOuter != nullptr);
    REQUIRE (null->speedInner != nullptr);
    CHECK (null->distanceInner->value->getFloat () == 0);
    CHECK (null->distanceOuter->value->getFloat () == 0);
    CHECK (null->speedInner->value->getFloat () == 0);
    CHECK (null->controlPoint == 7);
    CHECK (authored->controlPoint == 7);
    CHECK (authored->distanceInner->value->getFloat () == -2);
    CHECK (authored->distanceOuter->value->getFloat () == 3);
    CHECK (authored->speedInner->value->getFloat () == -4);
}

TEST_CASE ("vortex v2 weights spin and predicted radius together", "[particle][vortex]") {
    using WallpaperEngine::Render::Objects::calculateParticleVortexV2;
    const auto increment = calculateParticleVortexV2 (
        {10, 0, 0}, {0, -10, 0}, {0, 0, 1}, 2, {0, 20}, {20, 20}, 1, {}, .1f, .1f, .5f
    );
    CHECK (increment.x == Catch::Approx (-.24814049f).margin (.00002f));
    CHECK (increment.y == Catch::Approx (1.02481405f).margin (.00002f));
    CHECK (increment.z == 0.0f);
    const auto slowerForce = calculateParticleVortexV2 (
        {10, 0, 0}, {0, -10, 0}, {0, 0, 1}, 2, {0, 20}, {20, 20}, 1, {}, .1f, .05f, .5f
    );
    CHECK (slowerForce.x == increment.x);
    CHECK (slowerForce.y == Catch::Approx (increment.y - .5f));
    CHECK (calculateParticleVortexV2 ({}, {}, {0, 0, 1}, 2, {0, 20}, {20, 20}, 1, {}, .1f, .1f, 1)
           == glm::vec3 (0));
    CHECK (calculateParticleVortexV2 ({10, 0, 0}, {}, {0, 0, 1}, 2, {0, 20}, {20, 20}, 1, {}, 0, 0, 1)
           == glm::vec3 (0));
}

TEST_CASE ("attraction parses lifetime blending and native point selection", "[particle][attraction]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":20,"particle":{"operator":[
            {"name":"controlpointattract"},
            {"name":"controlpointattract","controlpoint":-1,"flags":0,"scale":-200,"threshold":100,
             "blendinstart":0.2,"blendinend":0.4,"blendoutstart":0.6,"blendoutend":0.8}
        ]}})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 2);
    const auto* defaults = particle->operators[0]->as<ControlPointAttractOperator> ();
    const auto* authored = particle->operators[1]->as<ControlPointAttractOperator> ();
    REQUIRE (defaults != nullptr);
    REQUIRE (authored != nullptr);
    CHECK (defaults->controlPoint == 0);
    CHECK (defaults->flags == 2);
    CHECK (defaults->scale == nullptr);
    CHECK (defaults->threshold == nullptr);
    CHECK (defaults->blendTimes == glm::vec4 (0, 0, 1, 1));
    CHECK (authored->controlPoint == 7);
    CHECK (authored->flags == 0);
    CHECK (authored->scale->value->getFloat () == -200.0f);
    CHECK (authored->threshold->value->getFloat () == 100.0f);
    CHECK (authored->blendTimes == glm::vec4 (.2f, .4f, .6f, .8f));
}

TEST_CASE ("attraction deletion uses swept distance and marks age without removing the slot", "[particle][attraction][deletion]") {
    using WallpaperEngine::Render::Objects::applyParticleAttractionDeletion;
    const struct { glm::vec3 previous, position, center; float radius; bool deleted; } cases[] = {
        {{-10,0,0}, {10,0,0}, {}, 1, true},
        {{-10,2,0}, {10,2,0}, {}, 1, false},
        {{-10,0,2}, {10,0,2}, {}, 1, false},
        {{}, {}, {}, 0, true},
        {{}, {}, {}, -1, true},
        {{1,0,0}, {1,0,0}, {}, 1, true},
        {{1.01f,0,0}, {1.01f,0,0}, {}, 1, false},
        {{-10,0,0}, {10,0,0}, {}, -1, true},
        {{-10,0,0}, {-5,0,0}, {}, 1, false},
        {{5,0,0}, {10,0,0}, {}, 1, false},
        {{-10,-20,-30}, {10,20,30}, {}, 1, true},
        {{}, {20,0,0}, {10,2,0}, 1.9f, false},
        {{}, {20,0,0}, {10,2,0}, 2.1f, true},
        {{12,3,4}, {12,3,4}, {10,3,4}, 2, true}
    };
    for (const auto& value : cases) {
        ParticleInstance particle;
        particle.previousPosition = value.previous;
        particle.position = value.position;
        particle.age = .25f;
        particle.lifetime = 2.0f;
        particle.alive = true;
        applyParticleAttractionDeletion (particle, value.center, value.radius);
        CHECK (particle.age == (value.deleted ? 2.0f : .25f));
        CHECK (particle.alive); // Later authored operators still see the slot.
    }
}

TEST_CASE ("attraction deletion preserves omitted defaults and explicit zero or null", "[particle][attraction][deletion]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":20,"particle":{"operator":[
            {"name":"controlpointattract","flags":1},
            {"name":"controlpointattract","flags":3,"deletethreshold":-2},
            {"name":"controlpointattract","scale":null,"threshold":null,"deletethreshold":null},
            {"name":"controlpointattract","deletethreshold":0}
        ]}})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 4);
    const auto* omitted = particle->operators[0]->as<ControlPointAttractOperator> ();
    const auto* authored = particle->operators[1]->as<ControlPointAttractOperator> ();
    const auto* nulls = particle->operators[2]->as<ControlPointAttractOperator> ();
    const auto* zero = particle->operators[3]->as<ControlPointAttractOperator> ();
    REQUIRE (omitted != nullptr);
    REQUIRE (authored != nullptr);
    REQUIRE (nulls != nullptr);
    REQUIRE (zero != nullptr);
    CHECK (omitted->deleteThreshold == nullptr);
    CHECK (omitted->scale == nullptr);
    CHECK (omitted->threshold == nullptr);
    CHECK (omitted->flags == 1);
    CHECK (authored->flags == 3);
    CHECK (authored->deleteThreshold->value->getFloat () == -2.0f);
    REQUIRE (nulls->scale != nullptr);
    REQUIRE (nulls->threshold != nullptr);
    REQUIRE (nulls->deleteThreshold != nullptr);
    CHECK (nulls->scale->value->getFloat () == 0.0f);
    CHECK (nulls->threshold->value->getFloat () == 0.0f);
    CHECK (nulls->deleteThreshold->value->getFloat () == 0.0f);
    CHECK (zero->deleteThreshold->value->getFloat () == 0.0f);
}

TEST_CASE ("attraction caps the signed step before lifetime weight", "[particle][attraction]") {
    CHECK (calculateControlPointAttraction ({40, 0, 0}, 5000, 100, .05f, true, .5f).x
           == Catch::Approx (20.0f));
    CHECK (calculateControlPointAttraction ({40, 0, 0}, 5000, 100, .05f, false, .5f).x
           == Catch::Approx (75.0f));
    CHECK (calculateControlPointAttraction ({40, 0, 0}, -5000, 100, .05f, true, .5f).x
           == Catch::Approx (-75.0f));
    CHECK (calculateControlPointAttraction ({40, 0, 0}, 5000, 100, .05f, true, 0) == glm::vec3 (0));
    CHECK (calculateControlPointAttraction ({40, 0, 0}, 5000, 100, 0) == glm::vec3 (0));
    CHECK (calculateControlPointAttraction ({.0001f, 0, 0}, 100, 1, .05f).x
           == Catch::Approx (.0001f).margin (.00000001f));
}

TEST_CASE ("position oscillation preserves native defaults and lifetime blending", "[particle][oscillateposition]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":{"operator":[
            {"name":"oscillateposition"},
            {"name":"oscillateposition","scalemax":20,"blendinstart":0.2,"blendinend":0.4,
             "blendoutstart":0.6,"blendoutend":0.8}
        ]}})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 2);
    const auto* defaults = particle->operators[0]->as<OscillatePositionOperator> ();
    const auto* authored = particle->operators[1]->as<OscillatePositionOperator> ();
    REQUIRE (defaults != nullptr);
    REQUIRE (authored != nullptr);
    CHECK (defaults->frequencyMin->value->getFloat () == 1.0f);
    CHECK (defaults->frequencyMax->value->getFloat () == 5.0f);
    CHECK (defaults->scaleMin->value->getFloat () == 0.0f);
    CHECK (defaults->scaleMax == nullptr); // Resolve the projection-dependent default at runtime.
    CHECK (defaults->phaseMin->value->getFloat () == 0.0f);
    CHECK (defaults->phaseMax->value->getFloat () == Catch::Approx (glm::two_pi<float> ()));
    CHECK (defaults->mask->value->getVec3 () == glm::vec3 (1, 1, 0));
    CHECK (defaults->blendTimes == glm::vec4 (0, 0, 1, 1));
    CHECK (authored->scaleMax->value->getFloat () == 20.0f);
    CHECK (authored->blendTimes == glm::vec4 (.2f, .4f, .6f, .8f));
}

TEST_CASE ("position oscillation integrates native waves with a shared random fraction", "[particle][oscillateposition]") {
    using WallpaperEngine::Render::Objects::calculateParticlePositionOscillation;
    ParticleInstance p;
    p.age = .3f;
    p.lifetime = 1;
    p.oscillationRandom = .25f;
    const glm::vec4 fullLifetime (0, 0, 1, 1);
    const auto delta = calculateParticlePositionOscillation (p, {2, 4}, {.1f, .5f}, {20, 60}, {1, 1, 1}, fullLifetime, .05f);
    CHECK (delta.x == Catch::Approx (1.40151076f).margin (.00002f));
    CHECK (delta.y == Catch::Approx (1.46661999f).margin (.00002f));
    CHECK (delta.z == delta.x);
    const auto masked = calculateParticlePositionOscillation (p, {2, 4}, {.1f, .5f}, {20, 60}, {-.5f, 0, 2}, fullLifetime, .05f);
    CHECK (masked.x == Catch::Approx (-.70075538f).margin (.00002f));
    CHECK (masked.y == 0);
    CHECK (masked.z == Catch::Approx (2.80302152f).margin (.00004f));
    CHECK (calculateParticlePositionOscillation (p, {2, 4}, {.1f, .5f}, {20, 60}, {1, 1, 1}, fullLifetime, 0) == glm::vec3 (0));
    CHECK (calculateParticlePositionOscillation (p, {2, 4}, {.1f, .5f}, {20, 60}, {1, 1, 1}, {.4f, .6f, 1, 1}, .05f) == glm::vec3 (0));
    const auto blended = calculateParticlePositionOscillation (p, {2, 4}, {.1f, .5f}, {20, 60}, {1, 1, 1}, {0, .6f, 1, 1}, .05f);
    CHECK (blended.x == Catch::Approx (.70075538f).margin (.00002f));
}

TEST_CASE ("size oscillation retains native defaults and authored blending", "[particle][oscillatesize]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":{"operator":[
            {"name":"oscillatesize"},
            {"name":"oscillatesize","blendinstart":0.2,"blendinend":0.4,
             "blendoutstart":0.6,"blendoutend":0.8}
        ]}})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 2);
    const auto* defaults = particle->operators[0]->as<OscillateSizeOperator> ();
    const auto* authored = particle->operators[1]->as<OscillateSizeOperator> ();
    REQUIRE (defaults != nullptr);
    REQUIRE (authored != nullptr);
    CHECK (defaults->frequencyMin->value->getFloat () == 1.0f);
    CHECK (defaults->frequencyMax->value->getFloat () == 10.0f);
    CHECK (defaults->scaleMin->value->getFloat () == Catch::Approx (.8f));
    CHECK (defaults->scaleMax->value->getFloat () == Catch::Approx (1.2f));
    CHECK (defaults->phaseMin->value->getFloat () == 0.0f);
    CHECK (defaults->phaseMax->value->getFloat () == Catch::Approx (glm::two_pi<float> ()));
    CHECK (defaults->blendTimes == glm::vec4 (0, 0, 1, 1));
    CHECK (authored->blendTimes == glm::vec4 (.2f, .4f, .6f, .8f));
}

TEST_CASE ("alpha oscillation preserves native defaults and lifetime blending", "[particle][oscillatealpha]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":{"operator":[
            {"name":"oscillatealpha"},
            {"name":"oscillatealpha","blendinstart":0.2,"blendinend":0.4,
             "blendoutstart":0.6,"blendoutend":0.8}
        ]}})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 2);
    const auto* defaults = particle->operators[0]->as<OscillateAlphaOperator> ();
    const auto* authored = particle->operators[1]->as<OscillateAlphaOperator> ();
    REQUIRE (defaults != nullptr);
    REQUIRE (authored != nullptr);
    CHECK (defaults->frequencyMin->value->getFloat () == 1.0f);
    CHECK (defaults->frequencyMax->value->getFloat () == 10.0f);
    CHECK (defaults->scaleMin->value->getFloat () == 0.0f);
    CHECK (defaults->scaleMax->value->getFloat () == 1.0f);
    CHECK (defaults->phaseMin->value->getFloat () == 0.0f);
    CHECK (defaults->phaseMax->value->getFloat () == Catch::Approx (glm::two_pi<float> ()));
    CHECK (defaults->blendTimes == glm::vec4 (0, 0, 1, 1));
    CHECK (authored->blendTimes == glm::vec4 (.2f, .4f, .6f, .8f));
}

TEST_CASE ("alpha oscillation shares one random fraction and multiplies the incoming alpha", "[particle][oscillatealpha]") {
    ParticleInstance p;
    p.alpha = .8f;
    p.age = .3f;
    p.lifetime = 1;
    p.oscillationRandom = .25f;
    using WallpaperEngine::Render::Objects::calculateParticleOscillationMultiplier;
    // Frequency 2.5, phase .2, amplitude .2 all use the same fraction.
    CHECK (p.alpha * calculateParticleOscillationMultiplier (p, {2, 4}, {.1f, .5f}, {.2f, 1}, {0, 0, 1, 1}) == Catch::Approx (.31591877f));
    p.oscillationRandom = 0;
    CHECK (p.alpha * calculateParticleOscillationMultiplier (p, {2, 4}, {.1f, .5f}, {.2f, 1}, {0, 0, 1, 1}) == Catch::Approx (.16f));
    p.oscillationRandom = 1;
    CHECK (p.alpha * calculateParticleOscillationMultiplier (p, {2, 4}, {.1f, .5f}, {.2f, 1}, {0, 0, 1, 1}) == Catch::Approx (.46132026f));
}

TEST_CASE ("particles retain the fractal position-offset initializer", "[particle][positionoffset]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":{
            "initializer":[{"name":"positionoffsetrandom","distance":150}]
        }})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->initializers.size () == 1);
}

TEST_CASE ("position-offset noise keeps signed simplex gradients", "[particle][positionoffset]") {
    using WallpaperEngine::Render::Utils::simplexNoise2D;
    CHECK (simplexNoise2D (0.0f, 0.0f) == 0.0f);
    CHECK (simplexNoise2D (0.2f, 0.0f) == Catch::Approx (0.8188545f).margin (0.00001f));
    CHECK (simplexNoise2D (0.0f, 0.3f) == Catch::Approx (-0.3810688f).margin (0.00001f));
    CHECK (simplexNoise2D (-0.2f, 0.0f) == Catch::Approx (-0.8188726f).margin (0.00001f));
    CHECK (simplexNoise2D (0.0f, -0.3f) == Catch::Approx (0.3794413f).margin (0.00001f));
    CHECK (simplexNoise2D (1.25f, -2.75f) == Catch::Approx (-0.9296935f).margin (0.00001f));
    CHECK (simplexNoise2D (0.125f, 0.5f) == Catch::Approx (-0.4931363f).margin (0.00001f));
}

TEST_CASE ("control-point sentinels retain float values without integer overflow", "[particle][controlpoint]") {
    using WallpaperEngine::Data::Model::DynamicValue;
    const float sentinel = std::numeric_limits<float>::max ();
    DynamicValue value (glm::vec3 (sentinel, 20.0f, 10.0f));
    CHECK (value.getVec3 () == glm::vec3 (sentinel, 20.0f, 10.0f));
    CHECK (value.getInt () == std::numeric_limits<int>::max ());
    value.update (glm::vec3 (-sentinel), DynamicValue::UpdateSource::User);
    CHECK (value.getInt () == std::numeric_limits<int>::min ());
    value.update (glm::vec3 (sentinel), DynamicValue::UpdateSource::Script);
    CHECK (value.getInt () == std::numeric_limits<int>::max ());
    value.update (glm::vec2 (sentinel), DynamicValue::UpdateSource::User);
    CHECK (value.getInt () == std::numeric_limits<int>::max ());
    value.update (glm::vec4 (sentinel), DynamicValue::UpdateSource::User);
    CHECK (value.getInt () == std::numeric_limits<int>::max ());
    value.update (sentinel, DynamicValue::UpdateSource::User);
    CHECK (value.getInt () == std::numeric_limits<int>::max ());
    value.update (0.75f, DynamicValue::UpdateSource::User);
    CHECK (value.getInt () == 0);
    CHECK_FALSE (value.getBool ());
    value.update (-42.9f, DynamicValue::UpdateSource::User);
    CHECK (value.getInt () == -42);
}

TEST_CASE ("particles retain the control-point velocity initializer", "[particle][inheritvelocity]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":{
            "initializer":[{"name":"inheritcontrolpointvelocity","controlpoint":1,"min":0.3,"max":1}]
        }})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->initializers.size () == 1);
    REQUIRE (particle->initializers.front () != nullptr);
    const auto* initializer = particle->initializers.front ()->as<InheritControlPointVelocityInitializer> ();
    REQUIRE (initializer != nullptr);
    CHECK (initializer->controlPoint == 1);
    CHECK (initializer->min->value->getFloat () == Catch::Approx (0.3f));
    CHECK (initializer->max->value->getFloat () == 1.0f);
}

TEST_CASE ("control-point velocity uses native default fractions", "[particle][inheritvelocity]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":{
            "initializer":[{"name":"inheritcontrolpointvelocity"}]
        }})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->initializers.size () == 1);
    const auto* initializer = particle->initializers.front ()->as<InheritControlPointVelocityInitializer> ();
    REQUIRE (initializer != nullptr);
    CHECK (initializer->controlPoint == 0);
    CHECK (initializer->min->value->getFloat () == Catch::Approx (0.1f));
    CHECK (initializer->max->value->getFloat () == Catch::Approx (0.2f));
}

TEST_CASE ("control-point velocity follows frame displacement without startup impulses", "[particle][inheritvelocity]") {
    WallpaperEngine::Render::Objects::ControlPointData point;
    point.sampleVelocity ({ 100.0f, 50.0f, -20.0f }, 0.1f);
    CHECK (point.velocity == glm::vec3 (0.0f));
    point.sampleVelocity ({ 110.0f, 30.0f, -15.0f }, 0.5f);
    CHECK (point.velocity == glm::vec3 (20.0f, -40.0f, 10.0f));
    point.sampleVelocity ({ 108.0f, 34.0f, -16.0f }, 0.1f);
    CHECK (point.velocity == glm::vec3 (-20.0f, 40.0f, -10.0f));
    point.sampleVelocity ({ 108.0f, 34.0f, -16.0f }, 0.1f);
    CHECK (point.velocity == glm::vec3 (0.0f));
    point.sampleVelocity ({ 0.0f, 0.0f, 0.0f }, 0.0f);
    CHECK (point.velocity == glm::vec3 (0.0f));
    point.sampleVelocity ({ 1.0f, 0.0f, 0.0f }, 0.25f);
    CHECK (point.velocity == glm::vec3 (4.0f, 0.0f, 0.0f));
}

TEST_CASE ("particles retain the authored control-point motion reduction operator", "[particle][motioncontrolpoint]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":{"operator":[{
            "name":"reducemovementnearcontrolpoint","controlpoint":1,
            "distanceinner":50,"distanceouter":150,"reductioninner":10,"reductionouter":0
        }]}})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 1);
    const auto* op = particle->operators.front ()->as<ReduceMovementNearControlPointOperator> ();
    REQUIRE (op != nullptr);
    CHECK (op->controlPoint == 1);
    CHECK (op->distanceInner->value->getFloat () == 50.0f);
    CHECK (op->distanceOuter->value->getFloat () == 150.0f);
    CHECK (op->reductionInner->value->getFloat () == 10.0f);
    CHECK (op->reductionOuter->value->getFloat () == 0.0f);
}

TEST_CASE ("control-point motion reduction preserves native defaults and index bounds", "[particle][motioncontrolpoint]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":{"operator":[
            {"name":"reducemovementnearcontrolpoint"},
            {"name":"reducemovementnearcontrolpoint","controlpoint":-1},
            {"name":"reducemovementnearcontrolpoint","controlpoint":2147483648}
        ]}})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle->operators.size () == 3);
    const auto* op = particle->operators.front ()->as<ReduceMovementNearControlPointOperator> ();
    REQUIRE (op != nullptr);
    CHECK (op->controlPoint == 0);
    CHECK (op->distanceInner == nullptr);
    CHECK (op->distanceOuter == nullptr);
    CHECK (op->reductionInner->value->getFloat () == 100.0f);
    CHECK (op->reductionOuter->value->getFloat () == 0.0f);
    CHECK (op->blendTimes == glm::vec4 (0.0f, 0.0f, 1.0f, 1.0f));
    CHECK (particle->operators[1]->as<ReduceMovementNearControlPointOperator> ()->controlPoint == 7);
    CHECK (particle->operators[2]->as<ReduceMovementNearControlPointOperator> ()->controlPoint == 7);
}

TEST_CASE ("control-point motion reduction follows native damping and endpoint rules", "[particle][motioncontrolpoint]") {
    using WallpaperEngine::Render::Objects::calculateParticleMovementReduction;
    const glm::vec3 velocity (40.0f, -20.0f, 10.0f);
    const glm::vec4 fullLifetime (0.0f, 0.0f, 1.0f, 1.0f);
    CHECK (calculateParticleMovementReduction (velocity, 40, {50, 150}, {10, 0}, .05f, 0, fullLifetime)
           == glm::vec3 (20, -10, 5));
    CHECK (calculateParticleMovementReduction (velocity, 100, {50, 150}, {10, 0}, .05f, 0, fullLifetime)
           == glm::vec3 (30, -15, 7.5f));
    CHECK (calculateParticleMovementReduction (velocity, 200, {50, 150}, {10, 0}, .05f, 0, fullLifetime) == velocity);
    CHECK (calculateParticleMovementReduction (velocity, 40, {50, 150}, {-10, 0}, .05f, 0, fullLifetime) == velocity);
    CHECK (calculateParticleMovementReduction (velocity, 40, {50, 150}, {1000, 0}, .05f, 0, fullLifetime)
           == glm::vec3 (0));
    CHECK (calculateParticleMovementReduction (velocity, 0, {50, 150}, {10, 0}, .05f, 0, fullLifetime) == velocity);
    CHECK (calculateParticleMovementReduction (velocity, 50.5f, {50, 50}, {10, 0}, .05f, 0, fullLifetime)
           == glm::vec3 (30, -15, 7.5f));
    CHECK (calculateParticleMovementReduction (velocity, 200, {50, 150}, {5, 5}, .05f, 0, fullLifetime)
           == glm::vec3 (28, -14, 7));
    CHECK (calculateParticleMovementReduction (velocity, 200, {50, 150}, {0, 0}, .05f, 0, fullLifetime)
           == glm::vec3 (38, -19, 9.5f));
    CHECK (calculateParticleMovementReduction (velocity, 200, {50, 150}, {-.5f, -.5f}, .05f, 0, fullLifetime)
           == glm::vec3 (39, -19.5f, 9.75f));
    CHECK (calculateParticleMovementReduction (velocity, 100, {150, 50}, {10, 0}, .05f, 0, fullLifetime)
           == glm::vec3 (30, -15, 7.5f));
    CHECK (calculateParticleMovementReduction (velocity, 40, {50, 150}, {1000, 0}, .05f, .25f, {0, .5f, 1, 1})
           == glm::vec3 (20, -10, 5));
    CHECK (calculateParticleMovementReduction (velocity, 40, {50, 150}, {1000, 0}, .05f, .75f, {0, 0, .5f, 1})
           == glm::vec3 (20, -10, 5));
}

TEST_CASE ("stock cap velocity particles retain their speed limit operator", "[particle][capvelocity]") {
    auto filesystem = std::make_unique<WallpaperEngine::FileSystem::Container> ();
    // Installed particleelementpreviews/capvelocity uses this delayed speed cap.
    filesystem->getVFS ().add ("particles/capvelocity.json", R"({
        "operator":[{"name":"capvelocity","maxspeed":100,"blendinstart":0.5,"blendinend":0.6}]
    })");
    WallpaperEngine::Data::Model::Project project {};
    project.assetLocator = std::make_unique<WallpaperEngine::Assets::AssetLocator> (std::move (filesystem));
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":"particles/capvelocity.json"})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 1);
    REQUIRE (particle->operators.front () != nullptr);
    const auto* cap = particle->operators.front ()->as<CapVelocityOperator> ();
    REQUIRE (cap != nullptr);
    REQUIRE (cap->maxSpeed != nullptr);
    CHECK (cap->maxSpeed->value->getFloat () == 100.0f);
    CHECK (cap->blendTimes == glm::vec4 (0.5f, 0.6f, 1.0f, 1.0f));
}

TEST_CASE ("omitted cap velocity settings retain scene defaults and a full lifetime envelope", "[particle][capvelocity]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":{"operator":[{"name":"capvelocity"}]}})"),
        project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 1);
    const auto* cap = particle->operators.front ()->as<CapVelocityOperator> ();
    REQUIRE (cap != nullptr);
    CHECK (cap->maxSpeed == nullptr);
    CHECK (cap->blendTimes == glm::vec4 (0.0f, 0.0f, 1.0f, 1.0f));
}

TEST_CASE ("particle velocity caps preserve direction and do not accelerate slow particles", "[particle][capvelocity]") {
    using WallpaperEngine::Render::Objects::calculateParticleVelocityCap;
    const glm::vec4 fullLifetime (0.0f, 0.0f, 1.0f, 1.0f);
    CHECK (calculateParticleVelocityCap ({ 0.0f, 120.0f, -160.0f }, 100.0f, 0.0f, fullLifetime)
	   == glm::vec3 (0.0f, 60.0f, -80.0f));
    CHECK (calculateParticleVelocityCap ({ 30.0f, 40.0f, 0.0f }, 100.0f, 0.5f, fullLifetime)
	   == glm::vec3 (30.0f, 40.0f, 0.0f));
    CHECK (calculateParticleVelocityCap ({ 30.0f, 40.0f, 0.0f }, 0.0f, 0.5f, fullLifetime)
	   == glm::vec3 (0.0f));
    CHECK (calculateParticleVelocityCap (glm::vec3 (0.0f), 0.0f, 0.5f, fullLifetime) == glm::vec3 (0.0f));
}

TEST_CASE ("particle velocity caps fade by normalized lifetime rather than seconds", "[particle][capvelocity]") {
    using WallpaperEngine::Render::Objects::calculateParticleVelocityCap;
    const glm::vec4 blendTimes (0.2f, 0.4f, 0.6f, 0.8f);
    ParticleInstance particle;
    particle.lifetime = 10.0f;
    for (const auto& [age, expectedSpeed] : std::vector<std::pair<float, float>> {
	     { 1.0f, 200.0f }, { 3.0f, 150.0f }, { 5.0f, 100.0f }, { 7.0f, 150.0f }, { 9.0f, 200.0f } }) {
	particle.age = age;
	const auto velocity = calculateParticleVelocityCap (
	    { 200.0f, 0.0f, 0.0f }, 100.0f, particle.getLifetimePos (), blendTimes
	);
	CHECK (velocity.x == Catch::Approx (expectedSpeed));
    }
    // Overlapping ramps multiply; a min() envelope would cap to 150 instead.
    CHECK (calculateParticleVelocityCap ({ 200.0f, 0.0f, 0.0f }, 100.0f, 0.5f, { 0.0f, 1.0f, 0.0f, 1.0f }).x
	   == Catch::Approx (175.0f));
    // The shipped thunderbolt uses coincident .2/.2 blend-in endpoints.
    CHECK (calculateParticleVelocityCap ({ 200.0f, 0.0f, 0.0f }, 50.0f, 0.1f, { 0.2f, 0.2f, 1.0f, 1.0f }).x
	   == 200.0f);
    CHECK (calculateParticleVelocityCap ({ 200.0f, 0.0f, 0.0f }, 50.0f, 0.2f, { 0.2f, 0.2f, 1.0f, 1.0f }).x
	   == Catch::Approx (50.0f));
}

TEST_CASE ("particle emission follows the strongest selected audio band", "[particle]") {
    float left[16] {}, right[16] {};
    CHECK (calculateParticleAudioResponse (left, right, 0, { 0.8f, 1.0f }, 0.5f, 4, 8) == 1.0f);
    CHECK (calculateParticleAudioResponse (left, right, 3, { 0.8f, 1.0f }, 0.5f, 4, 8) == 0.0f);
    left[6] = 0.9f;
    right[6] = 0.9f;
    // Native 14022a8a0 takes a band maximum, then smoothstep and exponent.
    CHECK (calculateParticleAudioResponse (left, right, 3, { 0.8f, 1.0f }, 0.5f, 4, 8)
	   == Catch::Approx (std::sqrt (0.5f)).margin (0.00001f));
    CHECK (calculateParticleAudioResponse (left, right, 1, { 0.8f, 1.0f }, 1.0f, 6, 6)
	   == Catch::Approx (0.5f).margin (0.00001f));
    right[6] = 0.0f;
    right[7] = 0.9f;
    CHECK (calculateParticleAudioResponse (left, right, 3, { 0.8f, 1.0f }, 0.5f, 4, 8) == 0.0f);
    CHECK (calculateParticleAudioResponse (left, right, 2, { 0.8f, 1.0f }, 1.0f, 7, 7)
	   == Catch::Approx (0.5f).margin (0.00001f));
    CHECK (calculateParticleAudioResponse (left, right, 1, { 0.8f, 1.0f }, 0.5f, 0, 5) == 0.0f);
    CHECK (calculateParticleAudioResponse (left, right, 1, { 1.0f, 0.8f }, 1.0f, 6, 6)
	   == Catch::Approx (0.5f).margin (0.00001f));
}

TEST_CASE ("particle emitters retain periodic caps and default to unlimited", "[particle]") {
    auto filesystem = std::make_unique<WallpaperEngine::FileSystem::Container> ();
    filesystem->getVFS ().add ("particles/periodic.json", R"({
        "emitter":[{"name":"boxrandom"},
                   {"name":"boxrandom","maxtoemitperperiod":0},
                   {"name":"boxrandom","flags":4,"maxtoemitperperiod":3,
                    "minperiodicduration":10,"maxperiodicduration":11}]
    })");
    WallpaperEngine::Data::Model::Project project {};
    project.assetLocator = std::make_unique<WallpaperEngine::Assets::AssetLocator> (std::move (filesystem));
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":"particles/periodic.json"})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->emitters.size () == 3);
    CHECK (particle->emitters[0].maxToEmitPerPeriod == 0);
    CHECK (particle->emitters[1].maxToEmitPerPeriod == 0);
    CHECK (particle->emitters[2].maxToEmitPerPeriod == 3);
    CHECK (particle->emitters[2].flags == 4);
    CHECK (particle->emitters[2].minPeriodicDuration == 10.0f);
    CHECK (particle->emitters[2].maxPeriodicDuration == 11.0f);
}

TEST_CASE ("discharge particles retain their between-control-point initializer", "[particle]") {
    auto filesystem = std::make_unique<WallpaperEngine::FileSystem::Container> ();
    filesystem->getVFS ().add ("particles/discharge.json", R"({
        "initializer":[{"name":"mapsequencebetweencontrolpoints","count":10,"limitbehavior":"mirror"}],
        "emitter":[{"name":"sphererandom","audioprocessingexponent":0.5}]
    })");
    WallpaperEngine::Data::Model::Project project {};
    project.assetLocator = std::make_unique<WallpaperEngine::Assets::AssetLocator> (std::move (filesystem));
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":19,"particle":"particles/discharge.json"})"), project
    );
    const auto* particle = object->as<WallpaperEngine::Data::Model::Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->initializers.size () == 1);
    REQUIRE (particle->initializers.front () != nullptr);
    const auto* initializer = particle->initializers.front ()->as<MapSequenceBetweenControlPointsInitializer> ();
    REQUIRE (initializer != nullptr);
    CHECK (initializer->count->value->getFloat () == 10.0f);
    CHECK (initializer->bounds->value->getVec2 () == glm::vec2 (0.0f, 1.0f));
    CHECK (initializer->controlPointStart == 0);
    CHECK (initializer->controlPointEnd == 1);
    CHECK (initializer->flags == 0);
    CHECK (initializer->limitBehavior == "mirror");
    CHECK (initializer->arcAmount == Catch::Approx (0.3f));
    CHECK (initializer->arcDirection == glm::vec3 (0.0f, 1.0f, 0.0f));
    CHECK (initializer->sizeReductionAmount == Catch::Approx (0.9f));
    REQUIRE (particle->emitters.size () == 1);
    CHECK (particle->emitters.front ().audioProcessingExponent == Catch::Approx (0.5f));
}

TEST_CASE ("between-control-point sequences include endpoints and reflect in spawn order", "[particle]") {
    using WallpaperEngine::Data::Builders::UserSettingBuilder;
    using WallpaperEngine::Render::Objects::initializeParticleBetweenControlPoints;
    MapSequenceBetweenControlPointsInitializer initializer (
	UserSettingBuilder::fromValue (3.0f), UserSettingBuilder::fromValue (glm::vec2 (0.0f, 1.0f)),
	"mirror", 0, 1, 0, 0.3f, glm::vec3 (0.0f, 1.0f, 0.0f), 0.9f
    );
    float phase = 0.0f, direction = 1.0f;
    for (const float expectedX : { 20.0f, 70.0f, 120.0f, 70.0f, 20.0f }) {
	ParticleInstance particle;
	particle.position = { 7.0f, 5.0f, 3.0f };
	initializeParticleBetweenControlPoints (particle, initializer, { 20, -30, 0 }, { 120, -30, 0 }, phase, direction);
	CHECK (particle.position == glm::vec3 (expectedX, -25.0f, 3.0f));
    }
    initializer.limitBehavior = "repeat";
    phase = 0.0f;
    direction = 1.0f;
    for (const float expectedX : { 20.0f, 70.0f, 120.0f, 20.0f }) {
	ParticleInstance particle;
	initializeParticleBetweenControlPoints (particle, initializer, { 20, -30, 0 }, { 120, -30, 0 }, phase, direction);
	CHECK (particle.position == glm::vec3 (expectedX, -30.0f, 0.0f));
    }
}

TEST_CASE ("between-control-point count scales spacing before subtracting the endpoint", "[particle][sequence-count]") {
    using WallpaperEngine::Data::Builders::UserSettingBuilder;
    using WallpaperEngine::Render::Objects::initializeParticleBetweenControlPoints;
    MapSequenceBetweenControlPointsInitializer initializer (
	UserSettingBuilder::fromValue (3.0f), UserSettingBuilder::fromValue (glm::vec2 (0.0f, 1.0f)),
	"repeat", 0, 1, 16, 0.3f, glm::vec3 (0.0f, 1.0f, 0.0f), 0.9f
    );
    for (const auto [multiplier, secondX] : { std::pair { 1.0f, 66.0f }, { 2.0f, 33.0f }, { 4.0f, 21.0f } }) {
	float phase = 0.0f, direction = 1.0f;
	ParticleInstance first, second;
	initializeParticleBetweenControlPoints (first, initializer, { 11, 0, 0 }, { 121, 0, 0 }, phase, direction, multiplier);
	initializeParticleBetweenControlPoints (second, initializer, { 11, 0, 0 }, { 121, 0, 0 }, phase, direction, multiplier);
	CHECK (first.position.x == 11.0f);
	CHECK (second.position.x == Catch::Approx (secondX));
    }
    // Fractional authored counts remain fractional; the native floor applies
    // after multiplication and endpoint subtraction.
    initializer.count->value->update (1.5f, DynamicValue::UpdateSource::Initialization);
    float phase = 0.0f, direction = 1.0f;
    ParticleInstance particle;
    initializeParticleBetweenControlPoints (particle, initializer, {}, { 1, 0, 0 }, phase, direction, 2.0f);
    CHECK (phase == 0.5f);
    initializer.limitBehavior = "mirror";
    phase = 0.0f;
    initializeParticleBetweenControlPoints (particle, initializer, {}, { 1, 0, 0 }, phase, direction, 0.0f);
    CHECK (phase == Catch::Approx (-9998.0f));
    CHECK (direction == -1.0f);
}

TEST_CASE ("between-control-point flags taper spread velocity size and authored arc", "[particle]") {
    using WallpaperEngine::Data::Builders::UserSettingBuilder;
    using WallpaperEngine::Render::Objects::initializeParticleBetweenControlPoints;
    MapSequenceBetweenControlPointsInitializer initializer (
	UserSettingBuilder::fromValue (3.0f), UserSettingBuilder::fromValue (glm::vec2 (0.0f, 1.0f)),
	"repeat", 0, 1, 15, 0.3f, glm::vec3 (0.0f, 1.0f, 0.0f), 0.9f
    );
    for (const float initialPhase : { 0.0f, 0.5f, 1.0f }) {
	float phase = initialPhase, direction = 1.0f;
	ParticleInstance particle;
	particle.position = { 7, 5, 3 };
	particle.velocity = { 2, 4, 6 };
	particle.size = 10.0f;
	initializeParticleBetweenControlPoints (particle, initializer, { 20, -30, 0 }, { 120, -30, 0 }, phase, direction);
	if (initialPhase == 0.5f) {
	    CHECK (particle.position == glm::vec3 (70, -55, 3));
	    CHECK (particle.velocity == glm::vec3 (2, 4, 6));
	    CHECK (particle.size == 10.0f);
	} else {
	    CHECK (particle.position == glm::vec3 (initialPhase == 0.0f ? 20 : 120, -30, 0));
	    CHECK (particle.velocity == glm::vec3 (0));
	    CHECK (particle.size == Catch::Approx (1.0f));
	}
	CHECK (particle.initial.size == particle.size);
    }
}

TEST_CASE ("billboard particles preserve layer roll in the camera plane", "[particle]") {
    // A tree with a half-turn must point down even under a yawed parent. The
    // negative scale also exercises the simulation's Y reflection.
    const glm::mat4 model = glm::rotate (glm::mat4 (1.0f), 0.7f, { 0.0f, 1.0f, 0.0f })
	* glm::scale (glm::mat4 (1.0f), { 2.0f, -3.0f, 4.0f });
    const glm::mat4 cameraWorld = glm::inverse (glm::lookAt (
	glm::vec3 (7.0f, 3.0f, 5.0f), glm::vec3 (0.0f), glm::vec3 (0.0f, 1.0f, 0.0f)
    ));
    const glm::vec2 expectedRight[] { { 1, 0 }, { 0, 1 }, { -1, 0 } };
    const glm::vec2 expectedUp[] { { 0, 1 }, { -1, 0 }, { 0, -1 } };
    for (int quarterTurns = 0; quarterTurns < 3; ++quarterTurns) {
	const auto local = calculateBillboardParticleOrientation (
	    glm::inverse (model), cameraWorld, glm::radians (90.0f * quarterTurns)
	);
	const glm::mat3 viewModel = glm::mat3 (glm::inverse (cameraWorld) * model);
	const glm::vec3 right = glm::normalize (viewModel * local[0]);
	const glm::vec3 up = glm::normalize (viewModel * local[1]);
	CHECK (right.x == Catch::Approx (expectedRight[quarterTurns].x).margin (0.000001f));
	CHECK (right.y == Catch::Approx (expectedRight[quarterTurns].y).margin (0.000001f));
	CHECK (right.z == Catch::Approx (0.0f).margin (0.000001f));
	CHECK (up.x == Catch::Approx (expectedUp[quarterTurns].x).margin (0.000001f));
	CHECK (up.y == Catch::Approx (expectedUp[quarterTurns].y).margin (0.000001f));
	CHECK (up.z == Catch::Approx (0.0f).margin (0.000001f));
    }
}

TEST_CASE ("fixed particle orientation matches native draw helper vectors", "[particle]") {
    // Captured by executing wallpaper64.exe's 1402298b0 with the same inputs.
    const glm::mat3 expected[] {
	{ { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
	{ { .948683381f, 0, -.316227823f }, { -.169030860f, .845154285f, -.507092595f },
	  { .267261267f, .534522533f, .801783800f } },
	{ { .996545732f, 0, -.083045490f }, { -.060682733f, .682680666f, -.728192687f },
	  { .077790990f, .350059390f, .933491766f } },
	{ { .940148652f, -.340494931f, .013556005f }, { .242097050f, .639406264f, -.729759276f },
	  { .147441968f, .442325890f, .884651780f } }
    };
    for (int sample = 0; sample < 4; ++sample) {
	const glm::vec3 axis = sample == 0 ? glm::vec3 (0, 1, 0) : glm::vec3 (1, 2, 3);
	glm::mat4 model (1.0f);
	if (sample >= 2) {
	    model = glm::rotate (model, .6f, glm::normalize (glm::vec3 (1, 2, 3)))
		* glm::scale (glm::mat4 (1.0f), glm::vec3 (2, 3, 4));
	}
	const auto actual = calculateFixedParticleOrientation (axis, glm::mat3 (model), sample == 3);
	for (int column = 0; column < 3; ++column) {
	    for (int row = 0; row < 3; ++row) {
		CHECK (actual[column][row] == Catch::Approx (expected[sample][column][row]).margin (0.000001f));
	    }
	}
    }
    CHECK (calculateFixedParticleOrientation (glm::vec3 (0), glm::mat3 (1.0f), false) == expected[0]);
    CHECK (calculateFixedParticleOrientation (glm::vec3 (0), glm::mat3 (0.0f), false) == expected[0]);
}

TEST_CASE ("upright rope facing preserves the native locked axis", "[particle][rope]") {
    using WallpaperEngine::Render::Objects::calculateUprightRopeForward;
    // Native pitched-camera captures: screen 14px, upright Y 8px, upright Z
    // 12px, upright Z with renderer flag 1 back to 8px. These normals produce
    // the distinct Y and Z ribbon widths rather than a camera-facing lookalike.
    const glm::vec3 cameraForward (0, .8f, .6f);
    for (const glm::vec3 axis : { glm::vec3 (0), glm::vec3 (0, 1, 0), glm::vec3 (0, 5, 0) }) {
	const auto actual = calculateUprightRopeForward (axis, glm::mat3 (1), cameraForward, false);
	CHECK (actual.x == Catch::Approx (0).margin (.000001f));
	CHECK (actual.y == Catch::Approx (0).margin (.000001f));
	CHECK (actual.z == Catch::Approx (1).margin (.000001f));
    }
    const auto axisZ = calculateUprightRopeForward ({ 0, 0, 5 }, glm::mat3 (1), cameraForward, false);
    CHECK (axisZ.x == Catch::Approx (0).margin (.000001f));
    CHECK (axisZ.y == Catch::Approx (1).margin (.000001f));
    CHECK (axisZ.z == Catch::Approx (0).margin (.000001f));
    const auto world = calculateUprightRopeForward ({ 0, 0, 5 }, glm::mat3 (1), cameraForward, true);
    CHECK (world.x == Catch::Approx (0).margin (.000001f));
    CHECK (world.y == Catch::Approx (0).margin (.000001f));
    CHECK (world.z == Catch::Approx (1).margin (.000001f));
}

TEST_CASE ("control point spaces match native mirrored-layer results", "[particle]") {
    // Executed the installed wallpaper64.exe's 14022bd40 with Sea's actual
    // overrides. Native local=(4885.391113,361.117310), world=(-865.697021,-1178.376709).
    // Our simulation reflects Y once, after resolving the authored space.
    const glm::mat4 layer = glm::translate (glm::mat4 (1.0f), { 4019.69409f, 1539.49402f, 0.0f })
	* glm::scale (glm::mat4 (1.0f), { -1.0f, 1.0f, 1.0f });
    const glm::vec3 offset { 4885.39111f, 361.11731f, 0.0f };
    const auto local = resolveParticleControlPoint (offset, glm::inverse (layer), false);
    CHECK (local.x == Catch::Approx (4885.391113f));
    CHECK (local.y == Catch::Approx (-361.117310f));
    const auto world = resolveParticleControlPoint (offset, glm::inverse (layer), true);
    CHECK (world.x == Catch::Approx (-865.697021f));
    CHECK (world.y == Catch::Approx (1178.376709f));
    CHECK (world.z == 0.0f);
    CHECK (resolveParticleControlPoint (offset, glm::inverse (glm::mat4 (0.0f)), true) == glm::vec3 (0.0f));
}

TEST_CASE ("particle frame clock retains individual long uptime intervals", "[particle][particle-time]") {
    using WallpaperEngine::Render::Objects::advanceParticleRenderTime;
    for (const double uptime : { 432000.0, 2678400.0 }) {
        for (const double fps : { 60.0, 165.0 }) {
            CAPTURE (uptime, fps);
            double previous = uptime;
            const double now = uptime + 1.0 / fps;
            CHECK (advanceParticleRenderTime (now, previous) == Catch::Approx (1.0 / fps));
            CHECK (previous == now);
            CHECK (advanceParticleRenderTime (now, previous) == 0.0f);
        }
    }
}

TEST_CASE ("particle frame clock preserves activation and rebased anchors", "[particle][particle-time]") {
    using WallpaperEngine::Render::Objects::advanceParticleRenderTime;
    const double uptime = 432000.0;
    const double interval = 1.0 / 60.0;
    double previous = 0.0;
    CHECK (advanceParticleRenderTime (uptime, previous) == 0.0f);
    CHECK (advanceParticleRenderTime (uptime + interval, previous) == Catch::Approx (interval));

    // A pooled child starts in its parent's current frame and advances on the next.
    previous = uptime + interval;
    CHECK (advanceParticleRenderTime (previous, previous) == 0.0f);
    CHECK (advanceParticleRenderTime (uptime + 2 * interval, previous) == Catch::Approx (interval));

    // Loading rebases the local anchor; a paused rate still samples the frame clock.
    previous = uptime - 10.25;
    previous += 10.25;
    const float elapsed = advanceParticleRenderTime (uptime + interval, previous);
    CHECK (elapsed == Catch::Approx (interval));
    CHECK (calculateParticleSimulationDelta (elapsed, 0.0f) == 0.0f);
    CHECK (advanceParticleRenderTime (uptime + 2 * interval, previous) == Catch::Approx (interval));
}

TEST_CASE ("particle instance rate scales the complete simulation", "[particle]") {
    CHECK (calculateParticleSimulationDelta (1.0f / 60.0f, 0.15f)
	   == Catch::Approx (0.15f / 60.0f));
    CHECK (calculateParticleSimulationDelta (1.0f / 60.0f, 0.0f) == 0.0f);
    CHECK (calculateParticleSimulationDelta (1.0f / 60.0f, -1.0f) == 0.0f);
}

TEST_CASE ("particle instance count scales emission independently", "[particle]") {
    CHECK (calculateParticleEmissionRate (3.0f, 0.15f) == Catch::Approx (0.45f));
    CHECK (calculateParticleEmissionRate (3.0f, 1.0f) == 3.0f);
    CHECK (calculateParticleEmissionRate (3.0f, -1.0f) == 0.0f);
}

TEST_CASE ("particle rotations cross the Y-down scene boundary", "[particle]") {
    CHECK (convertParticleRotationForRender ({ 1.0f, 2.0f, 3.0f }) == glm::vec3 (-1.0f, 2.0f, -3.0f));
}

TEST_CASE ("perspective billboard spin follows the native screen direction", "[particle]") {
    const glm::mat4 model = glm::rotate (glm::mat4 (1.0f), 0.7f, { 0.0f, 1.0f, 0.0f })
	* glm::scale (glm::mat4 (1.0f), { 2.0f, -2.0f, 2.0f });
    const glm::mat4 cameraWorld = glm::inverse (glm::lookAt (
	glm::vec3 (7.0f, 3.0f, 5.0f), glm::vec3 (0.0f), glm::vec3 (0.0f, 1.0f, 0.0f)
    ));
    const auto orientation = calculateBillboardParticleOrientation (glm::inverse (model), cameraWorld, 0.0f);
    for (const float angle : { glm::radians (45.0f), glm::radians (-45.0f) }) {
	const auto rotation = convertParticleRotationForRender ({ 0.0f, 0.0f, angle }, true);
	// common_particles.h rotates the UV-right tangent toward -Up for positive
	// Z. Native uploads that angle unchanged: its marker moves down on screen.
	const auto tangent = orientation * glm::vec3 (std::cos (rotation.z), -std::sin (rotation.z), 0.0f);
	const auto viewRight = glm::normalize (glm::mat3 (glm::inverse (cameraWorld) * model) * tangent);
	CHECK (viewRight.x == Catch::Approx (0.70710678f).margin (0.000001f));
	CHECK (viewRight.y == Catch::Approx (angle > 0.0f ? -0.70710678f : 0.70710678f).margin (0.000001f));
	CHECK (viewRight.z == Catch::Approx (0.0f).margin (0.000001f));
    }
}

TEST_CASE ("rope trails use the live particle visual state", "[particle]") {
    CHECK (calculateRopeTrailVisualValue (0.25f, 0.0f, false) == Catch::Approx (0.25f));
    CHECK (calculateRopeTrailVisualValue (0.25f, 0.5f, false) == Catch::Approx (0.25f));
    CHECK (calculateRopeTrailVisualValue (0.25f, 1.0f, false) == Catch::Approx (0.25f));

    CHECK (calculateRopeTrailVisualValue (0.25f, 0.0f, true) == Catch::Approx (0.0f));
    CHECK (calculateRopeTrailVisualValue (0.25f, 0.5f, true) == Catch::Approx (0.125f));
    CHECK (calculateRopeTrailVisualValue (0.25f, 1.0f, true) == Catch::Approx (0.25f));
}

TEST_CASE ("event-follow children carry local particles with the parent particle", "[particle][eventfollow]") {
    using WallpaperEngine::Render::Objects::calculateFollowChildDisplacement;
    // Meteors at Dawn: a one-shot head born at the first follow point must reach the tip.
    CHECK (calculateFollowChildDisplacement (glm::vec3 (382.0f, -353.0f, 0.0f), { 591.0f, -331.0f, 0.0f }, false)
	   == glm::vec3 (209.0f, 22.0f, 0.0f));
    CHECK (calculateFollowChildDisplacement (glm::vec3 (382.0f, -353.0f, 0.0f), { 591.0f, -331.0f, 0.0f }, true)
	   == glm::vec3 (0.0f));
    CHECK (calculateFollowChildDisplacement (std::nullopt, { 591.0f, -331.0f, 0.0f }, false) == glm::vec3 (0.0f));
}

TEST_CASE ("control point attraction fades through the full authored radius", "[particle]") {
    // At three quarters of the radius, force must still act at one quarter strength.
    CHECK (calculateControlPointAttraction ({ 750.0f, 0.0f, 0.0f }, 200.0f, 1000.0f, 0.1f).x
	   == Catch::Approx (5.0f));
    CHECK (calculateControlPointAttraction ({ 250.0f, 0.0f, 0.0f }, 200.0f, 1000.0f, 0.1f).x
	   == Catch::Approx (15.0f));
    CHECK (calculateControlPointAttraction ({ 0.0f, 0.0f, 750.0f }, -200.0f, 1000.0f, 0.1f).z
	   == Catch::Approx (-5.0f));
    CHECK (calculateControlPointAttraction ({ 1000.0f, 0.0f, 0.0f }, 200.0f, 1000.0f, 0.1f)
	   == glm::vec3 (0.0f));
    CHECK (calculateControlPointAttraction (glm::vec3 (0.0f), 200.0f, 1000.0f, 0.1f) == glm::vec3 (0.0f));
    CHECK (calculateControlPointAttraction ({ 1.0f, 0.0f, 0.0f }, 200.0f, 0.0f, 0.1f) == glm::vec3 (0.0f));
}

TEST_CASE ("two-control-point remaps retain only the measured linear subset", "[particle][remap]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":380,"particle":{
            "initializer":[
                {"name":"remapinitialvalue","input":"positionbetweentwocontrolpoints","output":"velocity","operation":"remap"},
                {"name":"remapinitialvalue","output":"positionbetweentwocontrolpoints","operation":"remap"},
                {"name":"remapinitialvalue","input":"positionbetweentwocontrolpoints","output":"velocity"},
                {"name":"remapinitialvalue","input":"positionbetweentwocontrolpoints","output":"velocity","operation":"remap","transformfunction":"simplexnoise"}
            ],"operator":[
                {"name":"remapvalue","input":"positionbetweentwocontrolpoints","output":"velocity","operation":"remap"},
                {"name":"remapvalue","input":"maxlifetime","output":"positionbetweentwocontrolpoints","operation":"remap"},
                {"name":"remapvalue","input":"color","output":"positionbetweentwocontrolpoints","operation":"remap"},
                {"name":"remapvalue","input":"positionbetweentwocontrolpoints","output":"velocity","operation":"remap","flags":4},
                {"name":"remapvalue","input":"lifetimefraction","output":"velocity","operation":"remap","transformfunction":"simplexnoise","inputcomponent":"x"},
                {"name":"remapvalue","input":"positionbetweentwocontrolpoints","output":"velocity","operation":"remap","outputcomponent":"x"},
                {"name":"remapvalue","input":"positionbetweentwocontrolpoints","operation":"remap"},
                {"name":"remapvalue","input":"positionbetweentwocontrolpoints","output":"velocity","operation":"remap","transformfunction":"unknown"}
            ]}})"), project
    );
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    // Dropping the new parser branches yields zero; treating unknown settings as
    // linear remaps incorrectly admits the unsupported nodes above.
    REQUIRE (particle->initializers.size () == 3);
    REQUIRE (particle->operators.size () == 3);
    const auto* defaultVelocity = particle->initializers.back ()->as<ControlPointRemapInitializer> ();
    REQUIRE (defaultVelocity != nullptr);
    CHECK (defaultVelocity->remap.input == ControlPointRemap::Input::BetweenPoints);
    CHECK (defaultVelocity->remap.output == ControlPointRemap::Output::Velocity);
    CHECK (defaultVelocity->remap.operation == ControlPointRemap::Operation::Multiply);
    const auto* defaultSize = particle->operators.back ()->as<ControlPointRemapOperator> ();
    REQUIRE (defaultSize != nullptr);
    CHECK (defaultSize->remap.input == ControlPointRemap::Input::BetweenPoints);
    CHECK (defaultSize->remap.output == ControlPointRemap::Output::Size);
    CHECK (defaultSize->remap.operation == ControlPointRemap::Operation::Remap);
}

TEST_CASE ("two-control-point scalar input follows the native output pair", "[particle][remap]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    std::vector<ControlPointData> points (4);
    points[0].position = { -60, 0, 0 };
    points[1].position = { 60, 0, 0 };
    points[2].position = { -60, 0, 0 };
    points[3].position = { 0, 0, 0 };
    const struct { uint32_t input0, input1, output0, output1; float velocity; int red; } cases[] = {
        { 0, 1, 0, 1, 15, 147 }, { 2, 3, 0, 1, 15, 147 }, { 0, 1, 2, 3, 30, 166 }
    };
    for (const bool initializer : { true, false }) {
        for (const auto& sample : cases) {
            CAPTURE (initializer, sample.input0, sample.output0);
            WallpaperEngine::Data::Model::Project project {};
            auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":381,"particle":{}})");
            data["particle"][initializer ? "initializer" : "operator"] = {
                { { "name", initializer ? "remapinitialvalue" : "remapvalue" },
                  { "input", "positionbetweentwocontrolpoints" }, { "output", "velocity" },
                  { "operation", "remap" }, { "outputrangemax", "60 0 0" },
                  { "inputcontrolpoint0", sample.input0 }, { "inputcontrolpoint1", sample.input1 },
                  { "outputcontrolpoint0", sample.output0 }, { "outputcontrolpoint1", sample.output1 } }
            };
            const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
            const auto* definition = object->as<Particle> ();
            REQUIRE (definition != nullptr);
            const ControlPointRemap* remap;
            if (initializer) {
                REQUIRE (definition->initializers.size () == 1);
                const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
                REQUIRE (parsed != nullptr);
                remap = &parsed->remap;
            } else {
                REQUIRE (definition->operators.size () == 1);
                const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                REQUIRE (parsed != nullptr);
                remap = &parsed->remap;
            }
            ParticleInstance particle;
            particle.position = { -30, 0, 0 };
            particle.velocity = { 20, 0, 0 };
            applyParticleControlPointRemap (particle, *remap, points, initializer);
            CHECK (particle.velocity.x == Catch::Approx (sample.velocity));
            CHECK (particle.velocity.y == 0);
            CHECK (particle.velocity.z == 0);
            // Current native binary, frozen priority probes, .5 + velocity / 200.
            CHECK (std::lround (255 * (.5f + particle.velocity.x / 200)) == sample.red);
        }
    }
}

TEST_CASE ("two-control-point scalar output preserves perpendicular simulation position", "[particle][remap]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    std::vector<ControlPointData> points (2);
    points[0].position = { -60, 0, 0 };
    points[1].position = { 60, 0, 0 };
    for (const bool initializer : { true, false }) {
        WallpaperEngine::Data::Model::Project project {};
        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":382,"particle":{}})");
        data["particle"][initializer ? "initializer" : "operator"] = {
            { { "name", initializer ? "remapinitialvalue" : "remapvalue" },
              { "input", "maxlifetime" }, { "output", "positionbetweentwocontrolpoints" },
              { "operation", "remap" }, { "inputrangemax", 200 } }
        };
        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
        const auto* definition = object->as<Particle> ();
        REQUIRE (definition != nullptr);
        const ControlPointRemap* remap;
        if (initializer) {
            REQUIRE (definition->initializers.size () == 1);
            const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
            REQUIRE (parsed != nullptr);
            remap = &parsed->remap;
        } else {
            REQUIRE (definition->operators.size () == 1);
            const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
            REQUIRE (parsed != nullptr);
            remap = &parsed->remap;
        }
        ParticleInstance particle;
        // Native readback is (0,45,40); the fork already reflects simulation Y.
        particle.position = { -30, -45, 40 };
        particle.velocity = { 20, 0, 0 };
        particle.lifetime = 100;
        applyParticleControlPointRemap (particle, *remap, points, initializer);
        CHECK (particle.position.x == Catch::Approx (0).margin (.001f));
        CHECK (particle.position.y == -45);
        CHECK (particle.position.z == 40);
        CHECK (particle.velocity == glm::vec3 (20, 0, 0));
        // Reapplying a remap replaces the fraction instead of accumulating it.
        applyParticleControlPointRemap (particle, *remap, points, initializer);
        CHECK (particle.position == glm::vec3 (0, -45, 40));
        points[1].position = points[0].position;
        applyParticleControlPointRemap (particle, *remap, points, initializer);
        CHECK (particle.position == glm::vec3 (0, -45, 40));
        points[1].position = { 60, 0, 0 };
    }
}

TEST_CASE ("two-control-point position arithmetic preserves residual and composes", "[particle][remap][remap-arithmetic]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { const char* operation; float first, second; } cases[] = {
        { "remap", .5f, .5f }, { "multiply", .125f, .0625f },
        { "add", .75f, 1.25f }, { "subtract", -.25f, -.75f },
        { nullptr, .125f, .0625f }
    };
    std::vector<ControlPointData> points (2);
    points[0].position = { 10, -20, 30 };
    points[1].position = { 130, 140, -10 };
    const glm::vec3 delta = points[1].position - points[0].position;
    const glm::vec3 residual (20, -10, 20); // Perpendicular to this oblique pair.
    for (const bool initializer : { true, false }) {
        for (const auto& entry : cases) {
            INFO ("initializer=" << initializer << ", operation=" << (entry.operation ? entry.operation : "default"));
            WallpaperEngine::Data::Model::Project project {};
            auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":385,"particle":{}})");
            auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                "input":"maxlifetime","output":"positionbetweentwocontrolpoints","inputrangemax":200
            })");
            node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
            if (entry.operation) node["operation"] = entry.operation;
            data["particle"][initializer ? "initializer" : "operator"] = { node };
            const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
            const auto* definition = object->as<Particle> ();
            REQUIRE (definition != nullptr);
            const ControlPointRemap* remap;
            if (initializer) {
                REQUIRE (definition->initializers.size () == 1);
                const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
                REQUIRE (parsed != nullptr);
                remap = &parsed->remap;
            } else {
                REQUIRE (definition->operators.size () == 1);
                const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                REQUIRE (parsed != nullptr);
                remap = &parsed->remap;
            }
            ParticleInstance particle;
            particle.position = points[0].position + delta * .25f + residual;
            particle.velocity = { 7, -11, 13 };
            particle.lifetime = 100;
            for (const float fraction : { entry.first, entry.second }) {
                applyParticleControlPointRemap (particle, *remap, points, initializer);
                const glm::vec3 expected = points[0].position + delta * fraction + residual;
                CHECK (particle.position.x == Catch::Approx (expected.x).margin (.001f));
                CHECK (particle.position.y == Catch::Approx (expected.y).margin (.001f));
                CHECK (particle.position.z == Catch::Approx (expected.z).margin (.001f));
                CHECK (particle.velocity == glm::vec3 (7, -11, 13));
            }
        }
    }
}

TEST_CASE ("two-control-point position arithmetic rejects unmeasured settings", "[particle][remap][remap-arithmetic]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":386,"particle":{"operator":[
            {"name":"remapvalue","input":"maxlifetime","output":"positionbetweentwocontrolpoints","operation":"divide"},
            {"name":"remapvalue","input":"maxlifetime","output":"positionbetweentwocontrolpoints","operation":"add","flags":4},
            {"name":"remapvalue","input":"maxlifetime","output":"positionbetweentwocontrolpoints","operation":"add","transformfunction":"simplexnoise"},
            {"name":"remapvalue","input":"lifetimefraction","output":"speed","operation":"multiply","transformfunction":"fbmnoise","flags":3,"transformoctaves":2},
            {"name":"remapvalue","input":"maxlifetime","output":"positionbetweentwocontrolpoints","operation":"add","outputcomponent":"x"},
            {"name":"remapvalue","input":"positionbetweentwocontrolpoints","output":"velocity","operation":"divide"},
            {"name":"remapvalue","input":"color","output":"positionbetweentwocontrolpoints","operation":"add"}
        ]}})"), project);
    const auto* definition = object->as<Particle> ();
    REQUIRE (definition != nullptr);
    CHECK (definition->operators.empty ());
}

TEST_CASE ("two-control-point remap flags clamp independent mapping stages", "[particle][remap][remap-flags]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { uint32_t flags; float input; glm::vec3 mapped; } cases[] = {
        { 0, -.25f, { -3, 1.5f, .125f } }, { 0, .25f, { -1, 2.5f, .375f } }, { 0, 1.25f, { 3, 4.5f, .875f } },
        { 1, -.25f, { -2, 2, .25f } }, { 1, .25f, { -1, 2.5f, .375f } }, { 1, 1.25f, { 2, 4, .75f } },
        { 2, -.25f, { 0, 1, .125f } }, { 2, .25f, { 0, 1, .375f } }, { 2, 1.25f, { 1, 1, .875f } },
        { 3, -.25f, { 0, 1, .25f } }, { 3, .25f, { 0, 1, .375f } }, { 3, 1.25f, { 1, 1, .75f } }
    };
    std::vector<ControlPointData> points (2);
    points[0].position = { -60, 0, 0 };
    points[1].position = { 60, 0, 0 };
    for (const bool initializer : { true, false }) {
        for (const bool positionInput : { true, false }) {
            for (const auto& entry : cases) {
                INFO ("initializer=" << initializer << ", positionInput=" << positionInput
                      << ", flags=" << entry.flags << ", input=" << entry.input);
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":387,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({"operation":"remap"})");
                node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                node["input"] = positionInput ? "positionbetweentwocontrolpoints" : "maxlifetime";
                node["output"] = positionInput ? "velocity" : "positionbetweentwocontrolpoints";
                node["flags"] = entry.flags;
                node["inputrangemin"] = positionInput ? 0 : 100;
                node["inputrangemax"] = positionInput ? 1 : 200;
                if (positionInput) {
                    node["outputrangemin"] = "-2 2 0.25";
                    node["outputrangemax"] = "2 4 0.75";
                } else {
                    node["outputrangemin"] = -2;
                    node["outputrangemax"] = 2;
                }
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE (definition != nullptr);
                const ControlPointRemap* remap;
                if (initializer) {
                    REQUIRE (definition->initializers.size () == 1);
                    const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
                    REQUIRE (parsed != nullptr);
                    remap = &parsed->remap;
                } else {
                    REQUIRE (definition->operators.size () == 1);
                    const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                    REQUIRE (parsed != nullptr);
                    remap = &parsed->remap;
                }
                ParticleInstance particle;
                particle.position = { -60 + 120 * entry.input, -45, 40 };
                particle.velocity = { 7, -11, 13 };
                particle.lifetime = 100 + 100 * entry.input;
                applyParticleControlPointRemap (particle, *remap, points, initializer);
                if (positionInput) {
                    CHECK (particle.velocity == entry.mapped * glm::vec3 (1, -1, 1));
                    CHECK (particle.position == glm::vec3 (-60 + 120 * entry.input, -45, 40));
                } else {
                    CHECK (particle.position == glm::vec3 (-60 + 120 * entry.mapped.x, -45, 40));
                    CHECK (particle.velocity == glm::vec3 (7, -11, 13));
                }
            }
        }
    }
}

TEST_CASE ("two-control-point output clamp precedes position arithmetic", "[particle][remap][remap-flags]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    std::vector<ControlPointData> points (2);
    points[0].position = { -60, 0, 0 };
    points[1].position = { 60, 0, 0 };
    for (const bool initializer : { true, false }) {
        WallpaperEngine::Data::Model::Project project {};
        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":388,"particle":{}})");
        auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
            "input":"maxlifetime","output":"positionbetweentwocontrolpoints",
            "operation":"add","flags":3,"outputrangemin":2,"outputrangemax":2
        })");
        node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
        data["particle"][initializer ? "initializer" : "operator"] = { node };
        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
        const auto* definition = object->as<Particle> ();
        REQUIRE (definition != nullptr);
        const ControlPointRemap* remap;
        if (initializer) {
            REQUIRE (definition->initializers.size () == 1);
            remap = &definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap;
        } else {
            REQUIRE (definition->operators.size () == 1);
            remap = &definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
        }
        ParticleInstance particle;
        particle.position = { 30, -45, 40 };
        particle.lifetime = 100;
        applyParticleControlPointRemap (particle, *remap, points, initializer);
        CHECK (particle.position == glm::vec3 (150, -45, 40));
        applyParticleControlPointRemap (particle, *remap, points, initializer);
        CHECK (particle.position == glm::vec3 (270, -45, 40));
    }
}

TEST_CASE ("two-control-point position remaps accept operator lifetime fraction", "[particle][remap][remap-lifetime]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { const char* operation; float first, second; } cases[] = {
        { "remap", .25f, .5f }, { "multiply", .125f, .0625f },
        { "add", .75f, 1.25f }, { "subtract", .25f, -.25f },
        { nullptr, .125f, .0625f }
    };
    std::vector<ControlPointData> points (2);
    points[0].position = { -60, 0, 0 };
    points[1].position = { 60, 0, 0 };
    for (const bool explicitInput : { true, false }) {
        for (const float lifetime : { 2.0f, 8.0f }) {
            for (const auto& entry : cases) {
                INFO ("explicitInput=" << explicitInput << ", lifetime=" << lifetime
                      << ", operation=" << (entry.operation ? entry.operation : "default"));
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":389,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                    "name":"remapvalue","output":"positionbetweentwocontrolpoints"
                })");
                if (explicitInput) node["input"] = "lifetimefraction";
                if (entry.operation) node["operation"] = entry.operation;
                data["particle"]["operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE (definition != nullptr);
                REQUIRE (definition->operators.size () == 1);
                const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                REQUIRE (parsed != nullptr);
                ParticleInstance particle;
                particle.position = { 0, -45, 40 };
                particle.velocity = { 7, -11, 13 };
                particle.lifetime = lifetime;
                particle.age = lifetime * .25f;
                applyParticleControlPointRemap (particle, parsed->remap, points, false);
                CHECK (particle.position == glm::vec3 (-60 + 120 * entry.first, -45, 40));
                particle.age = lifetime * .5f;
                applyParticleControlPointRemap (particle, parsed->remap, points, false);
                CHECK (particle.position == glm::vec3 (-60 + 120 * entry.second, -45, 40));
                CHECK (particle.velocity == glm::vec3 (7, -11, 13));
            }
        }
    }
}

TEST_CASE ("two-control-point position envelopes blend after arithmetic", "[particle][remap][remap-envelope]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { const char* operation; float operated; } operations[] = {
        { "remap", .5f }, { "multiply", .125f }, { "add", .75f }, { "subtract", -.25f }
    };
    const struct { float age, weight; } samples[] = {
        { 0, 0 }, { 25, .5f }, { 50, 1 }, { 75, .5f }, { 100, 0 }
    };
    std::vector<ControlPointData> points (2);
    points[0].position = { -60, 0, 0 };
    points[1].position = { 60, 0, 0 };
    for (const auto& operation : operations) {
        WallpaperEngine::Data::Model::Project project {};
        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":390,"particle":{"operator":[{
            "name":"remapvalue","input":"maxlifetime","output":"positionbetweentwocontrolpoints",
            "inputrangemax":200,"blendinend":0.5,"blendoutstart":0.5
        }]}})");
        data["particle"]["operator"][0]["operation"] = operation.operation;
        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
        const auto* definition = object->as<Particle> ();
        REQUIRE (definition != nullptr);
        REQUIRE (definition->operators.size () == 1);
        const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
        REQUIRE (parsed != nullptr);
        for (const auto& sample : samples) {
            INFO ("operation=" << operation.operation << ", age=" << sample.age);
            ParticleInstance particle;
            particle.position = { -30, -45, 40 };
            particle.velocity = { 7, -11, 13 };
            particle.lifetime = 100;
            particle.age = sample.age;
            applyParticleControlPointRemap (particle, parsed->remap, points, false);
            CHECK (particle.position == glm::vec3 (-30 + 120 * sample.weight * (operation.operated - .25f), -45, 40));
            CHECK (particle.velocity == glm::vec3 (7, -11, 13));
        }
    }
}

TEST_CASE ("two-control-point velocity envelopes blend current velocity", "[particle][remap][remap-envelope]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":391,"particle":{
            "operator":[{"name":"remapvalue","input":"positionbetweentwocontrolpoints","output":"velocity",
                "operation":"remap","outputrangemax":"80 160 240","blendinend":0.5,"blendoutstart":0.5}],
            "initializer":[{"name":"remapinitialvalue","input":"positionbetweentwocontrolpoints","output":"velocity",
                "operation":"remap","outputrangemax":"80 160 240","blendinend":0.5,"blendoutstart":0.5}]
        }})"), project);
    const auto* definition = object->as<Particle> ();
    REQUIRE (definition != nullptr);
    REQUIRE (definition->operators.size () == 1);
    REQUIRE (definition->initializers.size () == 1);
    const auto* op = definition->operators.front ()->as<ControlPointRemapOperator> ();
    const auto* init = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
    REQUIRE (op != nullptr);
    REQUIRE (init != nullptr);
    std::vector<ControlPointData> points (2);
    points[0].position = { -60, 0, 0 };
    points[1].position = { 60, 0, 0 };
    const struct { float age; glm::vec3 expected; } samples[] = {
        { 0, { 8, -12, 16 } }, { 25, { 14, -16, 18 } }, { 50, { 20, -20, 20 } },
        { 75, { 14, -16, 18 } }, { 100, { 8, -12, 16 } }
    };
    for (const auto& sample : samples) {
        ParticleInstance particle;
        particle.position = { -30, -45, 40 };
        particle.velocity = { 8, -12, 16 };
        particle.lifetime = 100;
        particle.age = sample.age;
        applyParticleControlPointRemap (particle, op->remap, points, false);
        CHECK (particle.velocity == sample.expected);
        CHECK (particle.position == glm::vec3 (-30, -45, 40));
        applyParticleControlPointRemap (particle, init->remap, points, true);
        CHECK (particle.velocity == glm::vec3 (20, -40, 60));
    }
}

TEST_CASE ("simplex remap envelopes blend mapped velocity without moving the noise sample", "[particle][remap][remap-noise-envelope]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    const struct { glm::vec4 envelope; glm::vec3 expected; } cases[] = {
        { { 0, 0, 1, 1 }, { -51.527244f, 663.42810f, .59818530f } },
        { { .5f, .75f, 1, 1 }, { 8, -12, 16 } },
        { { 0, .5f, 1, 1 }, { -21.763622f, 325.71405f, 8.29909265f } },
        { { 0, .5f, 0, .5f }, { -6.881811f, 156.857025f, 12.149546325f } },
        { { 0, 0, 0, .25f }, { 8, -12, 16 } }
    };
    for (const auto& sample : cases) {
        WallpaperEngine::Data::Model::Project project {};
        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":389,"particle":{"operator":[{
            "name":"remapvalue","operation":"remap","output":"velocity",
            "outputrangemin":"-100 -200 0","outputrangemax":"100 -1200 1",
            "transformfunction":"simplexnoise","transforminputscale":10
        }]}})");
        auto& node = data["particle"]["operator"][0];
        node["blendinstart"] = sample.envelope.x;
        node["blendinend"] = sample.envelope.y;
        node["blendoutstart"] = sample.envelope.z;
        node["blendoutend"] = sample.envelope.w;
        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
        const auto* definition = object->as<Particle> ();
        REQUIRE (definition != nullptr);
        REQUIRE (definition->operators.size () == 1);
        const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
        REQUIRE (parsed != nullptr);
        ParticleInstance particle;
        particle.lifetime = 8;
        particle.age = 2;
        particle.oscillationRandom = .5f;
        particle.velocity = { 8, -12, 16 };
        particle.position = { 12, 34, 56 };
        // Literal native noise samples at x=2.5; unchanged blended velocity
        // instructions 1402490af..1402490c7 interpolate AFTER mapping.
        applyParticleControlPointRemap (particle, parsed->remap, {}, false);
        for (int axis = 0; axis < 3; ++axis) {
            CHECK (particle.velocity[axis] == Catch::Approx (sample.expected[axis]).margin (.0002f));
        }
        CHECK (particle.position == glm::vec3 (12, 34, 56));
        CHECK (particle.age == 2);
        CHECK (particle.oscillationRandom == .5f);
    }
}

TEST_CASE ("noise remap envelopes follow normalized age through both fades", "[particle][remap][remap-noise-envelope]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":390,"particle":{"operator":[{
            "name":"remapvalue","operation":"remap","output":"velocity",
            "outputrangemin":"20 40 60","outputrangemax":"20 40 60",
            "transformfunction":"simplexnoise","blendinstart":0,"blendinend":0.5,
            "blendoutstart":0.5,"blendoutend":1
        }]}})"), project);
    const auto* definition = object->as<Particle> ();
    REQUIRE (definition != nullptr);
    REQUIRE (definition->operators.size () == 1);
    const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
    REQUIRE (parsed != nullptr);
    const struct { float fraction; glm::vec3 expected; } cases[] = {
        { 0, { 8, -12, 16 } }, { .25f, { 14, -26, 38 } },
        { .5f, { 20, -40, 60 } }, { .75f, { 14, -26, 38 } }, { 1, { 8, -12, 16 } }
    };
    for (const float lifetime : { 4.0f, 8.0f }) {
        for (const auto& sample : cases) {
            ParticleInstance particle;
            particle.lifetime = lifetime;
            particle.age = lifetime * sample.fraction;
            particle.oscillationRandom = .5f;
            particle.velocity = { 8, -12, 16 };
            applyParticleControlPointRemap (particle, parsed->remap, {}, false);
            CHECK (particle.velocity == sample.expected);
        }
    }
}

TEST_CASE ("FBM remap envelopes blend speed after saturation and multiplication", "[particle][remap][remap-noise-envelope]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":391,"particle":{"operator":[{
            "name":"remapvalue","output":"speed","outputrangemin":-5,"outputrangemax":7,
            "transformfunction":"fbmnoise","transforminputscale":8,"flags":3,
            "blendinstart":0,"blendinend":0.25,"blendoutstart":0.5,"blendoutend":1
        }]}})"), project);
    const auto* definition = object->as<Particle> ();
    REQUIRE (definition != nullptr);
    REQUIRE (definition->operators.size () == 1);
    const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
    REQUIRE (parsed != nullptr);
    const struct { float fraction; glm::vec3 expected; } cases[] = {
        { 0, { -30, 40, -50 } },
        { .0625f, { -22.5f, 30, -37.5f } },
        { .125f, { -26.0173575f, 34.68981f, -43.3622625f } },
        { .25f, { -30, 40, -50 } },
        { 1, { -30, 40, -50 } }
    };
    for (const auto& sample : cases) {
        ParticleInstance particle;
        particle.age = sample.fraction * 8;
        particle.lifetime = 8;
        particle.oscillationRandom = .5f;
        particle.velocity = { -30, 40, -50 };
        // Native FBM x=1 maps to .73449050. At weight .5 the speed
        // multiplier is .86724525. 140248241..258 applies operation→blend.
        applyParticleControlPointRemap (particle, parsed->remap, {}, false);
        for (int axis = 0; axis < 3; ++axis) {
            CHECK (particle.velocity[axis] == Catch::Approx (sample.expected[axis]).margin (.0001f));
        }
        CHECK (particle.oscillationRandom == .5f);
        particle.velocity = glm::vec3 (0);
        applyParticleControlPointRemap (particle, parsed->remap, {}, false);
        CHECK (particle.velocity == glm::vec3 (0));
    }
}

TEST_CASE ("rain remaps sample independent seeded velocity channels over lifetime", "[particle][remap][remap-noise]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    const struct { const char* minimum; const char* maximum; glm::vec3 expected, nextSeed, midpoint; } cases[] = {
        { "-100 -50 0", "100 -500 0", { -51.527244f, 258.54266f, 0.0f },
          { 7.5248338f, 329.34128f, 0.0f }, { 0, 275, 0 } },
        { "-100 -200 0", "100 -1200 1", { -51.527244f, 663.42810f, .59818530f },
          { 7.5248338f, 820.75839f, .62126880f }, { 0, 700, .5f } }
    };
    for (const auto& sample : cases) {
        WallpaperEngine::Data::Model::Project project {};
        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":383,"particle":{"operator":[{
            "name":"remapvalue","operation":"remap","output":"velocity",
            "transformfunction":"simplexnoise","transforminputscale":10
        }]}})");
        data["particle"]["operator"][0]["outputrangemin"] = sample.minimum;
        data["particle"]["operator"][0]["outputrangemax"] = sample.maximum;
        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
        const auto* definition = object->as<Particle> ();
        REQUIRE (definition != nullptr);
        REQUIRE (definition->operators.size () == 1);
        const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
        REQUIRE (parsed != nullptr);
        ParticleInstance particle;
        particle.lifetime = 8;
        particle.age = 2;
        particle.oscillationRandom = .5f;
        particle.velocity = { 700, 800, 900 };
        particle.position = { 12, 34, 56 };
        // Exact native kernel samples at x=2.5 and the bit-pattern seeds for
        // .5, .5 xor 188294317, .5 xor 1228574339; no RGB seed fitting.
        applyParticleControlPointRemap (particle, parsed->remap, {}, false);
        for (int axis = 0; axis < 3; ++axis) {
            CHECK (particle.velocity[axis] == Catch::Approx (sample.expected[axis]).margin (.0002f));
        }
        CHECK (particle.position == glm::vec3 (12, 34, 56));
        CHECK (particle.oscillationRandom == .5f);
        const auto velocity = particle.velocity;
        applyParticleControlPointRemap (particle, parsed->remap, {}, false);
        CHECK (particle.velocity == velocity);
        particle.oscillationRandom = std::nextafter (.5f, 1.0f);
        applyParticleControlPointRemap (particle, parsed->remap, {}, false);
        for (int axis = 0; axis < 3; ++axis) {
            CHECK (particle.velocity[axis] == Catch::Approx (sample.nextSeed[axis]).margin (.0002f));
        }
        particle.age = 0;
        applyParticleControlPointRemap (particle, parsed->remap, {}, false);
        CHECK (particle.velocity == sample.midpoint);
    }
}

TEST_CASE ("seeded remap noise preserves native hash bits and lattice boundaries", "[particle][remap][remap-noise]") {
    using WallpaperEngine::Render::Utils::seededSimplexNoise2D;
    // Direct execution of native 1400fbe90, binary SHA256 40e2ce02...993b0.
    // These differ from the permutation-based simplex used by other operators.
    const struct { uint32_t seed; float x, y, expected; } cases[] = {
        { 0u, 0, 0, 0 },
        { 0u, .25f, 0, .8035542369f },
        { 0x3f000000u, 2.5f, 0, -.5152724385f },
        { 0x3f000001u, 2.5f, 0, .07524833828f },
        { 0x3f000000u, -1.25f, 0, -.1699448228f },
        { 0xffffffffu, .25f, -.5f, -.5190694928f },
        { 0x80000000u, 1, 0, .3829292357f },
        { 0x3f000000u, 2.5f, .1f, -.6222580075f },
        { 0x3f000000u, .25f, .25f, .7642583847f },
        { 0x3f000000u, .9999998808f, 0, .3829290569f },
        { 0x3f000000u, 1.000000119f, 0, .3829295337f }
    };
    for (const auto& sample : cases) {
        CAPTURE (sample.seed, sample.x, sample.y);
        CHECK (seededSimplexNoise2D (sample.seed, sample.x, sample.y)
               == Catch::Approx (sample.expected).margin (.000001f));
    }
}

TEST_CASE ("rain FBM remaps clamp speed multiplication without changing direction", "[particle][remap][remap-noise]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Utils::seededFbmNoise2D;
    const struct { uint32_t seed; float x, y, expected; } kernelCases[] = {
        { 0x3f000000u, 1, 0, -.04425158352f },
        { 0x3f000001u, 2, 0, -.5066438913f },
        { 0xffffffffu, .25f, -.5f, -.3643205464f },
        { 0u, -2, 0, -.2531152964f }
    };
    for (const auto& sample : kernelCases) {
        CHECK (seededFbmNoise2D (sample.seed, sample.x, sample.y)
               == Catch::Approx (sample.expected).margin (.000001f));
    }
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":384,"particle":{"operator":[{
            "name":"remapvalue","output":"speed","outputrangemin":-5,"outputrangemax":7,
            "transformfunction":"fbmnoise","transforminputscale":8,"flags":3
        }]}})"), project);
    const auto* definition = object->as<Particle> ();
    REQUIRE (definition != nullptr);
    REQUIRE (definition->operators.size () == 1);
    const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
    REQUIRE (parsed != nullptr);
    // Native three-octave wrapper 1400fbc70 at x = lifetime fraction * 8.
    // Its literal outputs map through -5..7, then flags 3 clamp to 0..1.
    const struct { float fraction, random, multiplier; } cases[] = {
        { 0, .5f, 1 },
        { .0625f, .5f, 0 },
        { .125f, .5f, .73449050f },
        { .1875f, .5f, .08027795f },
        { .25f, .5f, 1 },
        { .25f, std::nextafter (.5f, 1.0f), 0 }
    };
    for (const auto& sample : cases) {
        CAPTURE (sample.fraction, sample.random);
        ParticleInstance particle;
        particle.age = sample.fraction * 8;
        particle.lifetime = 8;
        particle.oscillationRandom = sample.random;
        particle.velocity = { -30, 40, -50 };
        applyParticleControlPointRemap (particle, parsed->remap, {}, false);
        const glm::vec3 expected = glm::vec3 (-30, 40, -50) * sample.multiplier;
        for (int axis = 0; axis < 3; ++axis) {
            CHECK (particle.velocity[axis] == Catch::Approx (expected[axis]).margin (.0001f));
        }
        CHECK (particle.oscillationRandom == sample.random);
        particle.velocity = glm::vec3 (0);
        applyParticleControlPointRemap (particle, parsed->remap, {}, false);
        CHECK (particle.velocity == glm::vec3 (0));
    }
}

TEST_CASE ("between control point constraints retain the stock operator and authored envelopes", "[particle][maintain-between-points]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":391,"particle":{"operator":[
            {"id":10,"name":"maintaindistancebetweencontrolpoints"},
            {"name":"maintaindistancebetweencontrolpoints","controlpointstart":2,"controlpointend":3},
            {"name":"maintaindistancebetweencontrolpoints","controlpointstart":-1,"controlpointend":99,
             "blendinstart":0,"blendinend":0.5,"blendoutstart":0.5,"blendoutend":1},
            {"name":"maintaindistancebetweencontrolpoints","controlpointstart":null,"controlpointend":null}
        ]}})"), project);
    const auto* definition = object->as<Particle> ();
    REQUIRE (definition != nullptr);
    REQUIRE (definition->operators.size () == 4);
    const uint32_t starts[] = { 0, 2, 7, 0 };
    const uint32_t ends[] = { 1, 3, 7, 0 };
    for (size_t index = 0; index < definition->operators.size (); ++index) {
        const auto* op = definition->operators[index]->as<MaintainDistanceBetweenControlPointsOperator> ();
        REQUIRE (op != nullptr);
        CHECK (op->controlPointStart == starts[index]);
        CHECK (op->controlPointEnd == ends[index]);
        CHECK (op->blendTimes == (index == 2 ? glm::vec4 (0, .5f, .5f, 1) : glm::vec4 (0, 0, 1, 1)));
    }
}

TEST_CASE ("between point constraints weight translation and longitudinal correction together", "[particle][maintain-between-points]") {
    using WallpaperEngine::Render::Objects::applyParticleBetweenControlPointConstraint;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct {
        glm::vec3 previousStart, previousEnd, currentStart, currentEnd, position, expected;
    } cases[] = {
        { { 0, 0, 0 }, { 64, 0, 0 }, { 0, 0, 0 }, { 64, 0, 0 }, { 16, 12, -8 }, { 16, 12, -8 } },
        { { 0, 0, 0 }, { 64, 0, 0 }, { 0, 0, 0 }, { 64, 0, 0 }, { -16, 12, -8 }, { 0, 12, -8 } },
        { { 0, 0, 0 }, { 64, 0, 0 }, { 0, 0, 0 }, { 64, 0, 0 }, { 80, 12, -8 }, { 64, 12, -8 } },
        { { 0, 0, 0 }, { 64, 0, 0 }, { 8, -4, 12 }, { 72, -4, 12 }, { 16, 12, -8 }, { 24, 8, 4 } },
        { { 0, 0, 0 }, { 64, 0, 0 }, { 0, 0, 0 }, { 128, 0, 0 }, { 16, 12, -8 }, { 32, 12, -8 } },
        { { 0, 0, 0 }, { 64, 0, 0 }, { 0, 0, 0 }, { 0, 64, 0 }, { 16, 12, -8 }, { 0, 28, -8 } },
        { { 4, -8, 12 }, { 68, 56, 76 }, { 12, 4, -8 }, { 140, 132, 120 }, { 28, 0, 28 }, { 52, 28, 24 } },
        { { 0, 0, 0 }, { 64, 0, 0 }, { 64, 0, 0 }, { 0, 0, 0 }, { 16, 12, -8 }, { 48, 12, -8 } }
    };
    // Unchanged native geometry/writeback at 140242437..676 produced these
    // independent finite-input targets within .014 (RSQRTPS approximation).
    for (const auto& sample : cases) {
        for (const float weight : { 0.0f, .25f, .5f, .75f, 1.0f }) {
            ControlPointData start, end;
            start.sampleVelocity (sample.previousStart, .1f);
            start.sampleVelocity (sample.currentStart, .1f);
            start.position = sample.currentStart;
            end.sampleVelocity (sample.previousEnd, .1f);
            end.sampleVelocity (sample.currentEnd, .1f);
            end.position = sample.currentEnd;
            std::vector<ParticleInstance> particles (2);
            particles[0].alive = true;
            particles[0].position = sample.position;
            particles[0].velocity = { 7, -11, 13 };
            particles[0].lifetime = 8;
            particles[0].age = 2;
            particles[1].position = { 90, 80, 70 };
            const glm::vec4 envelope = weight == 0 ? glm::vec4 (.5f, .75f, 1, 1)
                : glm::vec4 (0, .25f / weight, 1, 1);
            applyParticleBetweenControlPointConstraint (particles, 2, start, end, envelope);
            const glm::vec3 expected = sample.position + (sample.expected - sample.position) * weight;
            for (int axis = 0; axis < 3; ++axis) {
                CHECK (particles[0].position[axis] == Catch::Approx (expected[axis]).margin (.0001f));
            }
            CHECK (particles[0].velocity == glm::vec3 (7, -11, 13));
            CHECK (particles[0].age == 2);
            CHECK (particles[1].position == glm::vec3 (90, 80, 70));
            CHECK (start.previousPosition == sample.currentStart);
            CHECK (end.previousPosition == sample.currentEnd);
        }
    }
}

TEST_CASE ("between point constraints use both fades and skip short segments", "[particle][maintain-between-points]") {
    using WallpaperEngine::Render::Objects::applyParticleBetweenControlPointConstraint;
    using WallpaperEngine::Render::Objects::ControlPointData;
    ControlPointData start, end;
    end.position = { 64, 0, 0 };
    const struct { float fraction; float expectedX; } fades[] = {
        { 0, 80 }, { .25f, 72 }, { .5f, 64 }, { .75f, 72 }, { 1, 80 }
    };
    for (const float lifetime : { 4.0f, 8.0f }) {
        for (const auto& sample : fades) {
            std::vector<ParticleInstance> particles (2);
            particles[0].alive = particles[1].alive = true;
            particles[0].position = particles[1].position = { 80, 12, -8 };
            particles[0].lifetime = lifetime;
            particles[0].age = lifetime * sample.fraction;
            applyParticleBetweenControlPointConstraint (particles, 1, start, end, { 0, .5f, .5f, 1 });
            CHECK (particles[0].position == glm::vec3 (sample.expectedX, 12, -8));
            CHECK (particles[1].position == glm::vec3 (80, 12, -8));
        }
    }
    constexpr float epsilon = std::numeric_limits<float>::epsilon ();
    const struct { glm::vec3 currentEnd, endDisplacement; } small[] = {
        { { 0, 0, 0 }, { 0, 0, 0 } },
        { { 64, 0, 0 }, { 64, 0, 0 } },
        { { 0, 0, 0 }, { -64, 0, 0 } },
        { { epsilon, 0, 0 }, { 0, 0, 0 } },
        { { 2 * epsilon, 0, 0 }, { epsilon, 0, 0 } }
    };
    for (const auto& sample : small) {
        end.position = sample.currentEnd;
        end.displacement = sample.endDisplacement;
        std::vector<ParticleInstance> particles (1);
        particles[0].alive = true;
        particles[0].position = { 80, 12, -8 };
        applyParticleBetweenControlPointConstraint (particles, 1, start, end, { 0, 0, 1, 1 });
        CHECK (particles[0].position == glm::vec3 (80, 12, -8));
    }
}

TEST_CASE ("vortex orb retains its distance constraint and native point defaults", "[particle][maintain-distance]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":17,"particle":{"operator":[
            {"id":12,"name":"maintaindistancetocontrolpoint","variablestrength":5},
            {"name":"maintaindistancetocontrolpoint","controlpoint":-1,"distance":20,
             "blendinstart":-1,"blendinend":1},
            {"name":"maintaindistancetocontrolpoint","distance":null}
        ]}})"), project);
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 3);
    const auto* stock = particle->operators[0]->as<MaintainDistanceToControlPointOperator> ();
    REQUIRE (stock != nullptr);
    CHECK (stock->controlPoint == 0);
    CHECK (stock->distance == nullptr);
    CHECK (stock->variableStrength->value->getFloat () == 5);
    CHECK (stock->blendTimes == glm::vec4 (0, 0, 1, 1));
    const auto* clamped = particle->operators[1]->as<MaintainDistanceToControlPointOperator> ();
    REQUIRE (clamped != nullptr);
    CHECK (clamped->controlPoint == 7);
    CHECK (clamped->distance->value->getFloat () == 20);
    CHECK (clamped->variableStrength->value->getFloat () == 0);
    CHECK (clamped->blendTimes == glm::vec4 (-1, 1, 1, 1));
    const auto* explicitZero = particle->operators[2]->as<MaintainDistanceToControlPointOperator> ();
    REQUIRE (explicitZero != nullptr);
    REQUIRE (explicitZero->distance != nullptr);
    CHECK (explicitZero->distance->value->getFloat () == 0);
}

TEST_CASE ("distance constraints blend radial correction but retain point movement", "[particle][maintain-distance]") {
    using WallpaperEngine::Render::Objects::applyParticleDistanceConstraint;
    using WallpaperEngine::Render::Objects::ControlPointData;
    // Native 40e2ce02...993b0: one .05 second step from x10, point x50,
    // distance20 yields x30/full, x20/half, x10/inactive, x15/strength5.
    const struct { float strength; glm::vec4 blend; float expected; } cases[] = {
        { 0, { 0, 0, 1, 1 }, 30 }, { 0, { -1, 1, 1, 1 }, 20 },
        { 0, { .2f, .5f, 1, 1 }, 10 }, { 5, { 0, 0, 1, 1 }, 15 },
        { -5, { 0, 0, 1, 1 }, 10 }, { 50, { 0, 0, 1, 1 }, 30 }
    };
    ControlPointData point;
    point.position = { 50, 0, 0 };
    point.sampleVelocity ({ 50, 0, 0 }, .05f);
    CHECK (point.displacement == glm::vec3 (0));
    for (const auto& sample : cases) {
        ParticleInstance particle;
        particle.position = { 10, 0, 0 };
        particle.velocity = { 4, 5, 6 };
        applyParticleDistanceConstraint (particle, point, glm::mat3 (1), 20, sample.strength, .05f, sample.blend);
        CHECK (particle.position.x == Catch::Approx (sample.expected));
        CHECK (particle.position.y == 0);
        CHECK (particle.position.z == 0);
        CHECK (particle.velocity == glm::vec3 (4, 5, 6));
    }
    point.position = { 60, 0, 0 };
    point.sampleVelocity ({ 60, 0, 0 }, .02f);
    CHECK (point.displacement == glm::vec3 (10, 0, 0));
    ParticleInstance moved;
    moved.position = { 10, 0, 0 };
    applyParticleDistanceConstraint (moved, point, glm::mat3 (1), 20, 0, .05f, { .2f, .5f, 1, 1 });
    CHECK (moved.position == glm::vec3 (20, 0, 0));
    point.sampleVelocity ({ 60, 0, 0 }, .02f);
    CHECK (point.displacement == glm::vec3 (0));
    // The native inverse current basis keeps authored distance in point space.
    ParticleInstance scaled;
    scaled.position = { 20, 0, 0 };
    glm::mat3 inverseBasis (1);
    inverseBasis[0][0] = .5f;
    applyParticleDistanceConstraint (scaled, point, inverseBasis, 10, 0, .05f, { 0, 0, 1, 1 });
    CHECK (scaled.position == glm::vec3 (40, 0, 0));
}

TEST_CASE ("event color defaults select per-step color and opacity inheritance", "[particle][event-color]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":17,"particle":{"operator":[
            {"name":"inheritvaluefromevent"},
            {"name":"inheritvaluefromevent","input":"setcoloropacity","blendinstart":-1,"blendinend":1},
            {"name":"inheritvaluefromevent","input":"multiplyvelocity"}
        ]}})"), project);
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->operators.size () == 2);
    const auto* full = particle->operators[0]->as<InheritEventColorOperator> ();
    const auto* half = particle->operators[1]->as<InheritEventColorOperator> ();
    REQUIRE (full != nullptr);
    REQUIRE (half != nullptr);
    CHECK (full->blendTimes == glm::vec4 (0, 0, 1, 1));
    CHECK (half->blendTimes == glm::vec4 (-1, 1, 1, 1));
}

TEST_CASE ("event color blends current parent RGBA only while the parent lives", "[particle][event-color]") {
    using WallpaperEngine::Render::Objects::applyParticleEventColor;
    ParticleInstance parent;
    parent.alive = true;
    parent.color = { .1f, .2f, .3f };
    parent.alpha = .25f;
    parent.initial.color = { .9f, .8f, .7f };
    parent.initial.alpha = 1;
    for (const auto& blend : { glm::vec4 (0, 0, 1, 1), glm::vec4 (-1, 1, 1, 1), glm::vec4 (.2f, .5f, 1, 1) }) {
        ParticleInstance child;
        child.alive = true;
        child.color = { .8f, .6f, .4f };
        child.alpha = .75f;
        const float weight = blend.x == -1 ? .5f : blend.x == 0 ? 1.0f : 0.0f;
        applyParticleEventColor (child, &parent, blend);
        const glm::vec3 expected = glm::mix (glm::vec3 (.8f, .6f, .4f), parent.color, weight);
        CHECK (child.color.x == Catch::Approx (expected.x));
        CHECK (child.color.y == Catch::Approx (expected.y));
        CHECK (child.color.z == Catch::Approx (expected.z));
        CHECK (child.alpha == Catch::Approx (.75f - .5f * weight));
        CHECK (child.initial.color == glm::vec3 (1));
        CHECK (child.initial.alpha == 1);
        CHECK (child.velocity == glm::vec3 (0));
    }
    ParticleInstance untouched;
    untouched.alive = true;
    applyParticleEventColor (untouched, nullptr, { 0, 0, 1, 1 });
    CHECK (untouched.color == glm::vec3 (1));
    CHECK (untouched.alpha == 1);
    parent.age = parent.lifetime;
    applyParticleEventColor (untouched, &parent, { 0, 0, 1, 1 });
    CHECK (untouched.color == glm::vec3 (1));
    CHECK (untouched.alpha == 1);
    parent.age = 0;
    parent.alive = false;
    applyParticleEventColor (untouched, &parent, { 0, 0, 1, 1 });
    CHECK (untouched.color == glm::vec3 (1));
    CHECK (untouched.alpha == 1);
}

TEST_CASE ("birth event inheritance defaults to color without opacity", "[particle][event-initial-color]") {
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":17,"particle":{"initializer":[
            {"name":"inheritinitialvaluefromevent"},
            {"name":"inheritinitialvaluefromevent","input":"setcoloropacity"},
            {"name":"inheritinitialvaluefromevent","input":"setcolor"},
            {"name":"inheritinitialvaluefromevent","input":"multiplycolor"}
        ]}})"), project);
    const auto* particle = object->as<Particle> ();
    REQUIRE (particle != nullptr);
    REQUIRE (particle->initializers.size () == 3);
    for (size_t index = 0; index < 3; ++index) {
        const auto* initializer = particle->initializers[index]->as<InheritInitialEventColorInitializer> ();
        REQUIRE (initializer != nullptr);
        CHECK (initializer->copyOpacity == (index == 1));
    }
}

TEST_CASE ("birth event color records current parent values once", "[particle][event-initial-color]") {
    using WallpaperEngine::Render::Objects::initializeParticleEventColor;
    ParticleInstance parent;
    parent.color = { .1f, .2f, .3f };
    parent.alpha = .25f;
    parent.initial.color = { .9f, .8f, .7f };
    parent.initial.alpha = 1;
    for (const bool copyOpacity : { false, true }) {
        ParticleInstance child;
        child.color = child.initial.color = { .8f, .6f, .4f };
        child.alpha = child.initial.alpha = .75f;
        // Native birth opcode16 has no alive/lifetime guard on a retained parent record.
        initializeParticleEventColor (child, &parent, copyOpacity);
        CHECK (child.color == parent.color);
        CHECK (child.initial.color == parent.color);
        CHECK (child.alpha == (copyOpacity ? .25f : .75f));
        CHECK (child.initial.alpha == child.alpha);
        CHECK (child.velocity == glm::vec3 (0));
        CHECK (child.lifetime == 1);
    }
    ParticleInstance child;
    initializeParticleEventColor (child, &parent, true);
    parent.color = { .7f, .8f, .9f };
    parent.alpha = .5f;
    CHECK (child.initial.color == glm::vec3 (.1f, .2f, .3f));
    CHECK (child.initial.alpha == .25f);
    initializeParticleEventColor (child, nullptr, true);
    CHECK (child.initial.color == glm::vec3 (.1f, .2f, .3f));
    CHECK (child.initial.alpha == .25f);
}

TEST_CASE ("two-control-point projection can remap its own position fraction", "[particle][remap][remap-position-pair]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { const char* operation; glm::vec3 full, half; } cases[] = {
        { "remap", { 10, 49, -9 }, { 6, 41, -5 } },
        { "multiply", { -2, 25, 3 }, { 0, 29, 1 } },
        { "add", { 18, 65, -17 }, { 10, 49, -9 } },
        { "subtract", { -14, 1, 15 }, { -6, 17, 7 } },
        { nullptr, { -2, 25, 3 }, { 0, 29, 1 } }
    };
    std::vector<ControlPointData> points (4);
    // Input slots deliberately disagree: native scalar projection reads the
    // output pair. P = A + .25 * (B-A) + (6,-3,0), an orthogonal residual.
    points[0].position = { 1000, 0, 0 };
    points[1].position = { 1100, 0, 0 };
    points[2].position = { -12, 20, 7 };
    points[3].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                for (const float age : { 0.0f, 2.0f, 4.0f }) {
                    INFO ("initializer=" << initializer << ", operation="
                          << (sample.operation ? sample.operation : "default") << ", age=" << age);
                    WallpaperEngine::Data::Model::Project project {};
                    auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":395,"particle":{}})");
                    auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                        "input":"positionbetweentwocontrolpoints","output":"positionbetweentwocontrolpoints",
                        "inputcontrolpoint0":0,"inputcontrolpoint1":1,
                        "outputcontrolpoint0":2,"outputcontrolpoint1":3,
                        "outputrangemin":0.25,"outputrangemax":1.25,
                        "blendinstart":0,"blendinend":0.5
                    })");
                    node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                    if (sample.operation) node["operation"] = sample.operation;
                    data["particle"][initializer ? "initializer" : "operator"] = { node };
                    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                    const auto* definition = object->as<Particle> ();
                    REQUIRE (definition != nullptr);
                    REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                    const ControlPointRemap* remap = nullptr;
                    if (initializer) {
                        const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
                        REQUIRE (parsed != nullptr);
                        remap = &parsed->remap;
                    } else {
                        const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                        REQUIRE (parsed != nullptr);
                        remap = &parsed->remap;
                    }
                    ParticleInstance particle;
                    particle.position = { 2, 33, -1 };
                    particle.velocity = { 7, -11, 13 };
                    particle.lifetime = 8;
                    particle.age = age;
                    applyParticleControlPointRemap (particle, *remap, points, initializer);
                    const glm::vec3 expected = initializer || age == 4 ? sample.full
                                            : age == 2 ? sample.half : glm::vec3 (2, 33, -1);
                    CHECK (particle.position == expected);
                    CHECK (particle.velocity == glm::vec3 (7, -11, 13));
                    CHECK (particle.age == age);
                }
            }
        }
    }
}

TEST_CASE ("two-control-point projection position remaps clamp before arithmetic", "[particle][remap][remap-position-pair]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { uint32_t flags; glm::vec3 expected; } cases[] = {
        { 0, { -78, -127, 79 } }, { 1, { -46, -63, 47 } },
        { 2, { -14, 1, 15 } }, { 3, { -14, 1, 15 } }
    };
    std::vector<ControlPointData> points (2);
    points[0].position = { -12, 20, 7 };
    points[1].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                INFO ("initializer=" << initializer << ", flags=" << sample.flags);
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":396,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                    "input":"positionbetweentwocontrolpoints","output":"positionbetweentwocontrolpoints",
                    "operation":"add","outputrangemin":-1,"outputrangemax":3
                })");
                node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                node["flags"] = sample.flags;
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE (definition != nullptr);
                REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                const ControlPointRemap* remap = nullptr;
                if (initializer) {
                    const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
                    REQUIRE (parsed != nullptr);
                    remap = &parsed->remap;
                } else {
                    const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                    REQUIRE (parsed != nullptr);
                    remap = &parsed->remap;
                }
                ParticleInstance particle;
                particle.position = { -14, 1, 15 }; // signed fraction -.25
                particle.lifetime = 8;
                particle.age = 2;
                applyParticleControlPointRemap (particle, *remap, points, initializer);
                CHECK (particle.position == sample.expected);
            }
        }
    }
}

TEST_CASE ("two-control-point projection accepts the native default size output", "[particle][remap][remap-position-size]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { const char* operation; float full, half; } cases[] = {
        { nullptr, 5, 12.5f }, { "remap", .25f, 10.125f },
        { "multiply", 5, 12.5f }, { "add", 20.25f, 20.125f },
        { "subtract", 19.75f, 19.875f }
    };
    std::vector<ControlPointData> points (4);
    points[0].position = { 1000, 0, 0 };
    points[1].position = { 1100, 0, 0 };
    points[2].position = { -12, 20, 7 };
    points[3].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                for (const float age : { 0.0f, 2.0f, 4.0f }) {
                    INFO ("operation=" << (sample.operation ? sample.operation : "default") << ", age=" << age);
                    WallpaperEngine::Data::Model::Project project {};
                    auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":397,"particle":{}})");
                    auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                        "input":"positionbetweentwocontrolpoints",
                        "inputcontrolpoint0":0,"inputcontrolpoint1":1,
                        "outputcontrolpoint0":2,"outputcontrolpoint1":3,
                        "blendinstart":0,"blendinend":0.5
                    })");
                    node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                    if (sample.operation) {
                        node["operation"] = sample.operation;
                        node["output"] = "size";
                    }
                    data["particle"][initializer ? "initializer" : "operator"] = { node };
                    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                    const auto* definition = object->as<Particle> ();
                    REQUIRE (definition != nullptr);
                    REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                    const ControlPointRemap* remap = nullptr;
                    if (initializer) {
                        const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
                        REQUIRE (parsed != nullptr);
                        remap = &parsed->remap;
                    } else {
                        const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                        REQUIRE (parsed != nullptr);
                        remap = &parsed->remap;
                    }
                    ParticleInstance particle;
                    particle.position = { 2, 33, -1 }; // output-pair fraction .25
                    particle.velocity = { 7, -11, 13 };
                    particle.size = 20;
                    particle.initial.size = initializer ? 20 : 96;
                    particle.lifetime = 8;
                    particle.age = age;
                    applyParticleControlPointRemap (particle, *remap, points, initializer);
                    const float expected = initializer || age == 4 ? sample.full : age == 2 ? sample.half : 20;
                    CHECK (particle.size == expected);
                    CHECK (particle.initial.size == (initializer ? sample.full : 96));
                    CHECK (particle.position == glm::vec3 (2, 33, -1));
                    CHECK (particle.velocity == glm::vec3 (7, -11, 13));
                    CHECK (particle.age == age);
                }
            }
        }
    }
}

TEST_CASE ("two-control-point size remaps clamp mapped values before size arithmetic", "[particle][remap][remap-position-size]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { uint32_t flags; float full, half; } cases[] = {
        { 0, -40, -10 }, { 1, -20, 0 }, { 2, 0, 10 }, { 3, 0, 10 }
    };
    std::vector<ControlPointData> points (2);
    points[0].position = { -12, 20, 7 };
    points[1].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                INFO ("flags=" << sample.flags);
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":398,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                    "input":"positionbetweentwocontrolpoints",
                    "outputrangemin":"-1 100 200","outputrangemax":"3 120 240",
                    "blendinstart":0,"blendinend":0.5
                })");
                node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                node["flags"] = sample.flags;
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE (definition != nullptr);
                REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                const ControlPointRemap* remap = nullptr;
                if (initializer) {
                    const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
                    REQUIRE (parsed != nullptr);
                    remap = &parsed->remap;
                } else {
                    const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                    REQUIRE (parsed != nullptr);
                    remap = &parsed->remap;
                }
                ParticleInstance particle;
                particle.position = { -14, 1, 15 }; // signed fraction -.25
                particle.size = 20;
                particle.lifetime = 8;
                particle.age = 2;
                applyParticleControlPointRemap (particle, *remap, points, initializer);
                CHECK (particle.size == (initializer ? sample.full : sample.half));
                CHECK (particle.position == glm::vec3 (-14, 1, 15));
            }
        }
    }
}

TEST_CASE ("two-control-point projection multiplies speed after clamps and envelope", "[particle][remap][remap-position-speed]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { glm::vec3 position; uint32_t flags; float age; glm::vec3 initial, operated; } cases[] = {
        { { 2, 33, -1 }, 1, 4, { 6, -8, 0 }, { 6, -8, 0 } },
        { { 2, 33, -1 }, 1, 2, { 6, -8, 0 }, { 9, -12, 0 } },
        { { 2, 33, -1 }, 1, 0, { 6, -8, 0 }, { 12, -16, 0 } },
        { { -14, 1, 15 }, 0, 2, { -6, 8, 0 }, { 3, -4, 0 } },
        { { -14, 1, 15 }, 1, 2, { 0, 0, 0 }, { 6, -8, 0 } },
        { { 34, 97, -33 }, 0, 2, { 30, -40, 0 }, { 21, -28, 0 } },
        { { 34, 97, -33 }, 2, 2, { 12, -16, 0 }, { 12, -16, 0 } },
        { { 34, 97, -33 }, 3, 2, { 12, -16, 0 }, { 12, -16, 0 } }
    };
    std::vector<ControlPointData> points (4);
    points[0].position = { 1000, 0, 0 };
    points[1].position = { 1100, 0, 0 };
    points[2].position = { -12, 20, 7 };
    points[3].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                INFO ("flags=" << sample.flags << ", age=" << sample.age);
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":399,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                    "input":"positionbetweentwocontrolpoints","output":"speed","outputrangemax":2,
                    "inputcontrolpoint0":0,"inputcontrolpoint1":1,
                    "outputcontrolpoint0":2,"outputcontrolpoint1":3,
                    "blendinstart":0,"blendinend":0.5
                })");
                node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                node["flags"] = sample.flags;
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE (definition != nullptr);
                REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                const ControlPointRemap* remap = nullptr;
                if (initializer) {
                    const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
                    REQUIRE (parsed != nullptr);
                    remap = &parsed->remap;
                } else {
                    const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                    REQUIRE (parsed != nullptr);
                    remap = &parsed->remap;
                }
                ParticleInstance particle;
                particle.position = sample.position;
                particle.velocity = { 12, -16, 0 };
                particle.lifetime = 8;
                particle.age = sample.age;
                applyParticleControlPointRemap (particle, *remap, points, initializer);
                CHECK (particle.velocity == (initializer ? sample.initial : sample.operated));
                CHECK (particle.position == sample.position);
                CHECK (particle.age == sample.age);
                particle.velocity = glm::vec3 (0);
                applyParticleControlPointRemap (particle, *remap, points, initializer);
                CHECK (particle.velocity == glm::vec3 (0));
            }
        }
    }
}

TEST_CASE ("upright sprite basis matches the native locked-axis draw helper", "[particle][upright-sprite]") {
    using WallpaperEngine::Render::Objects::calculateUprightParticleOrientation;
    // Unchanged native 1402298b0 outputs: identity and rotated/nonuniform model,
    // authored Y/Z/oblique axes, each with renderer flags 0 and 1.
    const glm::mat3 expected[] {
        { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { -0.7682212f, -0.384110659f, 0.512147546f }, { 0.267261237f, 0.534522474f, 0.801783741f }, { -0.581728041f, 0.752824545f, -0.307973653f } },
        { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 0.976080835f, 1.68658012e-08f, -0.217408106f }, { -2.31795863e-08f, 0.99999994f, -2.64909552e-08f }, { 0.217408106f, 0.0f, 0.976080835f } },
        { { 0.929739237f, -0.316470265f, 0.188232526f }, { 0.339905024f, 0.934210956f, -0.108233318f }, { -0.137427241f, 0.176026806f, 0.974745274f } },
        { { -0.984472096f, 0.175541148f, 2.61576694e-09f }, { 0.0f, -1.49011612e-08f, 1.0f }, { 0.175541148f, 0.984472096f, 1.17032917e-08f } },
        { { 0.929739237f, -0.316470265f, 0.188232526f }, { 0.339905024f, 0.934210956f, -0.108233318f }, { -0.137427241f, 0.176026806f, 0.974745274f } },
        { { -0.987255275f, 0.157439604f, 0.0232314114f }, { 0.0777909756f, 0.35005942f, 0.933491826f }, { 0.115320608f, 0.808328927f, -0.577326179f } },
        { { 0.929739237f, -0.316470265f, 0.188232526f }, { 0.339905024f, 0.934210956f, -0.108233318f }, { -0.137427241f, 0.176026806f, 0.974745274f } }
    };
    const glm::vec3 axes[] { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 2, 3 } };
    for (int sample = 0; sample < 12; ++sample) {
        INFO ("sample=" << sample);
        glm::mat4 model (1.0f);
        if (sample >= 6) {
            model = glm::rotate (model, .6f, glm::normalize (glm::vec3 (1, 2, 3)))
                * glm::scale (glm::mat4 (1.0f), glm::vec3 (2, 3, 4));
        }
        const auto actual = calculateUprightParticleOrientation (
            axes[(sample % 6) / 2], glm::mat3 (model), { 0, .8f, .6f }, (sample & 1) != 0
        );
        for (int column = 0; column < 3; ++column) {
            for (int row = 0; row < 3; ++row) {
                CHECK (actual[column][row] == Catch::Approx (expected[sample][column][row]).margin (.000002f));
            }
        }
    }
    CHECK (calculateUprightParticleOrientation ({ 0, 0, 0 }, glm::mat3 (1), { 0, .8f, .6f }, false) == expected[0]);
    CHECK (calculateUprightParticleOrientation ({ 0, 7, 0 }, glm::mat3 (1), { 0, .8f, .6f }, false) == expected[0]);
}

TEST_CASE ("two-control-point velocity arithmetic preserves factors and reflects authored deltas", "[particle][remap][remap-position-velocity-arithmetic]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    // Fixed native arithmetic controls, reflected into the fork's velocity space.
    const struct { const char* operation; glm::vec3 full, half, clampedFull, clampedHalf; } cases[] = {
        { "remap", { 2, -3, -4 }, { 3, -4.5f, 2 }, { 1, -1, 0 }, { 2.5f, -3.5f, 4 } },
        { "multiply", { 8, -18, -32 }, { 6, -12, -12 }, { 4, -6, 0 }, { 4, -6, 4 } },
        { "add", { 6, -9, 4 }, { 5, -7.5f, 6 }, { 5, -7, 8 }, { 4.5f, -6.5f, 8 } },
        { "subtract", { 2, -3, 12 }, { 3, -4.5f, 10 }, { 3, -5, 8 }, { 3.5f, -5.5f, 8 } },
        { "", { 8, -18, -32 }, { 6, -12, -12 }, { 4, -6, 0 }, { 4, -6, 4 } },
    };
    std::vector<ControlPointData> points (4);
    points[0].position = { 1000, 0, 0 };
    points[1].position = { 1100, 0, 0 };
    points[2].position = { -12, 20, 7 };
    points[3].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        for (const auto& sample : cases) {
            DYNAMIC_SECTION ("initializer=" << initializer << ", operation=" << sample.operation) {
                for (const uint32_t flags : { 0u, 2u }) {
                    for (const float age : { 0.0f, 2.0f, 4.0f }) {
                        INFO ("flags=" << flags << ", age=" << age);
                        WallpaperEngine::Data::Model::Project project {};
                        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":408,"particle":{}})");
                        auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                            "input":"positionbetweentwocontrolpoints","output":"velocity",
                            "outputrangemin":"1 2 -5","outputrangemax":"5 6 -1",
                            "inputcontrolpoint0":0,"inputcontrolpoint1":1,
                            "outputcontrolpoint0":2,"outputcontrolpoint1":3,
                            "blendinstart":0,"blendinend":0.5
                        })");
                        node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                        node["flags"] = flags;
                        if (*sample.operation) node["operation"] = sample.operation;
                        data["particle"][initializer ? "initializer" : "operator"] = { node };
                        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                        const auto* definition = object->as<Particle> ();
                        REQUIRE (definition != nullptr);
                        REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                        const ControlPointRemap* remap = nullptr;
                        if (initializer) {
                            const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
                            REQUIRE (parsed != nullptr);
                            remap = &parsed->remap;
                        } else {
                            const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                            REQUIRE (parsed != nullptr);
                            remap = &parsed->remap;
                        }
                        ParticleInstance particle;
                        particle.position = { 2, 33, -1 }; // Signed output-pair projection = .25.
                        particle.velocity = { 4, -6, 8 };
                        particle.lifetime = 8;
                        particle.age = age;
                        applyParticleControlPointRemap (particle, *remap, points, initializer);
                        const auto full = flags == 0 ? sample.full : sample.clampedFull;
                        const auto half = flags == 0 ? sample.half : sample.clampedHalf;
                        auto expected = initializer || age == 4 ? full : age == 2 ? half : glm::vec3 (4, -6, 8);
                        if (!initializer && remap->operation == ControlPointRemap::Operation::Remap) {
                            const auto nativeFull = flags == 0 ? glm::vec3 (2, -2, 2) : glm::vec3 (1, -1, 1);
                            const auto nativeHalf = flags == 0 ? glm::vec3 (3, -4, 5) : glm::vec3 (2.5f, -3.5f, 4.5f);
                            expected = age == 4 ? nativeFull : age == 2 ? nativeHalf : glm::vec3 (4, -6, 8);
                        }
                        CHECK (particle.velocity == expected);
                        CHECK (particle.position == glm::vec3 (2, 33, -1));
                        CHECK (particle.age == age);
                    }
                }
            }
        }
    }
}

TEST_CASE ("blended all-component velocity remap reuses mapped X", "[particle][remap][remap-velocity-x-reuse]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { bool initializer; glm::vec4 envelope; float age; uint32_t flags; glm::vec3 expected; } cases[] = {
        { true, { 0, .5f, 1, 1 }, 25, 1, { 20, -40, 60 } },
        { false, { 0, 0, 1, 1 }, 25, 1, { 20, -40, 60 } },
        { false, { 0, .5f, 1, 1 }, 0, 1, { 8, -12, 16 } },
        { false, { 0, .5f, 1, 1 }, 25, 1, { 14, -16, 18 } },
        { false, { 0, .5f, 1, 1 }, 50, 1, { 20, -20, 20 } },
        { false, { 0, 0, .5f, 1 }, 75, 1, { 14, -16, 18 } },
        { false, { .25f, .25f, .25f, .25f }, 25, 1, { 20, -40, 60 } },
        { false, { 0, .01f, 1, 1 }, 25, 1, { 20, -40, 60 } },
        { false, { 0, .5f, 1, 1 }, 25, 3, { 4.5f, -6.5f, 8.5f } },
    };
    std::vector<ControlPointData> points (2);
    points[0].position = { -60, 0, 0 };
    points[1].position = { 60, 0, 0 };
    for (const auto& sample : cases) {
        INFO ("initializer=" << sample.initializer << ", age=" << sample.age << ", flags=" << sample.flags);
        WallpaperEngine::Data::Model::Project project {};
        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":409,"particle":{}})");
        auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
            "input":"positionbetweentwocontrolpoints","output":"velocity",
            "operation":"remap","outputrangemax":"80 160 240"
        })");
        node["name"] = sample.initializer ? "remapinitialvalue" : "remapvalue";
        node["blendinstart"] = sample.envelope.x;
        node["blendinend"] = sample.envelope.y;
        node["blendoutstart"] = sample.envelope.z;
        node["blendoutend"] = sample.envelope.w;
        node["flags"] = sample.flags;
        data["particle"][sample.initializer ? "initializer" : "operator"] = { node };
        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
        const auto* definition = object->as<Particle> ();
        REQUIRE (definition != nullptr);
        REQUIRE ((sample.initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
        const auto& remap = sample.initializer
            ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
            : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
        ParticleInstance particle;
        particle.position = { -30, -45, 40 };
        particle.velocity = { 8, -12, 16 };
        particle.lifetime = 100;
        particle.age = sample.age;
        applyParticleControlPointRemap (particle, remap, points, sample.initializer);
        CHECK (particle.velocity == sample.expected);
        CHECK (particle.position == glm::vec3 (-30, -45, 40));
        CHECK (particle.age == sample.age);
    }
}

TEST_CASE ("two-control-point opacity remaps preserve birth alpha and blend working alpha", "[particle][remap][remap-position-opacity]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { const char* operation; float full, half; } cases[] = {
        { nullptr, .1875f, .46875f }, { "remap", .25f, .5f },
        { "multiply", .1875f, .46875f }, { "add", 1, .875f },
        { "subtract", .5f, .625f }
    };
    std::vector<ControlPointData> points (4);
    points[0].position = { 1000, 0, 0 };
    points[1].position = { 1100, 0, 0 };
    points[2].position = { -12, 20, 7 };
    points[3].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                for (const float age : { 0.0f, 2.0f, 4.0f }) {
                    INFO ("operation=" << (sample.operation ? sample.operation : "default") << ", age=" << age);
                    WallpaperEngine::Data::Model::Project project {};
                    auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":414,"particle":{}})");
                    auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                        "input":"positionbetweentwocontrolpoints","output":"opacity",
                        "inputcontrolpoint0":0,"inputcontrolpoint1":1,
                        "outputcontrolpoint0":2,"outputcontrolpoint1":3,
                        "blendinstart":0,"blendinend":0.5
                    })");
                    node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                    if (sample.operation) {
                        node["operation"] = sample.operation;
                        node["output"] = "opacity";
                    }
                    data["particle"][initializer ? "initializer" : "operator"] = { node };
                    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                    const auto* definition = object->as<Particle> ();
                    REQUIRE (definition != nullptr);
                    REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                    const ControlPointRemap* remap = nullptr;
                    if (initializer) {
                        const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
                        REQUIRE (parsed != nullptr);
                        remap = &parsed->remap;
                    } else {
                        const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                        REQUIRE (parsed != nullptr);
                        remap = &parsed->remap;
                    }
                    ParticleInstance particle;
                    particle.position = { 2, 33, -1 }; // output-pair fraction .25
                    particle.velocity = { 7, -11, 13 };
                    particle.alpha = .75f;
                    particle.initial.alpha = initializer ? .75f : .125f;
                    particle.lifetime = 8;
                    particle.age = age;
                    applyParticleControlPointRemap (particle, *remap, points, initializer);
                    const float expected = initializer || age == 4 ? sample.full : age == 2 ? sample.half : .75f;
                    CHECK (particle.alpha == expected);
                    CHECK (particle.initial.alpha == (initializer ? sample.full : .125f));
                    CHECK (particle.position == glm::vec3 (2, 33, -1));
                    CHECK (particle.velocity == glm::vec3 (7, -11, 13));
                    CHECK (particle.age == age);
                }
            }
        }
    }
}

TEST_CASE ("two-control-point opacity remaps clamp mapped values before alpha arithmetic", "[particle][remap][remap-position-opacity]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { uint32_t flags; float full, half; } cases[] = {
        { 0, -1.5f, -.375f }, { 1, -.75f, 0 }, { 2, 0, .375f }, { 3, 0, .375f }
    };
    std::vector<ControlPointData> points (2);
    points[0].position = { -12, 20, 7 };
    points[1].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                INFO ("flags=" << sample.flags);
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":415,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                    "input":"positionbetweentwocontrolpoints","output":"opacity",
                    "outputrangemin":"-1 100 200","outputrangemax":"3 120 240",
                    "blendinstart":0,"blendinend":0.5
                })");
                node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                node["flags"] = sample.flags;
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE (definition != nullptr);
                REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                const ControlPointRemap* remap = nullptr;
                if (initializer) {
                    const auto* parsed = definition->initializers.front ()->as<ControlPointRemapInitializer> ();
                    REQUIRE (parsed != nullptr);
                    remap = &parsed->remap;
                } else {
                    const auto* parsed = definition->operators.front ()->as<ControlPointRemapOperator> ();
                    REQUIRE (parsed != nullptr);
                    remap = &parsed->remap;
                }
                ParticleInstance particle;
                particle.position = { -14, 1, 15 }; // signed fraction -.25
                particle.alpha = .75f;
                particle.lifetime = 8;
                particle.age = 2;
                applyParticleControlPointRemap (particle, *remap, points, initializer);
                CHECK (particle.alpha == (initializer ? sample.full : sample.half));
                CHECK (particle.position == glm::vec3 (-14, 1, 15));
            }
        }
    }
}

TEST_CASE ("degenerate two-control-point inputs still map scalar and velocity outputs", "[particle][remap][remap-position-degenerate]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    // Native initializer selects zero through FLT_EPSILON; operators project
    // every positive squared length. Coincident/underflowed input selects zero.
    const struct { const char* name; float distance; float initial, ordinary, blended; float transverse {}; } cases[] = {
        { "coincident", 0, .25f, .25f, .525f },
        { "below epsilon", 0x1p-24f, .25f, .375f, .5875f },
        { "at epsilon", 0x1p-23f, .25f, .375f, .5875f },
        { "above epsilon", 0x1p-22f, .375f, .375f, .5875f },
        { "rounded epsilon", 0x1p-23f, .25f, .375f, .5875f, 3 * 0x1p-36f },
        { "normal pair", 64, .375f, .375f, .5875f },
        { "reversed tiny", -0x1p-24f, .25f, .125f, .4625f },
        { "squared underflow", 0x1p-80f, .25f, .25f, .525f }
    };
    std::vector<ControlPointData> points (4);
    points[0].position = { 1024, 512, -256 };
    points[1].position = { 1088, 512, -256 }; // authored input pair is irrelevant
    for (const auto& sample : cases) {
        points[2].position = { 0, sample.transverse == 0 ? 8 : 0, -3 };
        points[3].position = points[2].position + glm::vec3 (sample.distance, sample.transverse, 0);
        for (const bool initializer : { true, false }) {
            for (const bool blended : { false, true }) {
                if (initializer && blended) continue;
                for (const char* output : { "size", "opacity", "velocity", "speed" }) {
                    INFO (sample.name << ", initializer=" << initializer << ", blended=" << blended << ", output=" << output);
                    WallpaperEngine::Data::Model::Project project {};
                    auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":416,"particle":{}})");
                    auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                        "input":"positionbetweentwocontrolpoints","operation":"remap",
                        "inputcontrolpoint0":0,"inputcontrolpoint1":1,
                        "outputcontrolpoint0":2,"outputcontrolpoint1":3,
                        "outputrangemin":0.25,"outputrangemax":0.75,"flags":0
                    })");
                    node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                    node["output"] = output;
                    const bool speed = std::string_view (output) == "speed";
                    if (speed) node["operation"] = "multiply";
                    if (blended) node["blendinend"] = .5f;
                    data["particle"][initializer ? "initializer" : "operator"] = { node };
                    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                    const auto* definition = object->as<Particle> ();
                    REQUIRE (definition != nullptr);
                    REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                    const auto& remap = initializer
                        ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                        : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
                    ParticleInstance particle;
                    particle.position = { std::abs (sample.distance) * .25f, sample.transverse == 0 ? 20 : sample.transverse * .25f, -11 };
                    particle.velocity = { 4, -8, 12 };
                    particle.size = particle.alpha = .8f;
                    particle.initial.size = particle.initial.alpha = .6f;
                    particle.lifetime = 8;
                    particle.age = 2;
                    const glm::vec3 position = particle.position;
                    applyParticleControlPointRemap (particle, remap, points, initializer);
                    const float mapped = initializer ? sample.initial : sample.ordinary;
                    const float scalar = blended ? sample.blended : mapped;
                    CHECK (particle.size == Catch::Approx (std::string_view (output) == "size" ? scalar : .8f));
                    CHECK (particle.alpha == Catch::Approx (std::string_view (output) == "opacity" ? scalar : .8f));
                    CHECK (particle.initial.size == Catch::Approx (initializer && std::string_view (output) == "size" ? scalar : .6f));
                    CHECK (particle.initial.alpha == Catch::Approx (initializer && std::string_view (output) == "opacity" ? scalar : .6f));
                    glm::vec3 velocity { 4, -8, 12 };
                    if (speed) velocity *= blended ? (1 + mapped) * .5f : mapped;
                    else if (std::string_view (output) == "velocity") {
                        const glm::vec3 target (mapped, -mapped, mapped);
                        velocity = blended ? (velocity + target) * .5f : target;
                    }
                    CHECK (particle.velocity.x == Catch::Approx (velocity.x));
                    CHECK (particle.velocity.y == Catch::Approx (velocity.y));
                    CHECK (particle.velocity.z == Catch::Approx (velocity.z));
                    CHECK (particle.position == position);
                    CHECK (particle.age == 2);
                }
            }
        }
    }
}

TEST_CASE ("tiny initializer position outputs retain native unnormalized reconstruction", "[particle][remap][remap-position-tiny-output]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    // Native instruction readbacks amplify the tiny displacement with finite
    // mapped values, distinguishing reconstruction from the previous skip.
    const struct { const char* name; float distance, mapped, multiplier; float replace, subtract; } cases[] = {
        { "epsilon", 0x1p-23f, 0x1p46f, 0x1p69f, 1.25f, -.75f },
        { "below epsilon", 0x1p-24f, 0x1p48f, 0x1p72f, 1.25f, -.75f },
        { "reversed epsilon", -0x1p-23f, 0x1p46f, 0x1p69f, -.75f, 1.25f },
        { "coincident", 0, .5f, .5f, .25f, .25f }
    };
    for (const auto& sample : cases) {
        for (const char* input : { "positionbetweentwocontrolpoints", "maxlifetime" }) {
            for (const char* operation : { "remap", "multiply", "add", "subtract" }) {
                INFO (sample.name << ", input=" << input << ", operation=" << operation);
                const bool multiply = std::string_view (operation) == "multiply";
                const float mapped = multiply ? sample.multiplier : sample.mapped;
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":417,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                    "name":"remapinitialvalue","output":"positionbetweentwocontrolpoints",
                    "inputcontrolpoint0":0,"inputcontrolpoint1":1,
                    "outputcontrolpoint0":2,"outputcontrolpoint1":3,"flags":0
                })");
                node["input"] = input;
                node["operation"] = operation;
                node["outputrangemin"] = mapped;
                node["outputrangemax"] = mapped;
                data["particle"]["initializer"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE (definition != nullptr);
                REQUIRE (definition->initializers.size () == 1);
                const auto& remap = definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap;
                std::vector<ControlPointData> points (4);
                points[0].position = { 1024, 512, -256 };
                points[1].position = { 1088, 512, -256 };
                points[2].position = { 0, 8, -3 };
                points[3].position = { sample.distance, 8, -3 };
                ParticleInstance particle;
                particle.position = { .25f, 12, -8 };
                particle.velocity = { 4, -8, 12 };
                particle.size = particle.alpha = .8f;
                particle.lifetime = 8;
                applyParticleControlPointRemap (particle, remap, points, true);
                const float expected = sample.distance == 0 ? .25f : multiply ? .5f
                    : std::string_view (operation) == "subtract" ? sample.subtract : sample.replace;
                CHECK (particle.position == glm::vec3 (expected, 12, -8));
                CHECK (particle.velocity == glm::vec3 (4, -8, 12));
                CHECK (particle.size == .8f);
                CHECK (particle.alpha == .8f);
                CHECK (particle.lifetime == 8);
            }
        }
    }
}

TEST_CASE ("initializer tiny position output uses rounded length and independent zero input", "[particle][remap][remap-position-tiny-output]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { const char* name; glm::vec3 start, end, position, expected; float minimum, maximum; } cases[] = {
        { "rounded epsilon", { 0, 0, 0 }, { 0x1p-23f, 3 * 0x1p-36f, 0 }, { 0, 0, 12 },
          { 1, 3 * 0x1p-13f, 12 }, 0x1p46f, 0x1p46f },
        { "translated oblique", { 0, 0, 8 }, { 3 * 0x1p-27f, 4 * 0x1p-27f, 8 }, { .25f, -.1875f, 12 },
          { .85f, .6125f, 12 }, 0x1p54f / 25, 0x1p54f / 25 },
        { "above epsilon", { 0, 0, 0 }, { 0x1p-22f, 0, 0 }, { .25f, 12, -8 },
          { 0x1p-23f, 12, -8 }, .5f, .5f },
        { "zero input differs from output projection", { 0, 0, 0 }, { 0x1p-23f, 0, 0 }, { 0x1p23f, 12, -8 },
          { 0x1p23f + 1, 12, -8 }, 0x1p46f, 0x1p47f }
    };
    for (const auto& sample : cases) {
        INFO (sample.name);
        WallpaperEngine::Data::Model::Project project {};
        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":418,"particle":{}})");
        auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
            "name":"remapinitialvalue","input":"positionbetweentwocontrolpoints",
            "output":"positionbetweentwocontrolpoints","operation":"remap","flags":0
        })");
        node["outputrangemin"] = sample.minimum;
        node["outputrangemax"] = sample.maximum;
        data["particle"]["initializer"] = { node };
        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
        const auto* definition = object->as<Particle> ();
        REQUIRE (definition != nullptr);
        REQUIRE (definition->initializers.size () == 1);
        const auto& remap = definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap;
        std::vector<ControlPointData> points (2);
        points[0].position = sample.start;
        points[1].position = sample.end;
        ParticleInstance particle;
        particle.position = sample.position;
        applyParticleControlPointRemap (particle, remap, points, true);
        // An absolute tolerance keeps both tiny and million-unit controls
        // discriminating; relative error would hide a whole-unit missed move.
        for (int axis = 0; axis < 3; ++axis)
            CHECK (std::abs (particle.position[axis] - sample.expected[axis]) <= 0.000001f);
    }
}

TEST_CASE ("two-control-point remaps write maximum lifetime before subsequent age evaluation", "[particle][remap][remap-position-lifetime]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { const char* operation; float full, half; } cases[] = {
        { nullptr, 16, 12 }, { "remap", 2, 5 }, { "multiply", 16, 12 },
        { "add", 10, 9 }, { "subtract", 6, 7 }
    };
    std::vector<ControlPointData> points (4);
    points[0].position = { 1000, 0, 0 };
    points[1].position = { 1100, 0, 0 };
    points[2].position = { -12, 20, 7 };
    points[3].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                for (const float age : { 0.0f, 2.0f, 4.0f }) {
                    INFO ("operation=" << (sample.operation ? sample.operation : "default") << ", age=" << age);
                    WallpaperEngine::Data::Model::Project project {};
                    auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":419,"particle":{}})");
                    auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                        "input":"positionbetweentwocontrolpoints","output":"maxlifetime",
                        "outputrangemax":8,"inputcontrolpoint0":0,"inputcontrolpoint1":1,
                        "outputcontrolpoint0":2,"outputcontrolpoint1":3,
                        "blendinstart":0,"blendinend":0.5
                    })");
                    node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                    if (sample.operation) node["operation"] = sample.operation;
                    data["particle"][initializer ? "initializer" : "operator"] = { node };
                    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                    const auto* definition = object->as<Particle> ();
                    REQUIRE (definition != nullptr);
                    REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                    const auto& remap = initializer
                        ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                        : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
                    ParticleInstance particle;
                    particle.position = { 2, 33, -1 }; // selected output-pair fraction .25
                    particle.velocity = { 7, -11, 13 };
                    particle.alpha = .75f;
                    particle.lifetime = 8;
                    particle.initial.lifetime = initializer ? 8 : 4;
                    particle.age = age;
                    particle.alive = true;
                    applyParticleControlPointRemap (particle, remap, points, initializer);
                    const float expected = initializer || age == 4 ? sample.full : age == 2 ? sample.half : 8;
                    CHECK (particle.lifetime == expected);
                    CHECK (particle.initial.lifetime == (initializer ? sample.full : 4));
                    CHECK (particle.age == age);
                    CHECK (particle.alive);
                    CHECK (particle.isAlive () == (age < expected));
                    CHECK (particle.getLifetimePos () == Catch::Approx (age / expected));
                    CHECK (particle.position == glm::vec3 (2, 33, -1));
                    CHECK (particle.velocity == glm::vec3 (7, -11, 13));
                    CHECK (particle.alpha == .75f);
                }
            }
        }
    }
}

TEST_CASE ("two-control-point lifetime remaps clamp mapped values and retain signed output", "[particle][remap][remap-position-lifetime]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { uint32_t flags; float full, half; } cases[] = {
        { 0, -128, -60 }, { 1, -64, -28 }, { 2, 0, 4 }, { 3, 0, 4 }
    };
    std::vector<ControlPointData> points (2);
    points[0].position = { -12, 20, 7 };
    points[1].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                INFO ("flags=" << sample.flags);
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":420,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                    "input":"positionbetweentwocontrolpoints","output":"maxlifetime",
                    "outputrangemin":"-8 100 200","outputrangemax":"24 120 240",
                    "blendinstart":0,"blendinend":0.5
                })");
                node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                node["flags"] = sample.flags;
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE (definition != nullptr);
                REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                const auto& remap = initializer
                    ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                    : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
                ParticleInstance particle;
                particle.position = { -14, 1, 15 }; // signed fraction -.25
                particle.lifetime = 8;
                particle.initial.lifetime = 3;
                particle.age = 2;
                applyParticleControlPointRemap (particle, remap, points, initializer);
                CHECK (particle.lifetime == (initializer ? sample.full : sample.half));
                CHECK (particle.initial.lifetime == (initializer ? sample.full : 3));
                CHECK (particle.age == 2);
                CHECK (particle.position == glm::vec3 (-14, 1, 15));
            }
        }
    }
}

TEST_CASE ("lifetime remap operators retain prior lifetime writes across dispatches", "[particle][remap][remap-position-lifetime]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":421,"particle":{"operator":[{
            "name":"remapvalue","input":"positionbetweentwocontrolpoints","output":"maxlifetime"
        }]}})"), project);
    const auto* definition = object->as<Particle> ();
    REQUIRE (definition != nullptr);
    REQUIRE (definition->operators.size () == 1);
    const auto& remap = definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
    std::vector<ControlPointData> points (2);
    points[1].position = { 64, 0, 0 };
    ParticleInstance particle;
    particle.position = { 32, 12, -8 };
    particle.lifetime = particle.initial.lifetime = 8;
    applyParticleControlPointRemap (particle, remap, points, false);
    CHECK (particle.lifetime == 4);
    applyParticleControlPointRemap (particle, remap, points, false);
    CHECK (particle.lifetime == 2);
    CHECK (particle.initial.lifetime == 8);
}

TEST_CASE ("size remaps read birth or working size before moving between control points", "[particle][remap][remap-size-position]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { const char* operation; glm::vec3 initial, full, half; } cases[] = {
        { nullptr, { -4, 21, 5 }, { 6, 41, -5 }, { 4, 37, -3 } },
        { "remap", { 2, 33, -1 }, { 42, 113, -41 }, { 22, 73, -21 } },
        { "multiply", { -4, 21, 5 }, { 6, 41, -5 }, { 4, 37, -3 } },
        { "add", { 10, 49, -9 }, { 50, 129, -49 }, { 26, 81, -25 } },
        { "subtract", { -6, 17, 7 }, { -46, -63, 47 }, { -22, -15, 23 } }
    };
    std::vector<ControlPointData> points (4);
    points[0].position = { 1000, 0, 0 };
    points[1].position = { 1100, 0, 0 };
    points[2].position = { -12, 20, 7 };
    points[3].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                for (const float age : { 0.0f, 1.0f, 2.0f }) {
                    INFO ("operation=" << (sample.operation ? sample.operation : "default") << ", age=" << age);
                    WallpaperEngine::Data::Model::Project project {};
                    auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":422,"particle":{}})");
                    auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                        "input":"size","output":"positionbetweentwocontrolpoints","outputrangemax":2,
                        "inputcontrolpoint0":0,"inputcontrolpoint1":1,"outputcontrolpoint0":2,"outputcontrolpoint1":3,
                        "blendinstart":0,"blendinend":0.5
                    })");
                    node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                    if (sample.operation) node["operation"] = sample.operation;
                    data["particle"][initializer ? "initializer" : "operator"] = { node };
                    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                    const auto* definition = object->as<Particle> ();
                    REQUIRE (definition != nullptr);
                    REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                    const auto& remap = initializer
                        ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                        : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
                    ParticleInstance particle;
                    particle.position = { 2, 33, -1 };
                    particle.velocity = { 7, -11, 13 };
                    particle.size = .75f;
                    particle.initial.size = .125f;
                    particle.alpha = .5f;
                    particle.lifetime = 4;
                    particle.age = age;
                    applyParticleControlPointRemap (particle, remap, points, initializer);
                    CHECK (particle.position == (initializer ? sample.initial : age == 2 ? sample.full : age == 1 ? sample.half : glm::vec3 (2, 33, -1)));
                    CHECK (particle.size == .75f);
                    CHECK (particle.initial.size == .125f);
                    CHECK (particle.velocity == glm::vec3 (7, -11, 13));
                    CHECK (particle.alpha == .5f);
                    CHECK (particle.lifetime == 4);
                    CHECK (particle.age == age);
                }
            }
        }
    }
}

TEST_CASE ("size inputs retain signed values until the authored remap clamps", "[particle][remap][remap-size-position]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { uint32_t flags; float initialX, operatorX; } cases[] = {
        { 0, 3584, -1024 }, { 1, 1536, -512 }, { 2, 64, 0 }, { 3, 64, 0 }
    };
    std::vector<ControlPointData> points (2);
    points[1].position = { 64, 0, 0 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                INFO ("flags=" << sample.flags);
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":423,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                    "input":"size","output":"positionbetweentwocontrolpoints","operation":"remap",
                    "outputrangemin":"-8 100 200","outputrangemax":"24 120 240"
                })");
                node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                node["flags"] = sample.flags;
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE (definition != nullptr);
                REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                const auto& remap = initializer
                    ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                    : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
                ParticleInstance particle;
                particle.position = { 32, 12, -8 };
                particle.size = -.25f;
                particle.initial.size = 2;
                applyParticleControlPointRemap (particle, remap, points, initializer);
                CHECK (particle.position == glm::vec3 (initializer ? sample.initialX : sample.operatorX, 12, -8));
                CHECK (particle.size == -.25f);
                CHECK (particle.initial.size == 2);
            }
        }
    }
}

TEST_CASE ("size input remains independent of tiny initializer position output", "[particle][remap][remap-size-position]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":424,"particle":{"initializer":[{
            "name":"remapinitialvalue","input":"size","output":"positionbetweentwocontrolpoints",
            "operation":"remap","outputrangemax":2.81474976710656e14
        }]}})"), project);
    const auto* definition = object->as<Particle> ();
    REQUIRE (definition != nullptr);
    REQUIRE (definition->initializers.size () == 1);
    const auto& remap = definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap;
    REQUIRE (remap.outputRangeMax->value->getFloat () == 0x1p48f);
    std::vector<ControlPointData> points (2);
    points[1].position = { std::numeric_limits<float>::epsilon (), 0, 0 };
    ParticleInstance particle;
    particle.position = { .25f, .5f, -.75f };
    particle.size = .75f;
    particle.initial.size = .25f;
    applyParticleControlPointRemap (particle, remap, points, true);
    CHECK (particle.position == glm::vec3 (1.25f, .5f, -.75f));
    CHECK (particle.size == .75f);
    CHECK (particle.initial.size == .25f);
}

TEST_CASE ("opacity remaps read birth or working opacity before moving between control points", "[particle][remap][remap-opacity-position]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { const char* operation; glm::vec3 initial, full, half; } cases[] = {
        { nullptr, { -4, 21, 5 }, { 6, 41, -5 }, { 4, 37, -3 } },
        { "remap", { 2, 33, -1 }, { 42, 113, -41 }, { 22, 73, -21 } },
        { "multiply", { -4, 21, 5 }, { 6, 41, -5 }, { 4, 37, -3 } },
        { "add", { 10, 49, -9 }, { 50, 129, -49 }, { 26, 81, -25 } },
        { "subtract", { -6, 17, 7 }, { -46, -63, 47 }, { -22, -15, 23 } }
    };
    std::vector<ControlPointData> points (4);
    points[0].position = { 1000, 0, 0 };
    points[1].position = { 1100, 0, 0 };
    points[2].position = { -12, 20, 7 };
    points[3].position = { 20, 84, -25 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                for (const float age : { 0.0f, 1.0f, 2.0f }) {
                    INFO ("operation=" << (sample.operation ? sample.operation : "default") << ", age=" << age);
                    WallpaperEngine::Data::Model::Project project {};
                    auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":425,"particle":{}})");
                    auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                        "input":"opacity","output":"positionbetweentwocontrolpoints","outputrangemax":2,
                        "inputcontrolpoint0":0,"inputcontrolpoint1":1,"outputcontrolpoint0":2,"outputcontrolpoint1":3,
                        "blendinstart":0,"blendinend":0.5
                    })");
                    node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                    if (sample.operation) node["operation"] = sample.operation;
                    data["particle"][initializer ? "initializer" : "operator"] = { node };
                    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                    const auto* definition = object->as<Particle> ();
                    REQUIRE (definition != nullptr);
                    REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                    const auto& remap = initializer
                        ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                        : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
                    ParticleInstance particle;
                    particle.position = { 2, 33, -1 };
                    particle.velocity = { 7, -11, 13 };
                    particle.alpha = .75f;
                    particle.initial.alpha = .125f;
                    particle.size = .5f;
                    particle.lifetime = 4;
                    particle.age = age;
                    applyParticleControlPointRemap (particle, remap, points, initializer);
                    CHECK (particle.position == (initializer ? sample.initial : age == 2 ? sample.full : age == 1 ? sample.half : glm::vec3 (2, 33, -1)));
                    CHECK (particle.alpha == .75f);
                    CHECK (particle.initial.alpha == .125f);
                    CHECK (particle.velocity == glm::vec3 (7, -11, 13));
                    CHECK (particle.size == .5f);
                    CHECK (particle.lifetime == 4);
                    CHECK (particle.age == age);
                }
            }
        }
    }
}

TEST_CASE ("opacity inputs retain signed values until the authored remap clamps", "[particle][remap][remap-opacity-position]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { uint32_t flags; float initialX, operatorX; } cases[] = {
        { 0, 3584, -1024 }, { 1, 1536, -512 }, { 2, 64, 0 }, { 3, 64, 0 }
    };
    std::vector<ControlPointData> points (2);
    points[1].position = { 64, 0, 0 };
    for (const bool initializer : { true, false }) {
        DYNAMIC_SECTION ("initializer=" << initializer) {
            for (const auto& sample : cases) {
                INFO ("flags=" << sample.flags);
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":426,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                    "input":"opacity","output":"positionbetweentwocontrolpoints","operation":"remap",
                    "outputrangemin":"-8 100 200","outputrangemax":"24 120 240"
                })");
                node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                node["flags"] = sample.flags;
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE (definition != nullptr);
                REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                const auto& remap = initializer
                    ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                    : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
                ParticleInstance particle;
                particle.position = { 32, 12, -8 };
                particle.alpha = -.25f;
                particle.initial.alpha = 2;
                applyParticleControlPointRemap (particle, remap, points, initializer);
                CHECK (particle.position == glm::vec3 (initializer ? sample.initialX : sample.operatorX, 12, -8));
                CHECK (particle.alpha == -.25f);
                CHECK (particle.initial.alpha == 2);
            }
        }
    }
}

TEST_CASE ("opacity input remains independent of tiny initializer position output", "[particle][remap][remap-opacity-position]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":427,"particle":{"initializer":[{
            "name":"remapinitialvalue","input":"opacity","output":"positionbetweentwocontrolpoints",
            "operation":"remap","outputrangemax":2.81474976710656e14
        }]}})"), project);
    const auto* definition = object->as<Particle> ();
    REQUIRE (definition != nullptr);
    REQUIRE (definition->initializers.size () == 1);
    const auto& remap = definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap;
    REQUIRE (remap.outputRangeMax->value->getFloat () == 0x1p48f);
    std::vector<ControlPointData> points (2);
    points[1].position = { std::numeric_limits<float>::epsilon (), 0, 0 };
    ParticleInstance particle;
    particle.position = { .25f, .5f, -.75f };
    particle.alpha = .75f;
    particle.initial.alpha = .25f;
    applyParticleControlPointRemap (particle, remap, points, true);
    CHECK (particle.position == glm::vec3 (1.25f, .5f, -.75f));
    CHECK (particle.alpha == .75f);
    CHECK (particle.initial.alpha == .25f);
}

TEST_CASE ("opacity position input observes a prior working-alpha operator", "[particle][remap][remap-opacity-position]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    WallpaperEngine::Data::Model::Project project {};
    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (
        WallpaperEngine::Data::JSON::JSON::parse (R"({"id":428,"particle":{"operator":[
            {"name":"remapvalue","input":"positionbetweentwocontrolpoints","output":"opacity"},
            {"name":"remapvalue","input":"opacity","output":"positionbetweentwocontrolpoints","operation":"remap"}
        ]}})"), project);
    const auto* definition = object->as<Particle> ();
    REQUIRE (definition != nullptr);
    REQUIRE (definition->operators.size () == 2);
    std::vector<ControlPointData> points (2);
    points[1].position = { 64, 0, 0 };
    ParticleInstance particle;
    particle.position = { 32, 12, -8 };
    particle.alpha = particle.initial.alpha = .75f;
    for (const auto& op : definition->operators)
        applyParticleControlPointRemap (particle, op->as<ControlPointRemapOperator> ()->remap, points, false);
    CHECK (particle.position == glm::vec3 (24, 12, -8));
    CHECK (particle.alpha == .375f);
    CHECK (particle.initial.alpha == .75f);
}

TEST_CASE ("two point remaps read native speed and Z angle channels", "[particle][remap][remap-kinematic]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    std::vector<ControlPointData> points (2);
    points[1].position = { 64, 0, 0 };
    for (const bool initializer : { true, false }) {
        for (const auto& sample : { std::pair { "speed", 320.0f }, std::pair { "rotation", -32.0f }, std::pair { "angularspeed", 0.0f } }) {
            INFO ("initializer=" << initializer << ", input=" << sample.first);
            WallpaperEngine::Data::Model::Project project {};
            auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":430,"particle":{}})");
            auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                "output":"positionbetweentwocontrolpoints","operation":"remap","flags":0
            })");
            node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
            node["input"] = sample.first;
            data["particle"][initializer ? "initializer" : "operator"] = { node };
            const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
            const auto* definition = object->as<Particle> ();
            REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
            const auto& remap = initializer
                ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
            ParticleInstance particle;
            particle.position = { 32, 12, -8 };
            particle.velocity = { 3, 4, 0 };
            particle.rotation = { 7, 9, -.5f };
            particle.angularVelocity = { 8, 10, 4 };
            applyParticleControlPointRemap (particle, remap, points, initializer);
            CHECK (particle.position == glm::vec3 (sample.second, 12, -8));
            CHECK (particle.velocity == glm::vec3 (3, 4, 0));
            CHECK (particle.rotation == glm::vec3 (7, 9, -.5f));
            CHECK (particle.angularVelocity == glm::vec3 (8, 10, 4));
            if (std::string_view (sample.first) == "angularspeed") {
                applyParticleControlPointRemap (particle, remap, points, initializer, true);
                CHECK (particle.position == glm::vec3 (256, 12, -8));
                CHECK (particle.angularVelocity == glm::vec3 (8, 10, 4));
            }
        }
    }
}

TEST_CASE ("two point speed outputs preserve native direction zero and arithmetic", "[particle][remap][remap-kinematic]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    // Unchanged native enum 4 output instructions, binary 40e2ce02: initializer,
    // ordinary operator, and half-blended operator. Reciprocal SIMD tolerance.
    const struct { bool initializer, blended; const char* operation; glm::vec3 expected; } samples[] = {
        { true, false, "remap", { -1.200000048f, -1.600000024f, -0.000000000f } },
        { true, false, "multiply", { -6.000000000f, -8.000000000f, -0.000000000f } },
        { true, false, "add", { 1.800000072f, 2.400000095f, 0.000000000f } },
        { true, false, "subtract", { 4.199999809f, 5.599999905f, 0.000000000f } },
        { false, false, "remap", { -1.199707031f, -1.599609375f, -0.000000000f } },
        { false, false, "multiply", { -5.998535156f, -7.998046875f, -0.000000000f } },
        { false, false, "add", { 1.799560547f, 2.399414062f, 0.000000000f } },
        { false, false, "subtract", { 4.198974609f, 5.598632812f, 0.000000000f } },
        { false, true, "remap", { 0.899780273f, 1.199707031f, 0.000000000f } },
        { false, true, "multiply", { -1.499633789f, -1.999511719f, -0.000000000f } },
        { false, true, "add", { 2.399414062f, 3.199218750f, 0.000000000f } },
        { false, true, "subtract", { 3.599121094f, 4.798828125f, 0.000000000f } },
    };
    std::vector<ControlPointData> points (2);
    points[1].position = { 64, 0, 0 };
    for (const auto& sample : samples) {
        INFO ("initializer=" << sample.initializer << ", blended=" << sample.blended << ", operation=" << sample.operation);
        WallpaperEngine::Data::Model::Project project {};
        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":431,"particle":{}})");
        auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
            "input":"positionbetweentwocontrolpoints","output":"speed","flags":0,
            "outputrangemin":-2,"outputrangemax":-2
        })");
        node["name"] = sample.initializer ? "remapinitialvalue" : "remapvalue";
        node["operation"] = sample.operation;
        if (sample.blended) node["blendinend"] = .5;
        data["particle"][sample.initializer ? "initializer" : "operator"] = { node };
        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
        const auto* definition = object->as<Particle> ();
        REQUIRE ((sample.initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
        const auto& remap = sample.initializer
            ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
            : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
        for (const bool zero : { false, true }) {
            ParticleInstance particle;
            particle.position = { 32, 12, -8 };
            particle.velocity = zero ? glm::vec3 (0) : glm::vec3 (3, 4, 0);
            particle.lifetime = 4;
            particle.age = 1;
            applyParticleControlPointRemap (particle, remap, points, sample.initializer);
            const glm::vec3 expected = zero ? glm::vec3 (0) : sample.expected;
            for (int axis = 0; axis < 3; ++axis)
                CHECK (particle.velocity[axis] == Catch::Approx (expected[axis]).epsilon (.0004).margin (.00001));
            CHECK (particle.position == glm::vec3 (32, 12, -8));
        }
    }
}

TEST_CASE ("two point angle outputs retain native radians and optional storage", "[particle][remap][remap-kinematic]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { const char* operation; float rotation, angular; } samples[] = {
        { "remap", -2, -2 }, { "multiply", 1, -8 }, { "add", -2.5f, 2 }, { "subtract", 1.5f, 6 }
    };
    std::vector<ControlPointData> points (2);
    points[1].position = { 64, 0, 0 };
    for (const bool initializer : { true, false }) {
        for (const auto output : { "rotation", "angularspeed" }) {
            for (const auto& sample : samples) {
                INFO ("initializer=" << initializer << ", output=" << output << ", operation=" << sample.operation);
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":432,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                    "input":"positionbetweentwocontrolpoints","flags":0,"outputrangemin":-2,"outputrangemax":-2,
                    "blendinend":0.5
                })");
                node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                node["operation"] = sample.operation;
                node["output"] = output;
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                const auto& remap = initializer
                    ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                    : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
                ParticleInstance particle;
                particle.position = { 32, 12, -8 };
                particle.rotation = { 7, 9, -.5f };
                particle.angularVelocity = { 8, 10, 4 };
                particle.lifetime = 4;
                particle.age = 1;
                applyParticleControlPointRemap (particle, remap, points, initializer);
                const bool rotation = std::string_view (output) == "rotation";
                CHECK (particle.rotation == glm::vec3 (7, 9, rotation ? (initializer ? sample.rotation : -.5f + (sample.rotation + .5f) * .5f) : -.5f));
                CHECK (particle.angularVelocity == glm::vec3 (8, 10, 4));
                CHECK (particle.position == glm::vec3 (32, 12, -8));
                if (!rotation) {
                    applyParticleControlPointRemap (particle, remap, points, initializer, true);
                    CHECK (particle.angularVelocity == glm::vec3 (8, 10, initializer ? sample.angular : 4 + (sample.angular - 4) * .5f));
                    CHECK (particle.rotation == glm::vec3 (7, 9, -.5f));
                }
            }
        }
    }
}

TEST_CASE ("owner clock remaps distinguish scene runtime daytime and particle age", "[particle][remap][remap-clocks]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    std::vector<ControlPointData> points (4);
    points[1].position = { -64, 0, 0 };
    points[2].position = { 0, 0, 0 };
    points[3].position = { 64, 0, 0 };
    // Native raw clock controls: owner runtime4, day1/8, system-1/2, layer9.
    // The operator's system-time selection is runtime4, not system-1/2.
    const struct { const char* name; float input; } samples[] = {
        { "runtime", 4 }, { "timeofday", .125f }, { "particlesystemtime", 4 }
    };
    for (const bool initializer : { true, false }) {
        for (const auto& sample : samples) {
            if (initializer && std::string_view (sample.name) == "particlesystemtime") continue;
            for (const std::string operation : { "remap", "multiply", "add", "subtract" }) {
                for (const float age : { 0.0f, 1.0f, 2.0f }) {
                    INFO (sample.name << ", initializer=" << initializer << ", operation=" << operation << ", age=" << age);
                    WallpaperEngine::Data::Model::Project project {};
                    auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":435,"particle":{}})");
                    auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                        "output":"positionbetweentwocontrolpoints","flags":0,"outputrangemax":2,
                        "outputcontrolpoint0":2,"outputcontrolpoint1":3,"blendinend":0.5
                    })");
                    node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                    node["input"] = sample.name;
                    node["operation"] = operation;
                    data["particle"][initializer ? "initializer" : "operator"] = { node };
                    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                    const auto* definition = object->as<Particle> ();
                    REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                    const auto& remap = initializer
                        ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                        : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
                    ParticleInstance particle;
                    particle.position = { 32, 12, -8 };
                    particle.velocity = { 7, -11, 13 };
                    particle.lifetime = 4;
                    particle.age = age;
                    applyParticleControlPointRemap (particle, remap, points, initializer, false, 4, .125f);
                    const float mapped = sample.input * 2;
                    const float target = operation == "remap" ? mapped : operation == "multiply" ? .5f * mapped
                        : operation == "add" ? .5f + mapped : .5f - mapped;
                    const float weight = initializer ? 1 : age * .5f;
                    CHECK (particle.position == glm::vec3 (32 + 64 * (target - .5f) * weight, 12, -8));
                    CHECK (particle.velocity == glm::vec3 (7, -11, 13));
                    CHECK (particle.lifetime == 4);
                    CHECK (particle.age == age);
                }
            }
        }
    }
}

TEST_CASE ("two point remaps admit only the measured radial counterpart pairs", "[particle][remap][remap-radial]") {
    for (const bool initializer : { true, false }) {
        for (const bool distanceInput : { true, false }) {
            WallpaperEngine::Data::Model::Project project {};
            auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":436,"particle":{}})");
            auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({"operation":"remap"})");
            node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
            node["input"] = distanceInput ? "distancetocontrolpoint" : "positionbetweentwocontrolpoints";
            node["output"] = distanceInput ? "positionbetweentwocontrolpoints" : "distancetocontrolpoint";
            data["particle"][initializer ? "initializer" : "operator"] = { node };
            const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
            const auto* particle = object->as<Particle> ();
            CHECK ((initializer ? particle->initializers.size () : particle->operators.size ()) == 1);
            node[distanceInput ? "output" : "input"] = "size";
            data["particle"][initializer ? "initializer" : "operator"] = { node };
            const auto unsupported = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
            CHECK ((initializer ? unsupported->as<Particle> ()->initializers.size ()
                                : unsupported->as<Particle> ()->operators.size ()) == 0);
        }
    }
}

TEST_CASE ("radial input reads selected input control point before two point mapping", "[particle][remap][remap-radial]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    std::vector<ControlPointData> points (6);
    points[0].position = { 100, 200, 300 };
    points[2].position = { 2, 6, 4 };
    points[3].position = { -32, -16, 8 };
    points[5].position = { 64, 0, 0 };
    // Native radial getter reads distance5 from P(5,10,4) to input CP0(2,6,4).
    for (const bool initializer : { true, false }) {
        for (const std::string operation : { "remap", "multiply", "add", "subtract" }) {
            for (const float age : { 0.0f, 1.0f, 2.0f }) {
                INFO ("initializer=" << initializer << ", operation=" << operation << ", age=" << age);
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":437,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                    "input":"distancetocontrolpoint","output":"positionbetweentwocontrolpoints",
                    "inputcontrolpoint0":2,"inputcontrolpoint1":3,"outputcontrolpoint0":4,"outputcontrolpoint1":5,
                    "inputrangemax":10,"flags":0,"blendinend":0.5
                })");
                node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                node["operation"] = operation;
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* definition = object->as<Particle> ();
                REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                const auto& remap = initializer
                    ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                    : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
                for (const bool coincident : { false, true }) {
                    ParticleInstance particle;
                    particle.position = coincident ? points[2].position : glm::vec3 (5, 10, 4);
                    particle.velocity = { 7, 9, -11 };
                    particle.lifetime = 4;
                    particle.age = age;
                    const glm::vec3 before = particle.position;
                    const float fraction = before.x / 64;
                    const float mapped = coincident ? 0 : .5f;
                    const float target = operation == "remap" ? mapped : operation == "multiply" ? fraction * mapped
                        : operation == "add" ? fraction + mapped : fraction - mapped;
                    const float weight = initializer ? 1 : age * .5f;
                    applyParticleControlPointRemap (particle, remap, points, initializer);
                    CHECK (particle.position == before + glm::vec3 (64 * (target - fraction) * weight, 0, 0));
                    CHECK (particle.velocity == glm::vec3 (7, 9, -11));
                }
            }
        }
    }
}

TEST_CASE ("radial output follows native signed radius and initializer tiny distance rules", "[particle][remap][remap-radial]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    // Literal readbacks from unchanged native initializer/ordinary/blended output
    // slices14023de79/140245c9e/1402483bc. SIMD reciprocal-square-root approximation
    // accounts for the small difference from the portable scalar normalization.
    const struct {
        int kind;
        const char* operation;
        glm::vec3 position, center;
        float mapped, weight;
        glm::vec3 expected;
    } samples[] = {
        { 0, "remap", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { 0.799999952f, 4.4000001f, 4.0f } },
        { 0, "multiply", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { -4.0f, -2.0f, 4.0f } },
        { 0, "add", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { 3.80000019f, 8.39999962f, 4.0f } },
        { 0, "subtract", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { 6.20000029f, 11.6000004f, 4.0f } },
        { 0, "remap", { 2.0f, 6.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 1.0f, { 2.0f, 6.0f, 4.0f } },
        { 0, "multiply", { 2.0f, 6.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 1.0f, { 2.0f, 6.0f, 4.0f } },
        { 0, "add", { 2.0f, 6.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 1.0f, { 2.0f, 6.0f, 4.0f } },
        { 0, "subtract", { 2.0f, 6.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 1.0f, { 2.0f, 6.0f, 4.0f } },
        { 0, "remap", { 5.96046448e-08f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { 8.0f, 0.0f, 0.0f } },
        { 0, "multiply", { 5.96046448e-08f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { 4.76837158e-07f, 0.0f, 0.0f } },
        { 0, "add", { 5.96046448e-08f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { 8.0f, 0.0f, 0.0f } },
        { 0, "subtract", { 5.96046448e-08f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { -8.0f, 0.0f, 0.0f } },
        { 0, "remap", { 8.47032947e-22f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { 8.0f, 0.0f, 0.0f } },
        { 0, "multiply", { 8.47032947e-22f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { 6.77626358e-21f, 0.0f, 0.0f } },
        { 0, "add", { 8.47032947e-22f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { 8.0f, 0.0f, 0.0f } },
        { 0, "subtract", { 8.47032947e-22f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { -8.0f, 0.0f, 0.0f } },
        { 0, "remap", { 8.27180613e-25f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { 6.6174449e-24f, 0.0f, 0.0f } },
        { 0, "multiply", { 8.27180613e-25f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { 0.0f, 0.0f, 0.0f } },
        { 0, "add", { 8.27180613e-25f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { 6.6174449e-24f, 0.0f, 0.0f } },
        { 0, "subtract", { 8.27180613e-25f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 8.0f, 1.0f, { -6.6174449e-24f, 0.0f, 0.0f } },
        { 1, "remap", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { 0.800109863f, 4.40014648f, 4.0f } },
        { 1, "multiply", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { -3.99890137f, -1.99853516f, 4.0f } },
        { 1, "add", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { 3.79956055f, 8.39941406f, 4.0f } },
        { 1, "subtract", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { 6.19934082f, 11.5991211f, 4.0f } },
        { 2, "remap", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { 0.800109863f, 4.40014648f, 4.0f } },
        { 2, "remap", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 0.0f, { 4.99945068f, 9.99926758f, 4.0f } },
        { 2, "remap", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 0.5f, { 5.89950562f, 11.1993408f, 4.0f } },
        { 2, "multiply", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { -3.99890137f, -1.99853516f, 4.0f } },
        { 2, "multiply", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 0.0f, { 4.99945068f, 9.99926758f, 4.0f } },
        { 2, "multiply", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 0.5f, { 15.4975281f, 23.9967041f, 4.0f } },
        { 2, "add", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { 3.79956055f, 8.39941406f, 4.0f } },
        { 2, "add", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 0.0f, { 4.99945068f, 9.99926758f, 4.0f } },
        { 2, "add", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 0.5f, { 7.39923096f, 13.1989746f, 4.0f } },
        { 2, "subtract", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, -2.0f, 1.0f, { 6.19934082f, 11.5991211f, 4.0f } },
        { 2, "subtract", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 0.0f, { 4.99945068f, 9.99926758f, 4.0f } },
        { 2, "subtract", { 5.0f, 10.0f, 4.0f }, { 2.0f, 6.0f, 4.0f }, 8.0f, 0.5f, { 2.59967041f, 6.79956055f, 4.0f } },
    };
    for (const auto& sample : samples) {
        INFO ("kind=" << sample.kind << ", operation=" << sample.operation << ", weight=" << sample.weight
              << ", position=" << sample.position.x << "," << sample.position.y << "," << sample.position.z);
        const bool initializer = sample.kind == 0;
        std::vector<ControlPointData> points (6);
        points[0].position = { 100, 200, 300 };
        points[2].position = { -32, -16, 8 };
        points[4].position = sample.center;
        points[5].position = sample.center + glm::vec3 (64, 0, 0);
        WallpaperEngine::Data::Model::Project project {};
        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":438,"particle":{}})");
        auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
            "input":"positionbetweentwocontrolpoints","output":"distancetocontrolpoint",
            "inputcontrolpoint0":2,"outputcontrolpoint0":4,"outputcontrolpoint1":5,"flags":0
        })");
        node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
        node["operation"] = sample.operation;
        node["outputrangemin"] = sample.mapped;
        node["outputrangemax"] = sample.mapped;
        if (sample.kind == 2) node["blendinend"] = .5;
        data["particle"][initializer ? "initializer" : "operator"] = { node };
        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
        const auto* definition = object->as<Particle> ();
        REQUIRE ((initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
        const auto& remap = initializer
            ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
            : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
        ParticleInstance particle;
        particle.position = sample.position;
        particle.velocity = { 7, 9, -11 };
        particle.lifetime = 4;
        particle.age = sample.weight * 2;
        applyParticleControlPointRemap (particle, remap, points, initializer);
        for (int axis = 0; axis < 3; ++axis)
            CHECK (particle.position[axis] == Catch::Approx (sample.expected[axis]).epsilon (.0007)
                .margin (sample.kind == 0 ? 1e-30 : .003));
        CHECK (particle.velocity == glm::vec3 (7, 9, -11));
    }
}

TEST_CASE ("radial operator leaves native nonfinite distance cases outside finite output support", "[particle][remap][remap-radial]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    // Native rsqrtps produces nonfinite coordinates for these cases even at
    // blend weight0. Keep the existing finite-particle scope explicit.
    for (const float distance : { 0.0f, 8.470329473e-22f, 8.271806126e-25f }) {
        for (const float age : { 0.0f, 1.0f, 2.0f }) {
            WallpaperEngine::Data::Model::Project project {};
            auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":439,"particle":{"operator":[{
                "name":"remapvalue","input":"positionbetweentwocontrolpoints","output":"distancetocontrolpoint",
                "operation":"remap","flags":0,"outputrangemin":8,"outputrangemax":8,"blendinend":0.5
            }]}})");
            const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
            const auto* definition = object->as<Particle> ();
            REQUIRE (definition->operators.size () == 1);
            const auto& remap = definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
            std::vector<ControlPointData> points (2);
            points[1].position = { 64, 0, 0 };
            ParticleInstance particle;
            particle.position = { distance, 0, 0 };
            particle.lifetime = 4;
            particle.age = age;
            applyParticleControlPointRemap (particle, remap, points, false);
            CHECK (particle.position == glm::vec3 (distance, 0, 0));
        }
    }
}

TEST_CASE ("periodic transforms admit measured scalar two point paths and keep vector gates", "[particle][remap][remap-periodic]") {
    for (const bool initializer : { true, false }) {
        for (const auto transform : { "sine", "square", "saw", "triangle" }) {
            for (const bool positionInput : { true, false }) {
                WallpaperEngine::Data::Model::Project project {};
                auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":440,"particle":{}})");
                auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({"operation":"remap"})");
                node["name"] = initializer ? "remapinitialvalue" : "remapvalue";
                node["input"] = positionInput ? "positionbetweentwocontrolpoints" : "size";
                node["output"] = positionInput ? "size" : "positionbetweentwocontrolpoints";
                node["transformfunction"] = transform;
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                const auto* particle = object->as<Particle> ();
                CHECK ((initializer ? particle->initializers.size () : particle->operators.size ()) == 1);
                node[positionInput ? "output" : "input"] = "velocity";
                data["particle"][initializer ? "initializer" : "operator"] = { node };
                const auto unsupported = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                CHECK ((initializer ? unsupported->as<Particle> ()->initializers.size ()
                                    : unsupported->as<Particle> ()->operators.size ()) == 0);
            }
        }
    }
}

TEST_CASE ("periodic scalar transforms match native phase scale and half step readbacks", "[particle][remap][remap-periodic]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    // Literal native scalar initializer and SIMD operator readbacks. The native
    // blended kernel independently agrees with the ordinary operator samples.
    const struct { bool initializer; const char* transform; float input, scale, expected; } samples[] = {
        { true, "sine", 0.5f, -3.0f, 0.499999911f },
        { true, "sine", 0.5f, 0.0f, 0.0f },
        { true, "sine", 0.5f, 0.5f, 0.146446615f },
        { true, "sine", -1.0f, 2.0f, 0.0f },
        { true, "sine", -0.25f, 2.0f, 0.50000006f },
        { true, "sine", 0.25f, 2.0f, 0.5f },
        { true, "sine", 0.5f, 2.0f, 1.0f },
        { true, "sine", 1.0f, 2.0f, 0.0f },
        { true, "square", 0.5f, -3.0f, 2.0f },
        { true, "square", 0.5f, 0.0f, 0.0f },
        { true, "square", 0.5f, 0.5f, 0.0f },
        { true, "square", -1.0f, 2.0f, 1.0f },
        { true, "square", -0.25f, 2.0f, 2.0f },
        { true, "square", 0.249f, 2.0f, 0.0f },
        { true, "square", 0.25f, 2.0f, 1.0f },
        { true, "square", 0.251f, 2.0f, 1.0f },
        { true, "square", 0.5f, 2.0f, 0.0f },
        { true, "square", 1.0f, 2.0f, 0.0f },
        { true, "saw", -0.25f, -3.0f, 1.75f },
        { true, "saw", 0.5f, -3.0f, 0.5f },
        { true, "saw", 0.5f, 0.0f, 0.0f },
        { true, "saw", 0.5f, 0.5f, 0.25f },
        { true, "saw", -1.0f, 2.0f, 1.0f },
        { true, "saw", -0.25f, 2.0f, 1.5f },
        { true, "saw", 0.25f, 2.0f, 0.5f },
        { true, "saw", 0.5f, 2.0f, 0.0f },
        { true, "saw", 1.0f, 2.0f, 0.0f },
        { true, "triangle", 0.5f, -3.0f, 1.0f },
        { true, "triangle", 0.5f, 0.0f, 0.0f },
        { true, "triangle", 0.5f, 0.5f, 0.5f },
        { true, "triangle", -1.0f, 2.0f, 0.0f },
        { true, "triangle", -0.25f, 2.0f, 1.0f },
        { true, "triangle", 0.25f, 2.0f, 1.0f },
        { true, "triangle", 0.5f, 2.0f, 0.0f },
        { true, "triangle", 1.0f, 2.0f, 0.0f },
        { false, "sine", 0.5f, -3.0f, 0.499999911f },
        { false, "sine", 0.5f, 0.0f, 0.0f },
        { false, "sine", 0.5f, 0.5f, 0.146446586f },
        { false, "sine", -1.0f, 2.0f, 0.0f },
        { false, "sine", -0.25f, 2.0f, 0.50000006f },
        { false, "sine", 0.25f, 2.0f, 0.5f },
        { false, "sine", 0.5f, 2.0f, 1.0f },
        { false, "sine", 1.0f, 2.0f, 0.0f },
        { false, "square", 0.5f, -3.0f, 1.0f },
        { false, "square", 0.5f, 0.0f, 0.0f },
        { false, "square", 0.5f, 0.5f, 0.0f },
        { false, "square", -1.0f, 2.0f, 1.0f },
        { false, "square", -0.25f, 2.0f, 1.0f },
        { false, "square", 0.249f, 2.0f, 0.0f },
        { false, "square", 0.25f, 2.0f, 0.0f },
        { false, "square", 0.251f, 2.0f, 1.0f },
        { false, "square", 0.5f, 2.0f, 0.0f },
        { false, "square", 1.0f, 2.0f, 0.0f },
        { false, "saw", -0.25f, -3.0f, 1.75f },
        { false, "saw", 0.5f, -3.0f, -0.5f },
        { false, "saw", 0.5f, 0.0f, 0.0f },
        { false, "saw", 0.5f, 0.5f, 0.25f },
        { false, "saw", -1.0f, 2.0f, 1.0f },
        { false, "saw", -0.25f, 2.0f, 0.5f },
        { false, "saw", 0.25f, 2.0f, 0.5f },
        { false, "saw", 0.5f, 2.0f, 0.0f },
        { false, "saw", 1.0f, 2.0f, 0.0f },
        { false, "triangle", 0.5f, -3.0f, 1.0f },
        { false, "triangle", 0.5f, 0.0f, 0.0f },
        { false, "triangle", 0.5f, 0.5f, 0.5f },
        { false, "triangle", -1.0f, 2.0f, 0.0f },
        { false, "triangle", -0.25f, 2.0f, 1.0f },
        { false, "triangle", 0.25f, 2.0f, 1.0f },
        { false, "triangle", 0.5f, 2.0f, 0.0f },
        { false, "triangle", 1.0f, 2.0f, 0.0f },
    };
    std::vector<ControlPointData> points (2);
    points[1].position = { 64, 0, 0 };
    for (const auto& sample : samples) {
        INFO (sample.transform << ", initializer=" << sample.initializer << ", input=" << sample.input << ", scale=" << sample.scale);
        WallpaperEngine::Data::Model::Project project {};
        auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":441,"particle":{}})");
        auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
            "input":"positionbetweentwocontrolpoints","output":"size","operation":"remap","flags":0
        })");
        node["name"] = sample.initializer ? "remapinitialvalue" : "remapvalue";
        node["transformfunction"] = sample.transform;
        node["transforminputscale"] = sample.scale;
        data["particle"][sample.initializer ? "initializer" : "operator"] = { node };
        const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
        const auto* definition = object->as<Particle> ();
        REQUIRE ((sample.initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
        const auto& remap = sample.initializer
            ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
            : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
        ParticleInstance particle;
        particle.position = { sample.input * 64, 12, -8 };
        particle.size = particle.initial.size = 7;
        particle.lifetime = 4;
        particle.age = 1;
        applyParticleControlPointRemap (particle, remap, points, sample.initializer);
        CHECK (particle.size == Catch::Approx (sample.expected).epsilon (.00002).margin (.000002));
        CHECK (particle.initial.size == Catch::Approx (sample.initializer ? sample.expected : 7).epsilon (.00002).margin (.000002));
        CHECK (particle.position == glm::vec3 (sample.input * 64, 12, -8));
    }
}

TEST_CASE ("periodic mapping clamps on each side before arithmetic and position blending", "[particle][remap][remap-periodic]") {
    using WallpaperEngine::Render::Objects::applyParticleControlPointRemap;
    using WallpaperEngine::Render::Objects::ControlPointData;
    const struct { bool initializer; const char* transform; float raw, inputClamped; } samples[] = {
        { true, "sine", 0.50000006f, 0.0f },
        { true, "square", 2.0f, 0.0f },
        { true, "saw", 1.5f, 0.0f },
        { true, "triangle", 1.0f, 0.0f },
        { false, "sine", 0.50000006f, 0.0f },
        { false, "square", 1.0f, 0.0f },
        { false, "saw", 0.5f, 0.0f },
        { false, "triangle", 1.0f, 0.0f },
    };
    std::vector<ControlPointData> points (2);
    points[1].position = { 64, 0, 0 };
    for (const auto& sample : samples) {
        for (const uint32_t flags : { 0u, 1u, 2u, 3u }) {
            for (const std::string operation : { "remap", "multiply", "add", "subtract" }) {
                for (const float age : { 0.0f, 1.0f, 2.0f }) {
                    INFO (sample.transform << ", initializer=" << sample.initializer << ", flags=" << flags << ", operation=" << operation << ", age=" << age);
                    WallpaperEngine::Data::Model::Project project {};
                    auto data = WallpaperEngine::Data::JSON::JSON::parse (R"({"id":442,"particle":{}})");
                    auto node = WallpaperEngine::Data::JSON::JSON::parse (R"({
                        "input":"size","output":"positionbetweentwocontrolpoints",
                        "inputrangemin":0.25,"inputrangemax":1.25,
                        "outputrangemin":-1,"outputrangemax":3,"blendinend":0.5
                    })");
                    node["name"] = sample.initializer ? "remapinitialvalue" : "remapvalue";
                    node["transformfunction"] = sample.transform;
                    node["operation"] = operation;
                    node["flags"] = flags;
                    data["particle"][sample.initializer ? "initializer" : "operator"] = { node };
                    const auto object = WallpaperEngine::Data::Parsers::ObjectParser::parse (data, project);
                    const auto* definition = object->as<Particle> ();
                    REQUIRE ((sample.initializer ? definition->initializers.size () : definition->operators.size ()) == 1);
                    const auto& remap = sample.initializer
                        ? definition->initializers.front ()->as<ControlPointRemapInitializer> ()->remap
                        : definition->operators.front ()->as<ControlPointRemapOperator> ()->remap;
                    ParticleInstance particle;
                    particle.position = { 32, 12, -8 };
                    particle.size = particle.initial.size = 0;
                    particle.lifetime = 4;
                    particle.age = age;
                    const float transformed = flags & 1 ? sample.inputClamped : sample.raw;
                    const float mapped = flags & 2 ? glm::clamp (-1 + 4 * transformed, 0.0f, 1.0f) : -1 + 4 * transformed;
                    const float target = operation == "remap" ? mapped : operation == "multiply" ? .5f * mapped
                        : operation == "add" ? .5f + mapped : .5f - mapped;
                    const float weight = sample.initializer ? 1 : age * .5f;
                    applyParticleControlPointRemap (particle, remap, points, sample.initializer);
                    CHECK (particle.position.x == Catch::Approx (32 + 64 * (target - .5f) * weight).margin (.0001));
                    CHECK (particle.position.y == 12);
                    CHECK (particle.position.z == -8);
                    CHECK (particle.size == 0);
                }
            }
        }
    }
}
