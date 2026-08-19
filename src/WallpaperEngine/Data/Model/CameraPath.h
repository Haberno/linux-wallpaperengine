#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace WallpaperEngine::Data::Model {
/** Playback flags recovered from the reference runtime's path options. The bit
 * values are the reference's own so the same tests apply unchanged. */
enum CameraPathFlags : uint32_t {
    CameraPathMirror = 0x1,
    CameraPathSingle = 0x2,
    CameraPathRandom = 0x4,
    CameraPathWrapLoop = 0x10,
    CameraPathStartPaused = 0x20000000,
    /** Runtime state, not authored: a single-shot path reached its end. */
    CameraPathFinished = 0x40000000,
    /** Runtime state, not authored: a mirrored path is playing backwards. */
    CameraPathReversed = 0x80000000,
};

/** Tangent handle stored by Wallpaper Engine's animation-curve editor. The x
 * offset is normalized against the segment; y is an absolute value offset. The
 * offset stays zero unless the handle is enabled, matching the reference
 * loader, because the evaluator reads the offsets and never the flag. */
struct CameraPathHandle {
    bool enabled = false;
    glm::vec2 offset = glm::vec2 (0.0f);
};

struct CameraPathKeyframe {
    int frame = 0;
    float time = 0.0f;
    float value = 0.0f;
    /** Constant interpolation: the segment ending here holds the previous value. */
    bool step = false;
    CameraPathHandle incoming = {};
    CameraPathHandle outgoing = {};
};

/** One scalar animation channel, used for vector components as well as FOV/zoom. */
struct CameraPathChannel {
    std::vector<CameraPathKeyframe> keyframes = {};

    /** Legacy timestamped transforms, interpolated in seconds. */
    [[nodiscard]] float evaluateLegacy (float time, float fallback) const;
    /** Curve channels, baked for one whole frame - the granularity the
     * reference runtime samples at before blending two adjacent frames. */
    [[nodiscard]] float evaluateFrame (int frame, float fallback) const;
};

struct CameraTransform {
    glm::vec3 center = glm::vec3 (0.0f, 0.0f, -1.0f);
    glm::vec3 eye = glm::vec3 (0.0f);
    glm::vec3 up = glm::vec3 (0.0f, 1.0f, 0.0f);
    float fov = 50.0f;
    float zoom = 1.0f;
};

/** A single camera shot. Modern scenes store curve channels sampled per frame;
 * legacy scenes keep their timestamped transforms and Hermite interpolation. */
struct CameraPath {
    std::string name;
    float duration = 0.0f;
    float secondsPerFrame = 1.0f / 30.0f;
    int lengthFrames = 0;
    uint32_t flags = 0;
    bool legacy = false;
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
