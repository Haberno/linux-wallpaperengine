#pragma once

#include "WallpaperEngine/Render/CFBO.h"
#include <map>

namespace WallpaperEngine::Render::Objects { class CLight; }
namespace WallpaperEngine::Render::Wallpapers {
class CScene;

/** Authored spotlight volumes, using the installed native front/back shaders. */
class VolumetricLights {
public:
    explicit VolumetricLights (CScene& scene);
    ~VolumetricLights ();
    void render (const std::vector<Objects::CLight*>& lights, const std::vector<Objects::CLight*>& batch);

private:
    void resize ();
    GLuint frontProgram (bool shadow, bool fullscreen);
    void draw (GLuint program, bool fullscreen);
    CScene& m_scene;
    std::map<int, GLuint> m_frontPrograms;
    GLuint m_backProgram = GL_NONE;
    GLuint m_combineProgram = GL_NONE;
    GLuint m_vertexArray = GL_NONE;
    GLuint m_vertexBuffer = GL_NONE;
    GLuint m_depthTexture = GL_NONE;
    GLuint m_depthFramebuffer = GL_NONE;
    GLuint m_sampler = GL_NONE;
    GLuint m_shadowSampler = GL_NONE;
    std::shared_ptr<CFBO> m_back;
    std::shared_ptr<CFBO> m_lightBuffer;
    glm::uvec2 m_size {};
};
}
