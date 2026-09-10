#include "WallpaperEngine/Render/Objects/CParticle.h"

#ifdef CHECK
#undef CHECK
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

using WallpaperEngine::Render::Objects::calculateParticleEmissionRate;
using WallpaperEngine::Render::Objects::calculateControlPointAttraction;
using WallpaperEngine::Render::Objects::calculateParticleSimulationDelta;
using WallpaperEngine::Render::Objects::calculateRopeTrailVisualValue;
using WallpaperEngine::Render::Objects::convertParticleRotationForRender;
using WallpaperEngine::Render::Objects::resolveParticleControlPoint;

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
