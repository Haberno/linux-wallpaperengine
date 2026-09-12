#include "VolumetricLights.h"
#include "CScene.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Render/Objects/CLight.h"
#include "WallpaperEngine/Render/Shaders/Shader.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Render::Wallpapers;
using namespace WallpaperEngine::Data::Model;

namespace {
// Keep the stock shader's D3D screen-depth reconstruction intact. Only its
// raster position crosses into GL's bottom-left, [-1,1] clip convention.
GLuint compile (CScene& scene, const std::string& name, const ComboMap& combos, bool volume) {
    Shaders::Shader shader (scene.getAssetLocator (), name, combos, {}, {}, {}, {});
    std::string vertex = shader.vertex ();
    if (volume) {
        const auto main = vertex.find ("main", vertex.find ("void main"));
        vertex.replace (main, 4, "volumeMain");
        vertex += "\nvoid main() { volumeMain(); gl_Position.y = -gl_Position.y;"
                  " gl_Position.z = gl_Position.z * 2.0 - gl_Position.w; }\n";
    }
    const auto [vert, frag] = Shaders::GLSLContext::get ().toGlsl (vertex, shader.fragment ());
    return scene.getContext ().getShaderProgramCache ().createProgram (vert, frag);
}

glm::mat4 nativeClip () {
    glm::mat4 matrix (1.0f);
    matrix[1][1] = -1.0f;
    matrix[2][2] = 0.5f;
    matrix[3][2] = 0.5f;
    return matrix;
}

void matrix (GLuint program, const char* name, const glm::mat4& value) {
    glUniformMatrix4fv (glGetUniformLocation (program, name), 1, GL_FALSE, glm::value_ptr (value));
}

void vector (GLuint program, const char* name, const glm::vec4& value) {
    glUniform4fv (glGetUniformLocation (program, name), 1, glm::value_ptr (value));
}
}

