#include "WallpaperEngine/Render/Objects/CParticle.h"

#ifdef CHECK
#undef CHECK
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using WallpaperEngine::Render::Objects::calculateParticleEmissionRate;
using WallpaperEngine::Render::Objects::calculateParticleSimulationDelta;
using WallpaperEngine::Render::Objects::calculateRopeTrailVisualValue;
using WallpaperEngine::Render::Objects::convertParticleRotationForRender;

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

TEST_CASE ("rope trails use the live particle visual state", "[particle]") {
    CHECK (calculateRopeTrailVisualValue (0.25f, 0.0f, false) == Catch::Approx (0.25f));
    CHECK (calculateRopeTrailVisualValue (0.25f, 0.5f, false) == Catch::Approx (0.25f));
    CHECK (calculateRopeTrailVisualValue (0.25f, 1.0f, false) == Catch::Approx (0.25f));

    CHECK (calculateRopeTrailVisualValue (0.25f, 0.0f, true) == Catch::Approx (0.0f));
    CHECK (calculateRopeTrailVisualValue (0.25f, 0.5f, true) == Catch::Approx (0.125f));
    CHECK (calculateRopeTrailVisualValue (0.25f, 1.0f, true) == Catch::Approx (0.25f));
}
