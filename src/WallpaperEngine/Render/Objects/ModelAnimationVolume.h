#pragma once

#include "WallpaperEngine/Data/Model/MdlAnimation.h"

namespace WallpaperEngine::Render::Objects {
/** Native uses scene planes before model virtual perspective and composition transforms.
 * Convert the conventional CPU view-projection to native reverse [0,1] depth;
 * 1401849e0 still extracts both Z planes as row4 +/- row3 in that convention.
 */
inline bool modelAnimationVolumeIntersects (const Data::Model::MdlAnimationBounds& bounds,
    const glm::mat4& model, const glm::mat4& sceneViewProjection) {
    const glm::vec3 minimum = model * glm::vec4 (bounds.minimum, 1.0f);
    const glm::vec3 maximum = model * glm::vec4 (bounds.maximum, 1.0f);
    const glm::vec3 halfDelta = (maximum - minimum) * 0.5f;
    const glm::vec3 center = minimum + halfDelta;
    const float radius = glm::length (halfDelta);
    for (int axis = 0; axis < 3; ++axis) {
        for (const float sign : { -1.0f, 1.0f }) {
            glm::vec4 plane;
            for (int column = 0; column < 4; ++column) {
                const float component = axis == 2
                    ? (sceneViewProjection[column][3] - sceneViewProjection[column][2]) * 0.5f
                    : sceneViewProjection[column][axis];
                plane[column] = sceneViewProjection[column][3] + sign * component;
            }
            const float normalLength = glm::length (glm::vec3 (plane));
            if (normalLength == 0.0f) continue;
            const float distance = (glm::dot (glm::vec3 (plane), center) + plane.w) / normalLength;
            if (distance < -radius) return false;
        }
    }
    return true;
}
} // namespace WallpaperEngine::Render::Objects
