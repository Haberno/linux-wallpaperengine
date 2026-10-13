#pragma once

#include <bit>
#include <cstdint>
#include <glm/glm.hpp>

namespace WallpaperEngine::Render::Utils {

inline glm::mat3 calculateNormalModelMatrix (const glm::mat4& model) {
    glm::mat3 normal (model);
    // Native g_NormalModelMatrix normalizes model columns directly, including
    // its one-step reciprocal estimate. Inverse transpose changes sheared axes.
    for (int column = 0; column < 3; ++column) {
	const float squared = glm::dot (normal[column], normal[column]);
	float inverse = std::bit_cast<float> (0x5f375a86u - (std::bit_cast<uint32_t> (squared) >> 1));
	inverse *= 1.5f - squared * 0.5f * inverse * inverse;
	normal[column] *= inverse;
    }
    return normal;
}

} // namespace WallpaperEngine::Render::Utils
