#pragma once

#include "CRenderable.h"
#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Render/Objects/Effects/CPass.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include "WallpaperEngine/Render/Shaders/Shader.h"

#include "../TextureProvider.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

#include <glm/vec3.hpp>
#include <vector>

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Scripting;
namespace WallpaperEngine::Render::Objects::Effects {
class CMaterial;
class CPass;
} // namespace WallpaperEngine::Render::Objects::Effects

namespace WallpaperEngine::Render::Objects {
class CImage final : public CRenderable, public ScriptableObject {
    friend CObject;

public:
    CImage (Wallpapers::CScene& scene, const Image& image);
    ~CImage () override;

    void setup () override;
    void render () override;

    [[nodiscard]] const Image& getImage () const;
    [[nodiscard]] glm::vec2 getSize () const;

    [[nodiscard]] GLuint getSceneSpacePosition () const;
    [[nodiscard]] GLuint getCopySpacePosition () const;
    [[nodiscard]] GLuint getPassSpacePosition () const;
    [[nodiscard]] GLuint getTexCoordCopy () const;
    [[nodiscard]] GLuint getTexCoordPass () const;

    [[nodiscard]] const float& getBrightness () const override;
    [[nodiscard]] const float& getUserAlpha () const override;
    [[nodiscard]] const float& getAlpha () const override;
    [[nodiscard]] const glm::vec3& getColor () const override;
    [[nodiscard]] const glm::vec4& getColor4 () const override;
    [[nodiscard]] const glm::vec3& getCompositeColor () const override;

    /**
     * Performs a ping-pong on the available framebuffers to be able to continue rendering things to them
     *
     * @param drawTo The framebuffer to use
     * @param asInput The last texture used as output (if needed)
     */
    void pinpongFramebuffer (std::shared_ptr<const CFBO>* drawTo, std::shared_ptr<const TextureProvider>* asInput);

protected:
    void setupPasses ();

    void updateScreenSpacePosition ();

    struct ResolvedTransform {
	glm::vec3 origin;
	glm::vec3 scale;
	// raw scene angles in radians, accumulated through the parent chain;
	// the render path negates x/z to account for the y-flipped coordinate system
	glm::vec3 angles;
    };

    [[nodiscard]] ResolvedTransform resolveTransform (const WallpaperEngine::Data::Model::Object& object) const;

    /**
     * Computes the object's own transform (origin/scale/angle) without walking the
     * parent chain. Used as the per-node step of resolveTransform.
     */
    [[nodiscard]] static ResolvedTransform localTransform (const WallpaperEngine::Data::Model::Object& object, float time);

private:
    struct PuppetBone {
	int32_t parent = -1;
	/** inverse of the composed bind-pose transform; the rest mesh is the parts atlas,
	 *  so posed vertex = animWorld * inverseBindWorld * vertex */
	glm::mat4 inverseBindWorld = glm::mat4 (1.0f);
    };

    struct PuppetBoneFrame {
	glm::vec3 translation = glm::vec3 (0.0f);
	glm::vec3 rotation = glm::vec3 (0.0f);
	glm::vec3 scale = glm::vec3 (1.0f);
    };

    struct PuppetAnimation {
	uint32_t id = 0;
	std::string name = {};
	std::string mode = {};
	float fps = 0.0f;
	uint32_t frameCount = 0;
	/** boneFrames[bone][frame], usually frameCount + 1 entries with the last matching the first */
	std::vector<std::vector<PuppetBoneFrame>> boneFrames = {};
    };

    struct PuppetActiveLayer {
	const PuppetAnimation* animation = nullptr;
	float rate = 1.0f;
    };