VolumetricLights::VolumetricLights (CScene& scene) : m_scene (scene) {
    WallpaperEngine::Data::Utils::ScopeGuard cleanup ([&] {
        glDeleteProgram (m_backProgram);
        glDeleteProgram (m_combineProgram);
        glDeleteTextures (1, &m_depthTexture);
        glDeleteSamplers (1, &m_sampler);
        glDeleteSamplers (1, &m_shadowSampler);
    });
    m_backProgram = compile (scene, "volumetricsback", {}, true);
    m_combineProgram = compile (scene, "passthrough", {}, false);
    glGenTextures (1, &m_depthTexture);
    glGenSamplers (1, &m_sampler);
    glSamplerParameteri (m_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri (m_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri (m_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri (m_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri (m_sampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glGenSamplers (1, &m_shadowSampler);
    glSamplerParameteri (m_shadowSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri (m_shadowSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri (m_shadowSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glSamplerParameteri (m_shadowSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glSamplerParameteri (m_shadowSampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glSamplerParameteri (m_shadowSampler, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    const float border[] = { 1, 1, 1, 1 };
    glSamplerParameterfv (m_shadowSampler, GL_TEXTURE_BORDER_COLOR, border);
    resize ();
    cleanup.cancel ();
}

VolumetricLights::~VolumetricLights () {
    for (const auto& [key, program] : m_frontPrograms) glDeleteProgram (program);
    glDeleteProgram (m_backProgram);
    glDeleteProgram (m_combineProgram);
    glDeleteSamplers (1, &m_sampler);
    glDeleteSamplers (1, &m_shadowSampler);
    glDeleteTextures (1, &m_depthTexture);
    glDeleteFramebuffers (1, &m_depthFramebuffer);
    glDeleteVertexArrays (1, &m_vertexArray);
    glDeleteBuffers (1, &m_vertexBuffer);
}

void VolumetricLights::resize () {
    const auto size = glm::max (glm::uvec2 (m_scene.getFramebufferSize ()), glm::uvec2 (1));
    if (size == m_size) return;
    m_size = size;
    // Native QUALITY 3/4 uses quarter-size volume targets and no extra blur.
    const auto reduced = glm::max (size / 4u, glm::uvec2 (1));
    if (m_back) {
        m_back->resize (reduced.x, reduced.y);
        m_lightBuffer->resize (reduced.x, reduced.y);
    } else {
        m_back = std::make_shared<CFBO> ("_rt_volumetricsSingle", TextureFormat_ARGB8888,
            TextureFlags_ClampUVs, 1, reduced.x, reduced.y, reduced.x, reduced.y, false, true);
        m_lightBuffer = std::make_shared<CFBO> ("_rt_volumetricsLightBuffer", m_scene.getColorFormat (),
            TextureFlags_ClampUVs, 1, reduced.x, reduced.y, reduced.x, reduced.y);
    }
    // A depth blit requires exactly the source attachment's storage format.
    glBindTexture (GL_TEXTURE_2D, m_depthTexture);
    glTexImage2D (GL_TEXTURE_2D, 0,
        m_scene.getFBO ()->getSamples () > 1 ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT24,
        size.x, size.y, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
}

GLuint VolumetricLights::frontProgram (bool shadow, bool fullscreen) {
    const auto& fog = m_scene.getFog ();
    const int key = shadow | (fullscreen << 1) | (fog.distanceEnabled << 2) | (fog.heightEnabled << 3);
    if (const auto found = m_frontPrograms.find (key); found != m_frontPrograms.end ()) return found->second;
    return m_frontPrograms[key] = compile (m_scene, "volumetricsfront", {
        { "SHADOW", shadow }, { "FULLSCREEN", fullscreen }, { "QUALITY", 4 },
        { "FOG", 1 }, { "FOG_DIST", fog.distanceEnabled }, { "FOG_HEIGHT", fog.heightEnabled }
    }, true);
}

void VolumetricLights::draw (GLuint program, bool fullscreen) {
    const GLint position = glGetAttribLocation (program, "a_Position");
    const GLint uv = glGetAttribLocation (program, "a_TexCoord");
    glBindBuffer (GL_ARRAY_BUFFER, m_vertexBuffer);
    glEnableVertexAttribArray (position);
    glVertexAttribPointer (position, 3, GL_FLOAT, GL_FALSE, 5 * sizeof (float), nullptr);
    if (uv >= 0) {
        glEnableVertexAttribArray (uv);
        glVertexAttribPointer (uv, 2, GL_FLOAT, GL_FALSE, 5 * sizeof (float), reinterpret_cast<void*> (3 * sizeof (float)));
    }
    glDrawArrays (GL_TRIANGLES, fullscreen ? 384 : 0, fullscreen ? 3 : 384);
    glDisableVertexAttribArray (position);
    if (uv >= 0) glDisableVertexAttribArray (uv);
}

void VolumetricLights::render (
    const std::vector<Objects::CLight*>& lightObjects, const std::vector<Objects::CLight*>& batch
) {
    resize ();
    const auto target = m_scene.find ("_rt_FullFrameBuffer");
    if (m_vertexArray == GL_NONE) {
        // Native 140196ce0: 32 circular segments in light clip space, closed at
        // z=0/1 and transformed by the inverse spotlight projection.
        std::vector<float> vertices;
        const auto vertex = [&] (glm::vec3 p, glm::vec2 uv = {}) {
            vertices.insert (vertices.end (), { p.x, p.y, p.z, uv.x, uv.y });
        };
        for (int i = 0; i < 32; ++i) {
            const float a = float (i) * glm::two_pi<float> () / 32.0f;
            const float b = float (i + 1) * glm::two_pi<float> () / 32.0f;
            const glm::vec3 p (std::sin (a), -std::cos (a), 0), q (std::sin (b), -std::cos (b), 0);
            const auto r = p + glm::vec3 (0, 0, 1), s = q + glm::vec3 (0, 0, 1);
            vertex (r); vertex (p); vertex (q); vertex (r); vertex (q); vertex (s);
            vertex ({ .5f, .5f, 0 }); vertex (q); vertex (p);
            vertex ({ .5f, .5f, 1 }); vertex (r); vertex (s);
        }
        vertex ({ -1,-1,0 }, { 0,0 }); vertex ({ 3,-1,0 }, { 2,0 }); vertex ({ -1,3,0 }, { 0,2 });
        glGenVertexArrays (1, &m_vertexArray);
        glGenBuffers (1, &m_vertexBuffer);
        glBindBuffer (GL_ARRAY_BUFFER, m_vertexBuffer);
        glBufferData (GL_ARRAY_BUFFER, vertices.size () * sizeof (float), vertices.data (), GL_STATIC_DRAW);
        glGenFramebuffers (1, &m_depthFramebuffer);
        glBindFramebuffer (GL_FRAMEBUFFER, m_depthFramebuffer);
        glFramebufferTexture2D (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTexture, 0);
        glDrawBuffer (GL_NONE);
        glReadBuffer (GL_NONE);
    }
    GLint samplers[4] {}, previousFront, previousArray;
    glGetIntegerv (GL_FRONT_FACE, &previousFront);
    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &previousArray);
    for (GLuint unit = 0; unit < 4; ++unit) {
        glGetIntegeri_v (GL_SAMPLER_BINDING, unit, &samplers[unit]);
        glBindSampler (unit, unit == 0 ? m_shadowSampler : m_sampler);
    }
    WallpaperEngine::Data::Utils::ScopeGuard restore ([&] {
        for (GLuint unit = 0; unit < 4; ++unit) glBindSampler (unit, samplers[unit]);
        glFrontFace (previousFront);
        glBindVertexArray (previousArray);
        glActiveTexture (GL_TEXTURE0);
        glBindFramebuffer (GL_FRAMEBUFFER, target->getDrawFramebuffer ());
        glViewport (0, 0, m_size.x, m_size.y);
    });
    glDisable (GL_SCISSOR_TEST);
    glDisable (GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDepthMask (GL_TRUE);
    glBindFramebuffer (GL_READ_FRAMEBUFFER, target->getDrawFramebuffer ());
    glBindFramebuffer (GL_DRAW_FRAMEBUFFER, m_depthFramebuffer);
    glBlitFramebuffer (0, 0, m_size.x, m_size.y, 0, 0, m_size.x, m_size.y, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindVertexArray (m_vertexArray);
    glBindFramebuffer (GL_FRAMEBUFFER, m_lightBuffer->getFramebuffer ());
    glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    const float black[] = { 0, 0, 0, 0 };
    glClearBufferfv (GL_COLOR, 0, black);
    const auto& camera = m_scene.getCamera ();
    const auto viewProjection = nativeClip () * camera.getProjection () * camera.getLookAt ();
    const auto& lights = m_scene.getLights ();
    const float time = m_scene.getTime ();
    size_t spot = 0;
    for (const auto* light : lightObjects) {
        const auto& data = light->getLight ();
        if (data.type != LightData::Type_Spot) continue;
        const size_t index = spot++;
        if (std::ranges::find (batch, light) == batch.end ()) continue;
        if (!data.castVolumetrics->value->getBool () || data.density->evaluateFloat (time) <= 0
            || glm::length (light->getPremultipliedColor ()) <= 0) continue;
        const auto origin = light->getWorldPosition (), direction = light->getWorldDirection ();
        const float radius = data.radius->evaluateFloat (time);
        if (radius <= 0) continue;
        const auto lightProjection = nativeClip () * Objects::CLight::calculateSpotShadowViewProjection (
            origin, direction, data.outerCone->evaluateFloat (time), radius);
        const auto inverseLight = glm::inverse (lightProjection);
        glViewport (0, 0, m_back->getRealWidth (), m_back->getRealHeight ());
        glBindFramebuffer (GL_FRAMEBUFFER, m_back->getFramebuffer ());
        glDepthMask (GL_TRUE);
        glClearDepth (1.0);
        glClear (GL_DEPTH_BUFFER_BIT);
        glEnable (GL_DEPTH_TEST);
        glDepthFunc (GL_LESS);
        glEnable (GL_CULL_FACE);
        glFrontFace (camera.isYFlipped () ? GL_CW : GL_CCW);
        glCullFace (GL_FRONT);
        glDisable (GL_BLEND);
        glUseProgram (m_backProgram);
        matrix (m_backProgram, "g_ViewProjectionMatrix", viewProjection);
        matrix (m_backProgram, "g_AltViewProjectionMatrix", inverseLight);
        draw (m_backProgram, false);
        const auto cameraPoint = camera.getEye () + glm::normalize (camera.getCenter () - camera.getEye ()) * camera.getNearZ ();
        const auto delta = cameraPoint - origin;
        const float axial = glm::dot (delta, direction);
        const bool inside = axial >= 0 && axial <= radius
            && glm::length (delta - direction * axial) <= axial * std::tan (glm::radians (data.outerCone->evaluateFloat (time)));
        // Script-created lights can outnumber the scene's load-time shadow slots.
        const int feature = index < lights.spotShadowFeatures.size () ? lights.spotShadowFeatures[index] : -1;
        const bool shadow = feature >= 0 && index < lights.spotShadowEnabled.size ()
            && lights.spotShadowEnabled[index] > .5f;
        const GLuint program = frontProgram (shadow, inside);
        glBindFramebuffer (GL_FRAMEBUFFER, m_lightBuffer->getFramebuffer ());
        glDisable (GL_DEPTH_TEST);
        glDepthMask (GL_FALSE);
        glCullFace (GL_BACK);
        if (inside) glDisable (GL_CULL_FACE);
        glEnable (GL_BLEND);
        glBlendEquation (GL_FUNC_ADD);
        glBlendFuncSeparate (GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
        glUseProgram (program);
        matrix (program, "g_ViewProjectionMatrix", viewProjection);
        matrix (program, "g_AltViewProjectionMatrix", inverseLight);
        matrix (program, "g_EffectModelMatrix", glm::inverse (viewProjection));
        matrix (program, "g_AltModelMatrix", shadow ? nativeClip () * lights.shadowMatrices[feature] : lightProjection);
        vector (program, "g_RenderVar0", shadow ? lights.shadowTransforms[feature] : glm::vec4 (0));
        const auto cones = Objects::CLight::calculateSpotConeCosines (data.innerCone->evaluateFloat (time), data.outerCone->evaluateFloat (time));
        vector (program, "g_RenderVar1", { radius * .99f, cones.x, cones.y, data.intensity->evaluateFloat (time) });
        vector (program, "g_RenderVar2", glm::vec4 (origin, data.density->evaluateFloat (time)));
        vector (program, "g_RenderVar3", glm::vec4 (direction, 0));
        vector (program, "g_RenderVar4", glm::vec4 (data.color->evaluateVec3 (time), data.volumetricsExponent->evaluateFloat (time)));
        vector (program, "g_Texture1Resolution", { m_size.x, m_size.y, m_size.x, m_size.y });
        vector (program, "g_Texture3Resolution", *m_back->getResolution ());
        vector (program, "g_FogDistanceParams", m_scene.getFog ().distanceParams);
        vector (program, "g_FogHeightParams", m_scene.getFog ().heightParams);
        glUniform3fv (glGetUniformLocation (program, "g_EyePosition"), 1, glm::value_ptr (camera.getEye ()));
        // Native clips against the copied scene depth before limiting the ray
        // to the cone's back face; swapping these makes foreground beams leak.
        const GLuint textures[] = { m_scene.find ("_rt_shadowAtlas")->getTextureID (0), m_depthTexture, 0, m_back->getTextureID (0) };
        for (GLuint unit : { 0u, 1u, 3u }) {
            glActiveTexture (GL_TEXTURE0 + unit);
            glBindTexture (GL_TEXTURE_2D, textures[unit]);
            glUniform1i (glGetUniformLocation (program, ("g_Texture" + std::to_string (unit)).c_str ()), unit);
        }
        draw (program, inside);
    }
    glBindFramebuffer (GL_FRAMEBUFFER, target->getDrawFramebuffer ());
    glViewport (0, 0, m_size.x, m_size.y);
    glDisable (GL_CULL_FACE);
    glDisable (GL_DEPTH_TEST);
    glEnable (GL_BLEND);
    glBlendEquation (GL_FUNC_ADD);
    glBlendFuncSeparate (GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    glBindSampler (0, m_sampler);
    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, m_lightBuffer->getTextureID (0));
    glUseProgram (m_combineProgram);
    glUniform1i (glGetUniformLocation (m_combineProgram, "g_Texture0"), 0);
    draw (m_combineProgram, true);
}
