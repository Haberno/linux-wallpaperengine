#pragma once

#include <GL/glew.h>
#include <glm/mat4x4.hpp>
#include <cmath>

namespace WallpaperEngine::Render {
/** Native finite reverse-Z perspective: near maps to 1, far to 0. */
inline glm::mat4 reversePerspective (float fov, float aspect, float nearZ, float farZ) {
    const float focal = 1.0f / std::tan (fov * 0.5f);
    glm::mat4 result (0.0f);
    result[0][0] = focal / aspect;
    result[1][1] = focal;
    result[2][2] = nearZ / (farZ - nearZ);
    result[2][3] = -1.0f;
    result[3][2] = nearZ * farZ / (farZ - nearZ);
    return result;
}

/** Clip control is context state; every draw restores the caller's convention. */
class ClipDepthScope {
public:
    static bool supported () { return (GLEW_VERSION_4_5 || GLEW_ARB_clip_control) && glClipControl; }
    explicit ClipDepthScope (bool reverse) : m_supported (supported ()) {
        if (!m_supported) return;
        glGetIntegerv (GL_CLIP_ORIGIN, &m_origin);
        glGetIntegerv (GL_CLIP_DEPTH_MODE, &m_depth);
        glClipControl (GL_LOWER_LEFT, reverse ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE);
    }
    ~ClipDepthScope () { if (m_supported) glClipControl (m_origin, m_depth); }
    ClipDepthScope (const ClipDepthScope&) = delete;
    ClipDepthScope& operator= (const ClipDepthScope&) = delete;

private:
    bool m_supported;
    GLint m_origin = GL_LOWER_LEFT;
    GLint m_depth = GL_NEGATIVE_ONE_TO_ONE;
};
}
