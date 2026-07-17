#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Render/Objects/CLight.h"

// CEF exposes its own CHECK macro through CLight's scene includes.
#ifdef CHECK
#undef CHECK
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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

TEST_CASE ("Tube control point follows the full world transform") {
    glm::mat4 world = glm::translate (glm::mat4 (1.0f), glm::vec3 (10.0f, -2.0f, 3.0f));
    world = glm::rotate (world, glm::half_pi<float> (), glm::vec3 (0.0f, 0.0f, 1.0f));
    world = glm::scale (world, glm::vec3 (2.0f));

    const glm::vec3 endpoint = CLight::calculateTubeEndPosition (world, glm::vec3 (3.0f, 0.0f, 0.0f));
    CHECK (endpoint.x == Catch::Approx (10.0f));
    CHECK (endpoint.y == Catch::Approx (4.0f));
    CHECK (endpoint.z == Catch::Approx (3.0f));
}
