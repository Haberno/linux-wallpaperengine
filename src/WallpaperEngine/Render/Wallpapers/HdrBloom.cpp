#include "HdrBloom.h"
#include "CScene.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Render/Shaders/Shader.h"
#include "WallpaperEngine/Render/Shaders/GLSLContext.h"
#include <algorithm>
#include <bit>
#include <cmath>

using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Render::Wallpapers;
using namespace WallpaperEngine::Data::Model;

HdrBloom::HdrBloom (CScene& scene) : m_scene (scene) {
    const std::array<ComboMap, ProgramCount> combos {{
        {{ "BLOOM", 1 }}, {}, {{ "UPSAMPLE", 1 }}, {{ "UPSAMPLE", 1 }, { "BICUBIC", 1 }},
        // Native SDR Ultra decodes into a linear scRGB float swapchain. Our SDR
        // RGBA8 surface carries encoded values: this stock variant is the net
        // decode/encode result at SDR white factor 1 (not a native pass selector).
        {{ "LINEAR", 1 }}, {}
    }};
    try {
        for (size_t i = 0; i < m_programs.size (); ++i) {
            const std::string name = i == Combine ? "combine_hdr" : i == Copy ? "passthrough" : "hdr_downsample";
            Shaders::Shader shader (scene.getAssetLocator (), name, combos[i], {}, {}, {}, {});
            const auto [vertex, fragment] = Shaders::GLSLContext::get ().toGlsl (shader.vertex (), shader.fragment ());
            m_programs[i] = scene.getContext ().getShaderProgramCache ().createProgram (vertex, fragment);
        }
        glGenSamplers (1, &m_sampler);
        glSamplerParameteri (m_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri (m_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri (m_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri (m_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        resize ();
    } catch (...) {
        for (const auto program : m_programs) glDeleteProgram (program);
        glDeleteSamplers (1, &m_sampler);
        throw;
    }
}

HdrBloom::~HdrBloom () {
    glDeleteSamplers (1, &m_sampler);
    for (const auto program : m_programs) glDeleteProgram (program);
    glDeleteVertexArrays (1, &m_vertexArray);
    glDeleteBuffers (1, &m_vertexBuffer);
}

void HdrBloom::resize () {
    const auto size = glm::max (glm::uvec2 (m_scene.getFramebufferSize ()), glm::uvec2 (2));
    if (size == m_size) return;
    m_size = size;
    const auto count = static_cast<uint32_t> (std::min (8, std::bit_width (std::min (size.x, size.y)) - 1));
    m_levels.resize (count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto levelSize = glm::max (size / (2u << i), glm::uvec2 (2));
        if (m_levels[i]) m_levels[i]->resize (levelSize.x, levelSize.y);
        else m_levels[i] = std::make_shared<CFBO> (
            "_rt_HDRBloom_" + std::to_string (i), TextureFormat_RGBA16161616f, TextureFlags_ClampUVs,
            1.0f, levelSize.x, levelSize.y, levelSize.x, levelSize.y
        );
    }
    if (m_output) m_output->resize (size.x, size.y);
    else m_output = std::make_shared<CFBO> (
        "_rt_HDROutput", TextureFormat_ARGB8888, TextureFlags_ClampUVs,
        1.0f, size.x, size.y, size.x, size.y
    );
}

void HdrBloom::draw (const Program kind, const CFBO& source, const CFBO& target, const float offset) {
    const GLuint program = m_programs[kind];
    glUseProgram (program);
    glBindFramebuffer (GL_FRAMEBUFFER, target.getDrawFramebuffer ());
    glViewport (0, 0, target.getRealWidth (), target.getRealHeight ());
    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, source.getTextureID (0));
    glUniform1i (glGetUniformLocation (program, "g_Texture0"), 0);
    const glm::vec2 step = offset / glm::vec2 (m_size);
    glUniform4f (glGetUniformLocation (program, "g_RenderVar0"), step.x, step.y, -step.x, -step.y);
    const GLint position = glGetAttribLocation (program, "a_Position");
    const GLint uv = glGetAttribLocation (program, "a_TexCoord");
    glBindBuffer (GL_ARRAY_BUFFER, m_vertexBuffer);
    glEnableVertexAttribArray (position);
    glVertexAttribPointer (position, 3, GL_FLOAT, GL_FALSE, 5 * sizeof (float), nullptr);
    if (uv >= 0) {
        glEnableVertexAttribArray (uv);
        glVertexAttribPointer (uv, 2, GL_FLOAT, GL_FALSE, 5 * sizeof (float), reinterpret_cast<void*> (3 * sizeof (float)));
    }
    glDrawArrays (GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray (position);
    if (uv >= 0) glDisableVertexAttribArray (uv);
}

void HdrBloom::render (const bool bloomEnabled) {
    resize ();
    // Scene --clamp controls presentation. Native bloom samples always use
    // linear filtering and edge clamp, including taps outside the scene bounds.
    GLint samplers[2] {};
    for (GLuint unit = 0; unit < 2; ++unit) {
        glGetIntegeri_v (GL_SAMPLER_BINDING, unit, &samplers[unit]);
        glBindSampler (unit, m_sampler);
    }
    WallpaperEngine::Data::Utils::ScopeGuard restoreSamplers ([&] {
        for (GLuint unit = 0; unit < 2; ++unit) glBindSampler (unit, samplers[unit]);
    });
    if (m_vertexArray == GL_NONE) {
        // Only GL storage is shared with the build context; create containers on first draw.
        const float vertices[] = { -1,-1,0, 0,0, 3,-1,0, 2,0, -1,3,0, 0,2 };
        glGenVertexArrays (1, &m_vertexArray);
        glGenBuffers (1, &m_vertexBuffer);
        glBindBuffer (GL_ARRAY_BUFFER, m_vertexBuffer);
        glBufferData (GL_ARRAY_BUFFER, sizeof (vertices), vertices, GL_STATIC_DRAW);
    }
    glBindVertexArray (m_vertexArray);
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_CULL_FACE);
    glDisable (GL_SCISSOR_TEST);
    glDisable (GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable (GL_BLEND);
    glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    const auto source = m_scene.getFBO ();
    if (!bloomEnabled) {
        // Native decode to linear followed by SDR presentation encoding is an identity.
        draw (Copy, *source, *m_output);
        return;
    }
    const auto& settings = m_scene.getScene ().camera.bloom.hdr;
    const float time = m_scene.getTime ();
    const int count = std::clamp (static_cast<int> (settings.iterations->evaluateFloat (time)), 1, int (m_levels.size ()));
    const float scatter = settings.scatter->evaluateFloat (time);
    const float threshold = settings.threshold->evaluateFloat (time);
    const float knee = threshold * settings.feather->evaluateFloat (time);
    const float strength = settings.strength->evaluateFloat (time) / (1.0f + std::pow (scatter, std::max (2, count) - 2));
    const auto tint = m_scene.getScene ().camera.bloom.tint->evaluateVec3 (time);
    glUseProgram (m_programs[Extract]);
    glUniform1f (glGetUniformLocation (m_programs[Extract], "g_BloomStrength"), strength);
    glUniform3fv (glGetUniformLocation (m_programs[Extract], "g_BloomTint"), 1, &tint.x);
    glUniform4f (glGetUniformLocation (m_programs[Extract], "g_BloomBlendParams"),
        threshold, threshold - knee, knee * 2.0f, 0.25f / (knee + 0.00001f));
    draw (Extract, *source, *m_levels[0], 1.0f);
    for (int i = 1; i < count; ++i) draw (Downsample, *m_levels[i - 1], *m_levels[i], float (1u << i));
    glEnable (GL_BLEND);
    glBlendEquation (GL_FUNC_ADD);
    glBlendFuncSeparate (GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    for (int i = count - 1; i >= 1; --i) {
        const auto kind = i >= count - 2 ? UpsampleCubic : Upsample;
        glUseProgram (m_programs[kind]);
        glUniform1f (glGetUniformLocation (m_programs[kind], "g_BloomScatter"), scatter);
        draw (kind, *m_levels[i], *m_levels[i - 1], float (1u << i));
    }
    glDisable (GL_BLEND);
    glUseProgram (m_programs[Combine]);
    glActiveTexture (GL_TEXTURE1);
    glBindTexture (GL_TEXTURE_2D, m_levels[0]->getTextureID (0));
    glUniform1i (glGetUniformLocation (m_programs[Combine], "g_Texture1"), 1);
    glUniform2f (glGetUniformLocation (m_programs[Combine], "g_TexelSize"), 1.0f / m_size.x, 1.0f / m_size.y);
    draw (Combine, *source, *m_output);
    glActiveTexture (GL_TEXTURE1);
    glBindTexture (GL_TEXTURE_2D, 0);
    glActiveTexture (GL_TEXTURE0);
}
