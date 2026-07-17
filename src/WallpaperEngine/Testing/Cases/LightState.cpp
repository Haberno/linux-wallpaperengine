#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Render/Objects/CLight.h"

// CEF exposes its own CHECK macro through CLight's scene includes.
#ifdef CHECK
#undef CHECK
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

using WallpaperEngine::Data::Model::LightData;
using WallpaperEngine::Render::Objects::CLight;

TEST_CASE ("Light type IDs match Wallpaper Engine") {
    CHECK (LightData::Type_Point == 0);
    CHECK (LightData::Type_Spot == 1);
    CHECK (LightData::Type_Tube == 2);
    CHECK (LightData::Type_Directional == 3);
}

TEST_CASE ("Spot cone angles are packed as cosine thresholds") {
    const glm::vec2 cones = CLight::calculateSpotConeCosines (20.0f, 35.0f);

    CHECK (cones.x == Catch::Approx (0.9396926f));
    CHECK (cones.y == Catch::Approx (0.8191520f));
    CHECK (cones.x > cones.y);
}

TEST_CASE ("Spot shadow projection covers the authored cone and radius") {
    const glm::vec3 origin (10.0f, 3.0f, -2.0f);
    const glm::vec3 direction = glm::normalize (glm::vec3 (-1.0f, -0.2f, 0.1f));
    const glm::mat4 projection = CLight::calculateSpotShadowViewProjection (origin, direction, 35.0f, 100.0f);

    const glm::vec4 centerClip = projection * glm::vec4 (origin + direction * 50.0f, 1.0f);
    const glm::vec3 centerNdc = glm::vec3 (centerClip) / centerClip.w;
    CHECK (std::isfinite (centerNdc.x));
    CHECK (std::isfinite (centerNdc.y));
    CHECK (std::isfinite (centerNdc.z));
    CHECK (std::abs (centerNdc.x) < 0.001f);
    CHECK (std::abs (centerNdc.y) < 0.001f);
    CHECK (centerNdc.z > -1.0f);
    CHECK (centerNdc.z < 1.0f);
}

TEST_CASE ("Vertical spot shadow projections remain finite") {
    const glm::mat4 projection = CLight::calculateSpotShadowViewProjection (
	glm::vec3 (0.0f), glm::vec3 (0.0f, 1.0f, 0.0f), 30.0f, 10.0f
    );

    for (int column = 0; column < 4; column++) {
	for (int row = 0; row < 4; row++) {
	    CHECK (std::isfinite (projection[column][row]));
	}
    }
}

TEST_CASE ("Tube control point follows the full world transform") {
    glm::mat4 world = glm::translate (glm::mat4 (1.0f), glm::vec3 (10.0f, -2.0f, 3.0f));
    world = glm::rotate (world, glm::half_pi<float> (), glm::vec3 (0.0f, 0.0f, 1.0f));
    world = glm::scale (world, glm::vec3 (2.0f));

    const glm::vec3 endpoint = CLight::calculateTubeEndPosition (world, glm::vec3 (3.0f, 0.0f, 0.0f));
    CHECK (endpoint.x == Catch::Approx (10.0f));
    CHECK (endpoint.y == Catch::Approx (4.0f));
    CHECK (endpoint.z == Catch::Approx (3.0f));
}
