#include "TextStyle.h"

#include <algorithm>
#include <cfloat>
#include <glm/geometric.hpp>

using namespace WallpaperEngine::Render::Objects;

TextStyleUniforms WallpaperEngine::Render::Objects::computeTextStyleUniforms (
    const TextStyleInput& input, const float pointSize
) {
    const float width = input.outline ? std::max (input.outlineWidth, 1.0f) : 0.0f;
    const float blur = input.blur ? std::max (input.blurRadius, .01f) : 0.0f;
    const float shadow = input.shadow ? std::max (input.shadowRadius, .01f) : 0.0f;
    const glm::vec2 offset = input.shadow ? input.shadowOffset : glm::vec2 (0);
    const float scale = (32.0f / std::clamp (pointSize, 1.0f, 256.0f)) * .24f;
    constexpr float maxWidth = 5.1000004f;
    float scaledWidth = std::min (width * scale, maxWidth);
    const float scaledBlur = std::min (blur * scale, 6.0f);
    if (scaledWidth + scaledBlur > maxWidth) scaledWidth = std::max (maxWidth - scaledBlur, 0.0f);
    return {
        .msdf = input.msdf || input.outline || input.blur || input.shadow,
        .enabled = { width >= 1.0f, blur > 0.0f, shadow > 0.0f || glm::dot (offset, offset) > FLT_EPSILON },
        .renderVar0 = { 24, scaledWidth, scaledBlur, std::min (shadow * scale, 6.0f) },
        .renderVar1 = { input.outlineColor, std::min (offset.x * scale, 6.0f) },
        .renderVar2 = { input.shadowColor, std::min (offset.y * scale, 6.0f) },
        .renderVar3 = { input.shadowOpacity, 0, 0, 0 },
    };
}