    bool loadPuppetMesh (const glm::vec2& size);
    bool loadPuppetBones (const std::vector<char>& data, size_t mdlsOffset);
    void loadPuppetAnimations (const std::vector<char>& data, size_t mdlaOffset);
    void selectPuppetAnimation ();
    void updatePuppetPositionBuffer (const glm::vec2& size);
    void setupPuppetGeometryCallback (Effects::CPass* pass, bool samplesSourceTexture) const;
    ResolvedTransform updateGeometryBuffers ();
    [[nodiscard]] glm::vec2 resolveGeometrySize (float sceneWidth, float sceneHeight, glm::vec3& origin) const;
    void updateScenePosition (
	const glm::vec3& origin, const glm::vec2& size, const glm::vec3& scale, float sceneWidth, float sceneHeight
    );
    void uploadGeometryBuffers (const glm::vec2& size);
    [[nodiscard]] bool shouldRenderFinalPass (bool isLastPass) const;
    bool configurePassTarget (
	Effects::CPass* pass, std::shared_ptr<const CFBO>& drawTo,
	const std::shared_ptr<const TextureProvider>& asInput, std::shared_ptr<const TextureProvider>& effectInput,
	bool& inTargetEffectSequence
    );

    GLuint m_sceneSpacePosition;
    GLuint m_copySpacePosition;
    GLuint m_passSpacePosition;
    GLuint m_texcoordCopy;
    GLuint m_texcoordPass;
    GLuint m_puppetSpacePosition = GL_NONE;
    GLuint m_puppetTexCoord = GL_NONE;
    GLuint m_puppetTexCoordFirstPass = GL_NONE;
    GLuint m_puppetIndices = GL_NONE;
    GLsizei m_puppetIndexCount = 0;
    bool m_hasPuppetMesh = false;
    std::vector<GLfloat> m_puppetRawPositions = {};
    std::vector<uint32_t> m_puppetBlendIndices = {};
    std::vector<GLfloat> m_puppetBlendWeights = {};
    std::vector<PuppetBone> m_puppetBones = {};
    std::vector<PuppetAnimation> m_puppetAnimations = {};
    std::vector<PuppetActiveLayer> m_puppetActiveLayers = {};

    glm::mat4 m_modelViewProjectionScreen = {};
    glm::mat4 m_modelViewProjectionPass = {};
    glm::mat4 m_modelViewProjectionCopy = {};
    glm::mat4 m_modelViewProjectionScreenInverse = {};
    glm::mat4 m_modelViewProjectionPassInverse = {};
    glm::mat4 m_modelViewProjectionCopyInverse = {};

    glm::mat4 m_modelMatrix = {};
    glm::mat4 m_viewProjectionMatrix = {};

    /** rotation-only local->world matrix (and its inverse) fed to effect passes as
     * g_EffectTextureProjectionMatrix so depthparallax-style shaders can rotate the
     * parallax input into the layer's own local axes; identity when angles are zero */
    glm::mat4 m_effectTextureProjectionMatrix = glm::mat4 (1.0f);
    glm::mat4 m_effectTextureProjectionMatrixInverse = glm::mat4 (1.0f);

    std::shared_ptr<const CFBO> m_mainFBO = nullptr;
    std::shared_ptr<const CFBO> m_subFBO = nullptr;
    std::shared_ptr<const CFBO> m_currentMainFBO = nullptr;
    std::shared_ptr<const CFBO> m_currentSubFBO = nullptr;

    const Image& m_image;

    std::vector<Effects::CPass*> m_passes = {};
    std::vector<MaterialPassUniquePtr> m_virtualPassess = {};

    glm::vec4 m_pos = {};
    glm::vec3 m_sceneCenter = {};
    glm::vec2 m_size = {};

    bool m_initialized = false;

    struct {
	struct {
	    MaterialUniquePtr material;
	    ImageEffectPassOverrideUniquePtr override;
	} colorBlending;
	std::vector<MaterialUniquePtr> compatibilityMaterials = {};
	std::vector<ImageEffectPassOverrideUniquePtr> compatibilityOverrides = {};
    } m_materials;
};
} // namespace WallpaperEngine::Render::Objects
