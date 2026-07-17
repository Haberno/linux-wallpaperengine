#pragma once

#include "CRenderable.h"
#include "WallpaperEngine/Render/Objects/Effects/CPass.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

#include <glm/mat3x3.hpp>
#include <glm/mat4x3.hpp>
#include <limits>
#include <vector>

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Scripting;

/**
 * 3D model renderer for perspective scenes. Draws static and GPU-skinned MDLV
 * meshes through the regular material/pass pipeline (generic4 and friends)
 * with depth state taken from the material.
 */
class CModel final : public CRenderable, public ScriptableObject {
public:
    CModel (Wallpapers::CScene& scene, const Model3D& model);
    ~CModel () override;

    void setup () override;
    void render () override;
    /** Draw opaque mesh depth from a spotlight into the scene shadow atlas. */
    void renderShadow (const glm::mat4& lightViewProjection);

    [[nodiscard]] const Model3D& getModel () const;
    [[nodiscard]] std::optional<glm::mat4> getAttachmentTransform (const std::string& name) const override;

    [[nodiscard]] const float& getBrightness () const override;
    [[nodiscard]] const float& getUserAlpha () const override;
    [[nodiscard]] const float& getAlpha () const override;
    [[nodiscard]] const glm::vec3& getColor () const override;
    [[nodiscard]] const glm::vec4& getColor4 () const override;
    [[nodiscard]] const glm::vec3& getCompositeColor () const override;

private:
    struct SubmeshBuffers {
	GLuint vertexBuffer = GL_NONE;
	GLuint indexBuffer = GL_NONE;
	GLsizei indexCount = 0;
    };

    void setupGeometryCallback (Effects::CPass* pass, size_t submeshIndex);
    void setupShadowProgram ();
    void updateAnimationPose () const;
    void updateMatrices ();

    const Model3D& m_model;

    std::vector<SubmeshBuffers> m_submeshes = {};
    mutable std::vector<glm::mat4> m_worldBones = {};
    mutable std::vector<glm::mat4> m_skinBones = {};
    /** Packed affine matrices uploaded to the shader's mat4x3 g_Bones array. */
    mutable std::vector<glm::mat4x3> m_gpuSkinBones = {};
    mutable float m_poseTime = std::numeric_limits<float>::quiet_NaN ();
    bool m_skinningEnabled = false;

    glm::mat4 m_modelMatrix = glm::mat4 (1.0f);
    glm::mat4 m_viewProjectionMatrix = glm::mat4 (1.0f);
    glm::mat4 m_modelViewProjectionMatrix = glm::mat4 (1.0f);
    glm::mat4 m_modelViewProjectionMatrixInverse = glm::mat4 (1.0f);
    glm::mat3 m_normalMatrix = glm::mat3 (1.0f);

    std::vector<Effects::CPass*> m_passes = {};
    GLuint m_shadowProgram = GL_NONE;
    GLuint m_shadowVao = GL_NONE;
    GLint m_shadowLightViewProjection = -1;
    GLint m_shadowModel = -1;
    GLint m_shadowBones = -1;
    bool m_initialized = false;

    /** model objects carry no per-object tint/alpha; materials do that via constants */
    float m_brightness = 1.0f;
    float m_alpha = 1.0f;
    glm::vec3 m_color = glm::vec3 (1.0f);
    glm::vec4 m_color4 = glm::vec4 (1.0f);
};
} // namespace WallpaperEngine::Render::Objects
