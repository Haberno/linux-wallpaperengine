#pragma once

#include "MdlAnimation.h"
#include <limits>

namespace WallpaperEngine::Data::Model {
/** Native 140226a10 unions every enabled clip without weighting its volume. */
inline std::optional<MdlAnimationBounds> evaluateMdlAnimationBounds (
    const std::vector<MdlActiveAnimation>& layers, const MdlAnimationBounds& fallback
) {
    MdlAnimationBounds result { glm::vec3 (std::numeric_limits<float>::max ()),
        glm::vec3 (-std::numeric_limits<float>::max ()) };
    bool hasVolume = false;
    for (const auto& layer : layers) {
        if (!layer.enabled || layer.animation == nullptr) continue;
        const auto& bounds = layer.animation->bounds;
        hasVolume |= bounds.minimum.x < bounds.maximum.x;
        result.minimum = glm::min (result.minimum, bounds.minimum);
        result.maximum = glm::max (result.maximum, bounds.maximum);
    }
    if (hasVolume) return result;
    if (fallback.minimum.x < fallback.maximum.x) return fallback;
    return std::nullopt;
}
} // namespace WallpaperEngine::Data::Model
