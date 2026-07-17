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

TEST_CASE ("Directional shadow cascades enclose their camera frustum") {
    const glm::vec3 eye (0.0f, 0.0f, 5.0f);
    const glm::vec3 center (0.0f);
    const float nearDistance = 0.1f;
    const float farDistance = 10.0f;
    const float fieldOfView = 60.0f;
    const float aspect = 16.0f / 9.0f;
    const glm::mat4 projection = CLight::calculateDirectionalShadowViewProjection (
	eye, center, glm::vec3 (0.0f, 1.0f, 0.0f), fieldOfView, aspect, 1.0f, nearDistance, farDistance,
	glm::normalize (glm::vec3 (-1.0f, -1.0f, -1.0f)), 512
    );

    for (const float distance : { nearDistance, farDistance }) {
	const float halfHeight = std::tan (glm::radians (fieldOfView) * 0.5f) * distance;
	const float halfWidth = halfHeight * aspect;
	for (const float y : { -1.0f, 1.0f }) {
	    for (const float x : { -1.0f, 1.0f }) {
		const glm::vec3 point = eye + glm::vec3 (x * halfWidth, y * halfHeight, -distance);
		const glm::vec4 clip = projection * glm::vec4 (point, 1.0f);
		const glm::vec3 ndc = glm::vec3 (clip) / clip.w;
		CHECK (std::isfinite (ndc.x));
		CHECK (std::isfinite (ndc.y));
		CHECK (std::isfinite (ndc.z));
		CHECK (std::abs (ndc.x) <= 1.001f);
		CHECK (std::abs (ndc.y) <= 1.001f);
		CHECK (std::abs (ndc.z) <= 1.001f);
	    }
	}
    }
}

TEST_CASE ("Directional shadow cascades remain finite for vertical light directions") {
    const glm::mat4 projection = CLight::calculateDirectionalShadowViewProjection (
	glm::vec3 (0.0f, 0.0f, 5.0f), glm::vec3 (0.0f), glm::vec3 (0.0f, 1.0f, 0.0f), 60.0f,
	16.0f / 9.0f, 1.0f, 0.1f, 10.0f, glm::vec3 (0.0f, 1.0f, 0.0f), 512
    );

    for (int column = 0; column < 4; column++) {
	for (int row = 0; row < 4; row++) {
	    CHECK (std::isfinite (projection[column][row]));
	}
    }
}

TEST_CASE ("Directional shadow cascades suppress sub-texel camera movement") {
    const glm::vec3 lightDirection = glm::normalize (glm::vec3 (-1.0f, -1.0f, -1.0f));
    const glm::mat4 original = CLight::calculateDirectionalShadowViewProjection (
	glm::vec3 (0.0f, 0.0f, 5.0f), glm::vec3 (0.0f), glm::vec3 (0.0f, 1.0f, 0.0f), 60.0f,
	16.0f / 9.0f, 1.0f, 0.1f, 10.0f, lightDirection, 512
    );
    const glm::vec3 movement (0.00001f, 0.0f, 0.0f);
    const glm::mat4 moved = CLight::calculateDirectionalShadowViewProjection (
	glm::vec3 (0.0f, 0.0f, 5.0f) + movement, glm::vec3 (0.0f) + movement,
	glm::vec3 (0.0f, 1.0f, 0.0f), 60.0f, 16.0f / 9.0f, 1.0f, 0.1f, 10.0f, lightDirection, 512
    );

    for (int column = 0; column < 4; column++) {
	for (int row = 0; row < 4; row++) {
	    CHECK (moved[column][row] == Catch::Approx (original[column][row]).margin (0.000001f));
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
