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
