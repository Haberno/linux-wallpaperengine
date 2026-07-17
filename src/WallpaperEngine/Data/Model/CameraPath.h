#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace WallpaperEngine::Data::Model {
/** Tangent handle stored by Wallpaper Engine's animation-curve editor. The
 * offset is measured from its keyframe in seconds/value units. */
struct CameraPathHandle {
    bool enabled = false;
    glm::vec2 offset = glm::vec2 (0.0f);
};

struct CameraPathKeyframe {
    float time = 0.0f;
    float value = 0.0f;
    CameraPathHandle incoming = {};
    CameraPathHandle outgoing = {};
};

/** One scalar animation channel, used for vector components as well as FOV/zoom. */
struct CameraPathChannel {
    std::vector<CameraPathKeyframe> keyframes = {};

    [[nodiscard]] float evaluate (float time, float fallback) const;
};

struct CameraTransform {
    glm::vec3 center = glm::vec3 (0.0f, 0.0f, -1.0f);
    glm::vec3 eye = glm::vec3 (0.0f);
    glm::vec3 up = glm::vec3 (0.0f, 1.0f, 0.0f);
    float fov = 50.0f;
    float zoom = 1.0f;
};

/** A single camera shot. Modern scenes store curve channels; legacy scenes
 * are converted to the same representation from timestamped transforms. */
struct CameraPath {
    std::string name;
    float duration = 0.0f;
    std::array<CameraPathChannel, 3> center = {};
    std::array<CameraPathChannel, 3> eye = {};
    std::array<CameraPathChannel, 3> up = {};
    CameraPathChannel fov = {};
    CameraPathChannel zoom = {};

    [[nodiscard]] CameraTransform evaluate (float time, const CameraTransform& fallback) const;
};

/** One authored path queue. objectId is empty for the legacy top-level
 * camera.paths form and set for a camera object's path field. */
struct CameraPathSource {
    std::optional<int> objectId = std::nullopt;
    std::string queueMode = "sequence";
    std::vector<CameraPath> paths = {};
};
} // namespace WallpaperEngine::Data::Model
