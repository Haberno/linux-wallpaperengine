#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace WallpaperEngine::Render::Objects {
struct TextStyleInput {
    bool msdf = false;
    bool outline = false;
    bool blur = false;
    bool shadow = false;
    float outlineWidth = 4;
    glm::vec3 outlineColor { 0 };
    float blurRadius = 6;
    float shadowRadius = 6;
    float shadowOpacity = 1;
    glm::vec2 shadowOffset { 4 };
    glm::vec3 shadowColor { 0 };
};

struct TextStyleUniforms {
    bool msdf;
    glm::ivec3 enabled;
    glm::vec4 renderVar0;
    glm::vec4 renderVar1;
    glm::vec4 renderVar2;
    glm::vec4 renderVar3;
};

/** Native descriptor clamps followed by the font material's atlas-unit conversion. */
TextStyleUniforms computeTextStyleUniforms (const TextStyleInput& input, float pointSize);
}
