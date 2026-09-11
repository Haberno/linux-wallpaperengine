#pragma once

#include "WallpaperEngine/Render/CFBO.h"
#include <array>

namespace WallpaperEngine::Render::Wallpapers {
class CScene;

/** Native HDR pyramid, with a separate SDR result so scene feedback stays unprocessed. */
class HdrBloom {
public:
    explicit HdrBloom (CScene& scene);
    ~HdrBloom ();
    void render (bool bloomEnabled);
    [[nodiscard]] const CFBO& output () const { return *m_output; }

private:
    enum Program { Extract, Downsample, Upsample, UpsampleCubic, Combine, Copy, ProgramCount };
    void resize ();
    void draw (Program program, const CFBO& source, const CFBO& target, float offset = 0.0f);
    CScene& m_scene;
    std::array<GLuint, ProgramCount> m_programs {};
    GLuint m_sampler = GL_NONE;
    GLuint m_vertexBuffer = GL_NONE;
    GLuint m_vertexArray = GL_NONE;
    std::vector<std::shared_ptr<CFBO>> m_levels;
    std::shared_ptr<CFBO> m_output;
    glm::uvec2 m_size {};
};
}
