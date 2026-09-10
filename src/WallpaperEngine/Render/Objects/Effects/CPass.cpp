#include "CPass.h"
#include <sstream>
#include <utility>

#include "WallpaperEngine/Render/Helpers/ContextAware.h"

#include "WallpaperEngine/Data/Model/Effect.h"
#include "WallpaperEngine/Data/Model/Material.h"

#include "WallpaperEngine/Render/CFBO.h"
#include "WallpaperEngine/Render/Objects/CImage.h"

#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariable.h"
#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariableFloat.h"
#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariableInteger.h"
#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariableVector2.h"
#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariableVector3.h"
#include "WallpaperEngine/Render/Shaders/Variables/ShaderVariableVector4.h"

#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Render::Objects;

using namespace WallpaperEngine::Render::Shaders::Variables;
using namespace WallpaperEngine::Render::Objects::Effects;

extern float g_Time;
extern float g_Daytime;

const TextureMap DEFAULT_BINDS = {};
const ImageEffectPassOverride DEFAULT_OVERRIDE = {};

const glm::mat4 CPass::s_defaultMatrix = glm::mat4 (1.0f);

namespace {
std::string textureSizeLabel (const std::shared_ptr<const TextureProvider>& texture) {
    if (texture == nullptr) {
	return "<null>";
    }

    return std::to_string (texture->getRealWidth ()) + "x" + std::to_string (texture->getRealHeight ());
}
}

CPass::CPass (
    CRenderable& renderable, std::shared_ptr<const FBOProvider> fboProvider, const MaterialPass& pass,
    std::optional<std::reference_wrapper<const ImageEffectPassOverride>> override,
    std::optional<std::reference_wrapper<const TextureMap>> binds,
    std::optional<std::reference_wrapper<std::string>> target, ComboMap runtimeCombos, bool deferShaderSetup
) :
    Helpers::ContextAware (renderable), m_renderable (renderable), m_fboProvider (std::move (fboProvider)),
    m_pass (pass), m_binds (binds.has_value () ? binds.value ().get () : DEFAULT_BINDS),
    m_override (override.has_value () ? override.value ().get () : DEFAULT_OVERRIDE),
    m_runtimeCombos (std::move (runtimeCombos)), m_target (target),
    m_blendingmode (pass.blending), m_depthtestmode (pass.depthtest), m_depthwritemode (pass.depthwrite) {
    if (!deferShaderSetup) this->initialize ();
    // NOTE: m_vao is created lazily in render(): VAOs are not shared between GL
    // contexts, so it cannot be created here when built on the async switch worker
}

void CPass::initialize () {
    if (m_shaderInitialized) return;
    setupShaders ();
    m_shaderInitialized = true;
}

CPass::~CPass () {
    // release the usage counts taken in setupTextureUniforms so videos that are no
    // longer referenced by any pass can stop decoding
    this->adjustTextureUsageCounts (false);

    glDeleteVertexArrays (1, &m_vao);
    this->m_vao = GL_NONE;

    // destroy shader programs
    if (!glIsProgram (this->m_programID)) {
	return; // program already invalid or deleted
    }

    GLint shaderCount = 0;
    glGetProgramiv (this->m_programID, GL_ATTACHED_SHADERS, &shaderCount);

    if (shaderCount > 0) {
	std::vector<GLuint> attachedShaders (shaderCount);
	glGetAttachedShaders (this->m_programID, shaderCount, nullptr, attachedShaders.data ());

	for (GLuint s : attachedShaders) {
	    if (glIsShader (s)) {
		glDeleteShader (s);
	    }
	}
    }

    glDeleteProgram (this->m_programID);
    this->m_programID = 0;
}

std::shared_ptr<const TextureProvider> CPass::resolveTexture (
    std::shared_ptr<const TextureProvider> expected, int index, std::shared_ptr<const TextureProvider> previous
) {
    if (expected == nullptr) {
	if (const auto it = this->m_fbos.find (index); it != this->m_fbos.end ()) {
	    expected = it->second;
	}
    }

    // first check in the binds and replace it if necessary
    const auto it = this->m_binds.find (index);

    if (it == this->m_binds.end ()) {
	return expected;
    }

    // a bind named "previous" is just another way of telling it to use whatever texture there was already
    if (it->second == "previous") {
	return this->m_previousInput ?: (previous ?: expected);
    }

    // the bind actually has a name, search the FBO in the effect and return it
    return this->resolveFBO (it->second);
}

std::shared_ptr<const CFBO> CPass::resolveFBO (const std::string& name) const {
    auto fbo = this->m_fboProvider->find (name);

    if (fbo == nullptr) {
	sLog.exception ("Tried to resolve and FBO without any luck: ", name);
    }

    return fbo;
}

void CPass::setupRenderFramebuffer (const std::shared_ptr<const CFBO>& drawTo) const {
    // set the framebuffer we're drawing to
    glBindFramebuffer (GL_FRAMEBUFFER, drawTo->getFramebuffer ());

    // set proper viewport based on what we're drawing to
    glViewport (0, 0, drawTo->getRealWidth (), drawTo->getRealHeight ());

    // Alpha-to-coverage is sticky OpenGL state, so always reset it before
    // selecting the pass' blending mode.
    glDisable (GL_SAMPLE_ALPHA_TO_COVERAGE);
    // Blend equations are sticky state. Most passes use ordinary addition, while
    // puppet mask generation selects MAX for RGB to union overlapping source parts.
    glBlendEquationSeparate (this->m_colorBlendEquation, this->m_alphaBlendEquation);

    // set texture blending
    switch (this->getBlendingMode ()) {
	case BlendingMode_Translucent:
	    glEnable (GL_BLEND);
	    glBlendFuncSeparate (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	    break;
	case BlendingMode_Additive:
	    glEnable (GL_BLEND);
	    glBlendFuncSeparate (GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
	    break;
	case BlendingMode_Normal:
	    glEnable (GL_BLEND);
	    glBlendFuncSeparate (GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
	    break;
	case BlendingMode_AlphaToCoverage:
	    glDisable (GL_BLEND);
	    glEnable (GL_SAMPLE_ALPHA_TO_COVERAGE);
	    break;
	default:
	    glDisable (GL_BLEND);
	    break;
    }

    switch (this->m_depthtestmode) {
	case DepthtestMode_Enabled:
	    glEnable (GL_DEPTH_TEST);
	    glDepthFunc (GL_LEQUAL);
	    break;
	case DepthtestMode_Disabled:
	default:
	    glDisable (GL_DEPTH_TEST);
	    break;
    }

    switch (this->m_pass.cullmode) {
	case CullingMode_Normal:
	    glEnable (GL_CULL_FACE);
	    break;

	case CullingMode_Disable:
	default:
	    glDisable (GL_CULL_FACE);
	    break;
    }

    switch (this->m_depthwritemode) {
	case DepthwriteMode_Enabled:
	    glDepthMask (true);
	    break;

	case DepthwriteMode_Disabled:
	default:
	    glDepthMask (false);
	    break;
    }
}

void CPass::setupRenderTexture () {
    // use the shader we have registered
    glUseProgram (this->m_sharedProgram ? this->m_sharedProgram->id : this->m_programID);

    auto texture0 = this->resolveTexture0 ();
    const auto animation = this->resolveTextureAnimationState (texture0);

    this->bindTextureUnit (0, texture0, animation.currentTexture);
    this->bindTextureOverrides (animation.currentTexture, texture0);

    if (texture0 != nullptr) {
	this->m_texture0Resolution = *texture0->getResolution ();
    }

    // used in animations when one of the frames is vertical instead of horizontal
    // rotation with translation = origin and end of the image to display
    if (this->g_Texture0Rotation != -1) {
	glUniform4f (
	    this->g_Texture0Rotation, animation.rotation.x, animation.rotation.y, animation.rotation.z,
	    animation.rotation.w
	);
    }
    // this actually picks the origin point of the image from the atlast
    if (this->g_Texture0Translation != -1) {
	glUniform2f (this->g_Texture0Translation, animation.translation.x, animation.translation.y);
    }
}

std::shared_ptr<const TextureProvider> CPass::resolveTexture0 () {
    auto texture0 = this->resolveTexture (this->m_input, 0, this->m_input);
    const auto it = this->m_textures.find (0);

    if (it == this->m_textures.end ()) {
	return texture0;
    }

    auto& chain = it->second;

    do {
	texture0 = chain->texture;

	if (texture0 == nullptr) {
	    if (this->m_previousInput != nullptr && this->m_previousInput->isReady ()) {
		return this->m_previousInput;
	    }

	    if (this->m_input != nullptr && this->m_input->isReady ()) {
		return this->m_input;
	    }
	} else if (texture0->isReady ()) {
	    return texture0;
	}

	chain = chain->next;
    } while (chain != nullptr);

    // got to the end of the chain, use previous input or current input if available
    if (this->m_previousInput != nullptr && this->m_previousInput->isReady ()) {
	return this->m_previousInput;
    }

    // last resort, doesn't matter if the input is ready or not
    return this->m_input;
}

CPass::TextureAnimationState
CPass::resolveTextureAnimationState (const std::shared_ptr<const TextureProvider>& texture) const {
    TextureAnimationState state;

    if (texture == nullptr || !texture->isAnimated ()) {
	return state;
    }

    double currentRenderTime = this->m_renderable.getTextureAnimationTime (texture);

    for (const auto& frameCur : texture->getFrames ()) {
	currentRenderTime -= frameCur->frametime;

	if (currentRenderTime > 0.0f) {
	    continue;
	}

	state.currentTexture = frameCur->frameNumber;
	state.translation.x = frameCur->x / texture->getTextureWidth (state.currentTexture);
	state.translation.y = frameCur->y / texture->getTextureHeight (state.currentTexture);

	state.rotation.x = frameCur->width1 / static_cast<float> (texture->getTextureWidth (state.currentTexture));
	state.rotation.y = frameCur->width2 / static_cast<float> (texture->getTextureWidth (state.currentTexture));
	state.rotation.z = frameCur->height2 / static_cast<float> (texture->getTextureHeight (state.currentTexture));
	state.rotation.w = frameCur->height1 / static_cast<float> (texture->getTextureHeight (state.currentTexture));
	break;
    }

    return state;
}

void CPass::bindTextureUnit (int index, const std::shared_ptr<const TextureProvider>& texture, uint32_t frame) const {
    if (texture == nullptr) {
	return;
    }

    glActiveTexture (GL_TEXTURE0 + index);
    glBindTexture (GL_TEXTURE_2D, texture->getTextureID (frame));
}

void CPass::bindTextureOverrides (uint32_t currentTexture, std::shared_ptr<const TextureProvider>& texture0) const {
    for (auto [index, chain] : this->m_textures) {
	// find the expected texture
	auto expectedTexture = chain->texture;

	do {
	    if (expectedTexture == nullptr) {
		if (this->m_previousInput != nullptr && this->m_previousInput->isReady ()) {
		    expectedTexture = this->m_previousInput;
		    break;
		}

		if (this->m_input != nullptr && this->m_input->isReady ()) {
		    expectedTexture = this->m_input;
		    break;
		}
	    } else if (expectedTexture->isReady ()) {
		break;
	    }

	    chain = chain->next;
	    expectedTexture = chain == nullptr ? nullptr : chain->texture;
	} while (chain != nullptr);

	if (expectedTexture == nullptr && this->m_previousInput != nullptr && this->m_previousInput->isReady ()) {
	    expectedTexture = this->m_previousInput;
	}

	if (expectedTexture == nullptr) {
	    expectedTexture = this->m_input;
	}

	this->bindTextureUnit (index, expectedTexture, index == 0 ? currentTexture : 0);

	if (index == 0) {
	    texture0 = expectedTexture;
	}
    }
}

void CPass::setupRenderReferenceUniforms () {
    // add reference uniforms
    for (const auto& value : this->m_referenceUniforms | std::views::values) {
	switch (value->type) {
	    case Double:
		glUniform1d (value->id, *static_cast<const double*> (*value->value));
		break;
	    case Float:
		glUniform1f (value->id, *static_cast<const float*> (*value->value));
		break;
	    case Integer:
		glUniform1i (value->id, *static_cast<const int*> (*value->value));
		break;
	    case Vector4:
		glUniform4fv (value->id, 1, glm::value_ptr (*static_cast<const glm::vec4*> (*value->value)));
		break;
	    case Vector3:
		glUniform3fv (value->id, 1, glm::value_ptr (*static_cast<const glm::vec3*> (*value->value)));
		break;
	    case Vector2:
		glUniform2fv (value->id, 1, glm::value_ptr (*static_cast<const glm::vec2*> (*value->value)));
		break;
	    case Matrix4:
		glUniformMatrix4fv (
		    value->id, 1, GL_FALSE, glm::value_ptr (*static_cast<const glm::mat4*> (*value->value))
		);
		break;
	    case Matrix4x3:
		glUniformMatrix4x3fv (
		    value->id, 1, GL_FALSE, glm::value_ptr (*static_cast<const glm::mat4x3*> (*value->value))
		);
		break;
	    case Matrix3:
		glUniformMatrix3fv (
		    value->id, 1, GL_FALSE, glm::value_ptr (*static_cast<const glm::mat3*> (*value->value))
		);
		break;
	}
    }
}

void CPass::setupRenderUniforms () {
    const float time = m_renderable.getScene ().getTime ();
    for (auto& [name, uniform] : m_animatedUniforms) {
	const auto& animation = *uniform.setting->animation;
	glm::vec4 value = uniform.setting->value->getVec4 ();
	for (int channel = 0; channel < 4; ++channel) {
	    value[channel] = animation.relative
		? value[channel] + animation.evaluateChannel (channel, time, 0.0f)
		: animation.evaluateChannel (channel, time, value[channel]);
	}
	uniform.sampled->update (value, DynamicValue::UpdateSource::Initialization);
    }
    // add uniforms
    for (const auto& value : this->m_uniforms | std::views::values) {
	switch (value->type) {
	    case Double:
		glUniform1dv (value->id, value->count, static_cast<const double*> (value->value));
		break;
	    case Float:
		glUniform1fv (value->id, value->count, static_cast<const float*> (value->value));
		break;
	    case Integer:
		glUniform1iv (value->id, value->count, static_cast<const int*> (value->value));
		break;
	    // TODO: VEC2/VEC3 MIGHT NEED SPECIAL TREATMENT? IDK ONLY SUPPORT 1 FOR NOW
	    case Vector4:
		glUniform4fv (value->id, value->count, glm::value_ptr (*static_cast<const glm::vec4*> (value->value)));
		break;
	    case Vector3:
		glUniform3fv (value->id, 1, glm::value_ptr (*static_cast<const glm::vec3*> (value->value)));
		break;
	    case Vector2:
		glUniform2fv (value->id, 1, glm::value_ptr (*static_cast<const glm::vec2*> (value->value)));
		break;
	    case Matrix4:
		glUniformMatrix4fv (
		    value->id, value->count, GL_FALSE, glm::value_ptr (*static_cast<const glm::mat4*> (value->value))
		);
		break;
	    case Matrix4x3:
		glUniformMatrix4x3fv (
		    value->id, value->count, GL_FALSE,
		    glm::value_ptr (*static_cast<const glm::mat4x3*> (value->value))
		);
		break;
	    case Matrix3:
		glUniformMatrix3fv (
		    value->id, 1, GL_FALSE, glm::value_ptr (*static_cast<const glm::mat3*> (value->value))
		);
		break;
	}
    }
}

void CPass::setupRenderAttributes () const {
    if (this->m_setupAttribsCallback) {
	this->m_setupAttribsCallback ();
	return;
    }

    for (const auto& cur : this->m_attribs) {
	glEnableVertexAttribArray (cur->id);
	glBindBuffer (GL_ARRAY_BUFFER, *cur->value);
	glVertexAttribPointer (cur->id, cur->elements, cur->type, GL_FALSE, 0, nullptr);

#if !NDEBUG
	glObjectLabel (
	    GL_BUFFER, *cur->value, -1,
	    ("Image " + std::to_string (this->m_renderable.getId ()) + " Pass " + this->m_pass.shader + " " + cur->name)
		.c_str ()
	);
#endif /* DEBUG */
    }
}

void CPass::renderGeometry () const {
    if (this->m_drawGeometryCallback) {
	this->m_drawGeometryCallback ();
	return;
    }

    // start actual rendering now
    glBindBuffer (GL_ARRAY_BUFFER, this->a_Position);
    glDrawArrays (GL_TRIANGLES, 0, 6);
}

void CPass::cleanupRenderSetup () {
    if (this->m_cleanupAttribsCallback) {
	this->m_cleanupAttribsCallback ();
    } else {
	// disable vertex attribs array
	for (const auto& cur : this->m_attribs) {
	    glDisableVertexAttribArray (cur->id);
	}
    }

    // unbind all the used textures
    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, 0);

    // continue on the map from the second texture
    for (const auto& index : this->m_textures | std::views::keys) {
	glActiveTexture (GL_TEXTURE0 + index);
	glBindTexture (GL_TEXTURE_2D, 0);
    }
}

void CPass::render () {
    // created lazily on the render thread: VAOs are not shared between GL contexts,
    // so an async-built wallpaper cannot create it on the worker's context
    if (this->m_vao == GL_NONE) {
	glGenVertexArrays (1, &this->m_vao);
    }

    // set the VAO for now
    glBindVertexArray (this->m_vao);

    const auto& debug = this->getContext ().getApp ().getContext ().settings.render.debug;
    if (debug.passLog) {
	sLog.out (
	    "Render pass object=", this->m_renderable.getId (), " shader=", this->m_pass.shader,
	    " target=", this->m_target.has_value () ? this->m_target.value ().get () : std::string ("<screen/local>"),
	    " drawTo=", this->m_drawTo ? this->m_drawTo->getName () : std::string ("<null>"),
	    " drawSize=", textureSizeLabel (this->m_drawTo), " inputSize=", textureSizeLabel (this->m_input)
	);
	for (const auto* uniformName :
	     { "g_TintColor", "g_CompositeColor", "g_BlendAlpha", "g_CompositeAlpha", "g_UserAlpha" }) {
	    const auto uniform = this->m_uniforms.find (uniformName);
	    if (uniform == this->m_uniforms.end ()) {
		continue;
	    }

	    switch (uniform->second->type) {
		case Vector3:
		    {
			const auto* v = static_cast<const glm::vec3*> (uniform->second->value);
			sLog.out ("  uniform ", uniformName, "=", v->x, " ", v->y, " ", v->z);
			break;
		    }
		case Float:
		    {
			const auto* v = static_cast<const float*> (uniform->second->value);
			sLog.out ("  uniform ", uniformName, "=", *v);
			break;
		    }
		default:
		    break;
	    }
	}
    }

    const auto drawTo = this->m_renderable.getScene ().resolveRenderTarget (this->m_drawTo);
    if (drawTo == nullptr) {
	sLog.error ("Skipping render pass for object ", this->m_renderable.getId (), ": no destination FBO set");
	return;
    }

    if (this->m_input == nullptr) {
	sLog.error ("Skipping render pass for object ", this->m_renderable.getId (), ": no input texture set");
	return;
    }

    if (!this->m_programSharingChecked) {
	this->setupProgramSharing ();
    }
    this->setupRenderFramebuffer (drawTo);
    this->setupRenderTexture ();
    // genericimage3/4 (and VERSION-enabled genericimage2) encode object opacity in
    // g_Color4.a and do not expose g_UserAlpha. Refresh the combined value here so
    // live property changes affect those shaders without double-applying alpha to
    // older shaders that consume g_UserAlpha directly.
    this->m_effectiveColor4 = this->m_renderable.getColor4 ();
    this->m_effectiveColor4.a *= this->m_renderable.getUserAlpha ();
    // Feedback simulations such as Wallpaper Engine's cursor-ripple effect multiply both
    // force injection and propagation by g_Frametime. Leaving the native global unbound
    // gives GLSL's zero default and freezes the simulation completely. Use CScene's
    // per-output delta so a slower monitor receives all elapsed time instead of only the
    // most recent application-loop interval.
    this->m_frameTime = this->m_renderable.getScene ().getDeltaTime ();
    this->setupRenderUniforms ();
    this->setupRenderReferenceUniforms ();
    this->setupRenderAttributes ();
    this->renderGeometry ();
    this->cleanupRenderSetup ();

    // Refresh only Wallpaper Engine's dedicated mipmapped target. Ordinary
    // _rt_imageLayerComposite_* targets intentionally remain single-level.
    drawTo->generateMipmaps ();
}

std::shared_ptr<const FBOProvider> CPass::getFBOProvider () const { return this->m_fboProvider; }

const CRenderable& CPass::getRenderable () const { return this->m_renderable; }

void CPass::setDestination (std::shared_ptr<const CFBO> drawTo) { this->m_drawTo = std::move (drawTo); }

void CPass::setInput (std::shared_ptr<const TextureProvider> input) { this->m_input = std::move (input); }

void CPass::setPreviousInput (std::shared_ptr<const TextureProvider> input) {
    this->m_previousInput = std::move (input);
}

void CPass::setModelViewProjectionMatrix (const glm::mat4* projection) {
    this->m_modelViewProjectionMatrix = projection;
}

void CPass::setModelViewProjectionMatrixInverse (const glm::mat4* projection) {
    this->m_modelViewProjectionMatrixInverse = projection;
}

void CPass::setModelMatrix (const glm::mat4* model) { this->m_modelMatrix = model; }

void CPass::setViewProjectionMatrix (const glm::mat4* viewProjection) { this->m_viewProjectionMatrix = viewProjection; }

void CPass::setEffectTextureProjectionMatrix (const glm::mat4* matrix, const glm::mat4* inverse) {
    this->m_effectTextureProjectionMatrix = matrix;
    this->m_effectTextureProjectionMatrixInverse = inverse;
}

void CPass::setBlendingMode (BlendingMode blendingmode) { this->m_blendingmode = blendingmode; }

BlendingMode CPass::getBlendingMode () const { return this->m_blendingmode; }

void CPass::setDepthtestMode (DepthtestMode depthtestmode) { this->m_depthtestmode = depthtestmode; }

DepthtestMode CPass::getDepthtestMode () const { return this->m_depthtestmode; }

void CPass::setDepthwriteMode (DepthwriteMode depthwritemode) { this->m_depthwritemode = depthwritemode; }

DepthwriteMode CPass::getDepthwriteMode () const { return this->m_depthwritemode; }

void CPass::setBlendEquation (const GLenum color, const GLenum alpha) {
    this->m_colorBlendEquation = color;
    this->m_alphaBlendEquation = alpha;
}

void CPass::setTexCoord (GLuint texcoord) { this->a_TexCoord = texcoord; }

void CPass::setPosition (GLuint position) { this->a_Position = position; }

const MaterialPass& CPass::getPass () const { return this->m_pass; }

const ImageEffectPassOverride& CPass::getOverride () const { return this->m_override; }

const TextureMap& CPass::getBinds () const { return this->m_binds; }

std::optional<std::reference_wrapper<std::string>> CPass::getTarget () const { return this->m_target; }

Render::Shaders::Shader* CPass::getShader () const { return this->m_shader.get (); }

GLuint CPass::getProgramID () const { return this->m_programID; }

void CPass::setGeometryCallback (
    GeometryCallback setupAttribs, GeometryCallback drawGeometry, GeometryCallback cleanupAttribs
) {
    this->m_setupAttribsCallback = std::move (setupAttribs);
    this->m_drawGeometryCallback = std::move (drawGeometry);
    this->m_cleanupAttribsCallback = std::move (cleanupAttribs);
}

void CPass::setupShaders () {
    // ensure the constants are defined
    const auto texture0 = this->m_renderable.getTexture ();

    // copy the combos from the pass
    this->m_combos.insert (this->m_pass.combos.begin (), this->m_pass.combos.end ());
    for (const auto& [name, value] : this->m_runtimeCombos) {
	this->m_combos.insert_or_assign (name, value);
    }

    // genericimage3/4 use this combo to perform the authored cutout/discard.
    // GL_SAMPLE_ALPHA_TO_COVERAGE then adds multisample edge smoothing when the
    // target supports it; the shader discard remains correct without MSAA.
    this->m_combos.insert_or_assign (
	"ALPHATOCOVERAGE", this->m_pass.blending == BlendingMode_AlphaToCoverage ? 1 : 0
    );

    // WE compiles every scene shader with SCENE_ORTHO describing the camera type; lit shaders
    // (genericimage3/4) use it to pick a fixed view vector on 2D scenes vs. the perspective
    // view direction on 3D scenes (combo list mined from wallpaper64.exe)
    this->m_combos.insert_or_assign (
	"SCENE_ORTHO", this->m_renderable.getScene ().getScene ().camera.projection.isPerspective ? 0 : 1
    );

    // genericimage shaders gate fog with FOG_COMPUTED, but generic4 selects its fog
    // branches directly with FOG_DIST/FOG_HEIGHT. Respect an explicit FOG=0 in both
    // paths, or additive details acquire colored rectangles from the scene fog.
    const auto& fog = this->m_renderable.getScene ().getFog ();
    const auto fogOverride = this->m_override.combos.find ("FOG");
    const auto fogMaterial = this->m_pass.combos.find ("FOG");
    const bool materialFogEnabled = fogOverride != this->m_override.combos.end () ? fogOverride->second != 0
	: fogMaterial != this->m_pass.combos.end () && fogMaterial->second != 0;
    const bool materialFogDisabled = fogOverride != this->m_override.combos.end () ? fogOverride->second == 0
	: fogMaterial != this->m_pass.combos.end () && fogMaterial->second == 0;
    this->m_combos.insert_or_assign ("FOG_DIST", fog.distanceEnabled && !materialFogDisabled ? 1 : 0);
    this->m_combos.insert_or_assign ("FOG_HEIGHT", fog.heightEnabled && !materialFogDisabled ? 1 : 0);
    this->m_combos.insert_or_assign ("FOG_COMPUTED", materialFogEnabled ? 1 : 0);

    // scenes with lights need LightingV1 modules compiled with matching uniform array sizes;
    // 2D scenes have no light objects, keeping their shader compilation untouched
    const auto& lights = this->m_renderable.getScene ().getLights ();

    if (lights.directionalCount > 0) {
	this->m_combos.insert_or_assign ("LIGHTS_DIRECTIONAL", lights.directionalCount);
    }

    if (lights.directionalShadowCount > 0) {
	this->m_combos.insert_or_assign ("LIGHTS_DIRECTIONAL_SHADOW", lights.directionalShadowCount);
	this->m_combos.insert_or_assign (
	    "LIGHTS_DIRECTIONAL_SHADOW_MASK", static_cast<int> (lights.directionalShadowMask)
	);
    }

    if (lights.pointCount > 0) {
	this->m_combos.insert_or_assign ("LIGHTS_POINT", lights.pointCount);
    }

    if (lights.pointShadowCount > 0) {
	this->m_combos.insert_or_assign ("LIGHTS_POINT_SHADOW", lights.pointShadowCount);
	this->m_combos.insert_or_assign ("LIGHTS_POINT_SHADOW_MASK", static_cast<int> (lights.pointShadowMask));
    }

    if (lights.spotCount > 0) {
	this->m_combos.insert_or_assign ("LIGHTS_SPOT", lights.spotCount);
    }

    if (lights.spotShadowCount > 0) {
	this->m_combos.insert_or_assign ("LIGHTS_SPOT_SHADOW", lights.spotShadowCount);
	this->m_combos.insert_or_assign ("LIGHTS_SPOT_SHADOW_MASK", static_cast<int> (lights.spotShadowMask));
    }

    if (lights.shadowViewCount > 0) {
	this->m_combos.insert_or_assign ("LIGHTS_SHADOW_FEATURES", lights.shadowFeatureCount);
	this->m_combos.insert_or_assign ("LIGHTS_SHADOW_MAPPING", 1);
	this->m_combos.insert_or_assign ("LIGHTS_SHADOW_MAPPING_QUALITY", 2);
    }

    if (lights.tubeCount > 0) {
	this->m_combos.insert_or_assign ("LIGHTS_TUBE", lights.tubeCount);
    }

    // Wallpaper Engine exposes the concrete format of every bound sampler as
    // TEX<n>FORMAT. This is required for packed normal maps: DecompressNormal
    // reads RG88 from .rg, while DXT normals are stored in .ay. Treating an
    // RG88 normal as RGBA forces one axis to 1 and produces extreme lighting.
    // The high texture flag nibble is the matching packed-component occupancy
    // mask consumed by sampler metadata's `components` array.
    const auto registerTextureMetadata
	= [this] (const int index, const std::shared_ptr<const TextureProvider>& texture, const bool overwrite = true) {
	      if (texture == nullptr) {
		  return;
	      }

	      const std::string prefix = "TEX" + std::to_string (index);

	      // A slot the pass authors wins over the shader's own default: that is the texture
	      // setupTextureUniforms ends up binding, so its format is the one the shader must see.
	      if (!overwrite && this->m_combos.contains (prefix + "FORMAT")) {
		  return;
	      }

	      this->m_combos.insert_or_assign (prefix + "FORMAT", static_cast<int> (texture->getFormat ()));
	      this->m_combos.insert_or_assign (
		  prefix + "COMPONENTS", static_cast<int> ((texture->getFlags () & TextureFlags_ComponentMask) >> 20)
	      );
	  };

    const auto registerAuthoredTextures
	= [this, &registerTextureMetadata] (const TextureMap& textures, const bool overwrite = true) {
	      for (const auto& [index, name] : textures) {
		  if (name.starts_with ("_rt_") || name.starts_with ("_alias_")) {
		      continue;
		  }

		  try {
		      registerTextureMetadata (index, this->m_renderable.getScene ().resolveTexture (name), overwrite);
		  } catch (const std::runtime_error&) {
		      // setupTextureUniforms reports unresolved authored textures later;
		      // metadata discovery should not turn that recoverable path into a fatal one.
		  }
	      }
	  };

    registerTextureMetadata (0, texture0);
    registerAuthoredTextures (this->m_pass.textures);
    registerAuthoredTextures (this->m_pass.usertextures);
    registerAuthoredTextures (this->m_override.textures);
    registerAuthoredTextures (this->m_override.usertextures);

    // TODO: REVIEW THE SHADER TEXTURES HERE, THE ONES PASSED ON TO THE SHADER SHOULD NOT BE IN THE LIST
    // TODO: USED TO BUILD THE TEXTURES LATER
    // use the combos copied from the pass so it includes the texture format
    const std::string& shaderName
	= this->m_override.shaderOverride.has_value () ? this->m_override.shaderOverride.value () : this->m_pass.shader;

    TextureMap passTextures = this->m_pass.textures;
    for (const auto& [index, texture] : this->m_pass.usertextures) {
	passTextures.insert_or_assign (index, texture);
    }

    TextureMap overrideTextures = this->m_override.textures;
    for (const auto& [index, texture] : this->m_override.usertextures) {
	overrideTextures.insert_or_assign (index, texture);
    }

    this->m_shader = std::make_unique<Render::Shaders::Shader> (
	this->m_renderable.getAssetLocator (), shaderName, this->m_combos, this->m_override.combos, passTextures,
	overrideTextures, this->m_override.constants
    );

    // Samplers the shader declares itself (the "formatcombo":true defaults, like generic4's
    // toon shading gradient) are only discovered while the units preprocess, which happens in
    // the constructor above. ShaderUnit keeps m_combos by reference and only emits the #define
    // block on compile (), so registering them here still reaches the generated source. Without
    // this an R8 gradient reads through the RGBA path and every lit pixel comes out red.
    registerAuthoredTextures (this->m_shader->getVertex ().getTextures (), false);
    registerAuthoredTextures (this->m_shader->getFragment ().getTextures (), false);

    const auto [vertex, fragment]
	= Shaders::GLSLContext::get ().toGlsl (this->m_shader->vertex (), this->m_shader->fragment ());

    this->m_programID = this->m_renderable.getScene ().getContext ().getShaderProgramCache ().createProgram (
	vertex, fragment, &this->m_programSharingGroup
    );
#if !NDEBUG
    glObjectLabel (GL_PROGRAM, this->m_programID, -1, shaderName.c_str ());
#endif

    // first setup the default values, these will be overwritten by future values
    this->setupShaderVariables ();
    // setup uniforms
    this->setupUniforms ();
    // setup attributes too
    this->setupAttributes ();
    // get information from the program, like uniforms, etc
    // support three textures for now
    this->g_Texture0Rotation = glGetUniformLocation (this->m_programID, "g_Texture0Rotation");
    this->g_Texture0Translation = glGetUniformLocation (this->m_programID, "g_Texture0Translation");
}

void CPass::setupAttributes () {
    this->addAttribute ("a_TexCoord", GL_FLOAT, 2, &this->a_TexCoord);
    this->addAttribute ("a_Position", GL_FLOAT, 3, &this->a_Position);
}

void CPass::setupProgramSharing () {
    this->m_programSharingChecked = true;
    if (!this->m_programSharingGroup) {
	return;
    }
    // Every entry is uploaded before every draw. Including names, locations,
    // types and array counts prevents a pass from inheriting another pass's
    // extra uniforms or array tail. Unregistered uniforms retain GLSL defaults.
    std::ostringstream layout;
    for (const auto& [name, uniform] : this->m_uniforms) {
	layout << name << ':' << uniform->id << ':' << uniform->type << ':' << uniform->count << ';';
    }
    layout << '|';
    for (const auto& [name, uniform] : this->m_referenceUniforms) {
	layout << name << ':' << uniform->id << ':' << uniform->type << ';';
    }
    layout << '|' << this->g_Texture0Rotation << ':' << this->g_Texture0Translation;
    this->m_sharedProgram
	= Shaders::ShaderProgramCache::shareProgram (this->m_programID, this->m_programSharingGroup, layout.str ());
}

void CPass::leaveProgramSharing () {
    if (!this->m_sharedProgram) {
	return;
    }
    // Registration normally finishes before the first render. If a caller
    // changes it later, keep this pass private from now on. Materialize its own
    // values before changing the old layout, preserving partially written arrays
    // without copying whichever other material last used the shared program.
    GLint previous = 0;
    glGetIntegerv (GL_CURRENT_PROGRAM, &previous);
    glUseProgram (this->m_programID);
    this->setupRenderUniforms ();
    this->setupRenderReferenceUniforms ();
    glUseProgram (previous);
    this->m_sharedProgram.reset ();
    this->m_programSharingGroup.reset ();
}

void CPass::setupTextureUniforms () {
    // first set default textures extracted from the shader
    // vertex shader doesn't seem to have texture info
    // but for now just set first vertex's textures
    // and then try with fragment's and override any existing
    for (const auto& [index, textureName] : this->m_shader->getVertex ().getTextures ()) {
	try {
	    auto texture = textureName.find ("_rt_") == 0 || textureName.find ("_alias_") == 0
		? this->resolveFBO (textureName)
		: this->m_renderable.getScene ().resolveTexture (textureName);

	    // create chain entry
	    this->m_textures[index] = std::make_shared<TextureChainEntry> (TextureChainEntry {
		.texture = texture,
		.next = nullptr,
	    });
	} catch (std::runtime_error& ex) {
	    sLog.error ("Cannot resolve texture ", textureName, " for fragment shader ", ex.what ());
	}
    }

    for (const auto& [index, textureName] : this->m_shader->getFragment ().getTextures ()) {
	try {
	    auto texture = textureName.find ("_rt_") == 0 || textureName.find ("_alias_") == 0
		? this->resolveFBO (textureName)
		: this->m_renderable.getScene ().resolveTexture (textureName);

	    const auto it = this->m_textures.find (index);
	    const auto chain = std::make_shared<TextureChainEntry> (TextureChainEntry {
		.texture = texture,
		.next = it != this->m_textures.end () ? it->second : nullptr,
	    });

	    this->m_textures[index] = chain;
	} catch (std::runtime_error& ex) {
	    sLog.error ("Cannot resolve texture ", textureName, " for fragment shader ", ex.what ());
	}
    }

    for (const auto& [index, textureName] : this->m_pass.textures) {
	try {
	    auto texture = textureName.find ("_rt_") == 0 || textureName.find ("_alias_") == 0
		? this->resolveFBO (textureName)
		: this->m_renderable.getScene ().resolveTexture (textureName);

	    const auto it = this->m_textures.find (index);
	    const auto chain = std::make_shared<TextureChainEntry> (TextureChainEntry {
		.texture = texture,
		.next = it != this->m_textures.end () ? it->second : nullptr,
	    });

	    this->m_textures[index] = chain;
	} catch (std::runtime_error& ex) {
	    sLog.error ("Cannot resolve texture ", textureName, " for pass ", ex.what ());
	}
    }

    for (const auto& [index, textureName] : this->m_pass.usertextures) {
	try {
	    auto texture = textureName.find ("_rt_") == 0 || textureName.find ("_alias_") == 0
		? this->resolveFBO (textureName)
		: this->m_renderable.getScene ().resolveTexture (textureName);

	    const auto it = this->m_textures.find (index);
	    const auto chain = std::make_shared<TextureChainEntry> (TextureChainEntry {
		.texture = texture,
		.next = it != this->m_textures.end () ? it->second : nullptr,
	    });

	    this->m_textures[index] = chain;
	} catch (std::runtime_error& ex) {
	    sLog.error ("Cannot resolve user texture ", textureName, " for pass ", ex.what ());
	}
    }

    // override any texture
    for (const auto& [index, textureName] : this->m_override.textures) {
	try {
	    auto texture = textureName.find ("_rt_") == 0 || textureName.find ("_alias_") == 0
		? this->resolveFBO (textureName)
		: this->m_renderable.getScene ().resolveTexture (textureName);

	    const auto it = this->m_textures.find (index);
	    const auto chain = std::make_shared<TextureChainEntry> (TextureChainEntry {
		.texture = texture,
		.next = it != this->m_textures.end () ? it->second : nullptr,
	    });

	    this->m_textures[index] = chain;
	} catch (std::runtime_error& ex) {
	    sLog.error ("Cannot resolve texture ", textureName, " for override ", ex.what ());
	}
    }

    for (const auto& [index, textureName] : this->m_override.usertextures) {
	try {
	    auto texture = textureName.find ("_rt_") == 0 || textureName.find ("_alias_") == 0
		? this->resolveFBO (textureName)
		: this->m_renderable.getScene ().resolveTexture (textureName);

	    const auto it = this->m_textures.find (index);
	    const auto chain = std::make_shared<TextureChainEntry> (TextureChainEntry {
		.texture = texture,
		.next = it != this->m_textures.end () ? it->second : nullptr,
	    });

	    this->m_textures[index] = chain;
	} catch (std::runtime_error& ex) {
	    sLog.error ("Cannot resolve user texture ", textureName, " for override ", ex.what ());
	}
    }

    // binds are set last as they're the most important to be set
    for (const auto& [index, bind] : this->m_binds) {
	const auto texture = bind == "previous" ? nullptr : this->resolveFBO (bind);
	const auto it = this->m_textures.find (index);
	const auto chain = std::make_shared<TextureChainEntry> (TextureChainEntry {
	    .texture = texture,
	    .next = it != this->m_textures.end () ? it->second : nullptr,
	});

	this->m_textures[index] = chain;
    }

    // resolve the main texture
    std::shared_ptr<const TextureProvider> texture = this->resolveTexture (this->m_renderable.getTexture (), 0);
    // register all the texture uniforms with correct values
    this->addUniform ("g_Texture0", 0);
    this->addUniform ("g_Texture1", 1);
    this->addUniform ("g_Texture2", 2);
    this->addUniform ("g_Texture3", 3);
    this->addUniform ("g_Texture4", 4);
    this->addUniform ("g_Texture5", 5);
    this->addUniform ("g_Texture6", 6);
    this->addUniform ("g_Texture7", 7);
    this->addUniform ("g_Texture8", 8);
    this->addUniform ("g_TextureReductionScale", 1.0f);

    // Wallpaper Engine initializes resolution uniforms even when the matching texture
    // slot is not bound. Workshop shaders sometimes use one of those spare uniforms
    // solely to build UVs for a mask stored in another slot (for example, an opacity
    // mask in g_Texture3 with UVs derived from g_Texture7Resolution). Leaving it at
    // OpenGL's zero default turns the ratio below into 0/0 and makes the mask sample
    // undefined. Identity dimensions preserve the authored UVs; resolved textures
    // overwrite their own slots below.
    for (int index = 0; index < 9; index++) {
	this->addUniform ("g_Texture" + std::to_string (index) + "Resolution", glm::vec4 (1.0f));
    }

    this->m_texture0Resolution = *texture->getResolution ();
    this->addUniform ("g_Texture0Resolution", &this->m_texture0Resolution);

    const auto registerTexelUniform = [this] (const int index, const TextureProvider& provider) {
	const float width = static_cast<float> (glm::max (provider.getTextureWidth (0), 1u));
	const float height = static_cast<float> (glm::max (provider.getTextureHeight (0), 1u));
	this->m_textureTexels.insert_or_assign (index, glm::vec4 (1.0f / width, 1.0f / height, width, height));
	this->addUniform ("g_Texture" + std::to_string (index) + "Texel", &this->m_textureTexels.at (index));
    };
    registerTexelUniform (0, *texture);

    for (const auto& [textureIndex, expectedTexture] : this->m_textures) {
	std::ostringstream namestream;

	namestream << "g_Texture" << textureIndex << "Resolution";

	texture = this->resolveTexture (expectedTexture->texture, textureIndex, texture);
	this->addUniform (namestream.str (), texture->getResolution ());
	registerTexelUniform (textureIndex, *texture);
    }

    this->addUniform ("g_Texture0Resolution", &this->m_texture0Resolution);

    // pin every referenced texture so video-backed effect inputs actually decode
    this->adjustTextureUsageCounts (true);
}

void CPass::adjustTextureUsageCounts (const bool increment) const {
    // idempotent: holds exactly one pin no matter how many times setup runs
    if (increment == this->m_texturesUsageCounted) {
	return;
    }

    this->m_texturesUsageCounted = increment;

    for (const auto& [index, chain] : this->m_textures) {
	for (auto cur = chain.get (); cur != nullptr; cur = cur->next.get ()) {
	    if (cur->texture == nullptr) {
		continue;
	    }

	    if (increment) {
		cur->texture->incrementUsageCount ();
	    } else {
		cur->texture->decrementUsageCount ();
	    }
	}
    }
}

void CPass::setupUniforms () {
    this->setupTextureUniforms ();

    const auto& renderable = this->m_renderable;
    const auto& scene = this->m_renderable.getScene ();
    const auto& sceneData = this->m_renderable.getScene ().getScene ();
    const auto& recorder = this->m_renderable.getScene ().getAudioContext ().getRecorder ();

    // lighting variables
    this->addUniform ("g_LightAmbientColor", sceneData.colors.ambient->value->getVec3 ());
    this->addUniform ("g_LightSkylightColor", sceneData.colors.skylight->value->getVec3 ());
    this->addUniform ("g_EyePosition", &scene.getCamera ().getEye ());

    const auto& fog = scene.getFog ();
    this->addUniform ("g_FogDistanceColor", &sceneData.fog.distance.color->value->getVec3 ());
    this->addUniform ("g_FogDistanceParams", &fog.distanceParams);
    this->addUniform ("g_FogHeightColor", &sceneData.fog.height.color->value->getVec3 ());
    this->addUniform ("g_FogHeightParams", &fog.heightParams);

    // dynamic light state, refreshed by CScene::updateLightState every frame
    const auto& lights = scene.getLights ();

    if (lights.directionalCount > 0) {
	this->addUniform ("g_LDirectional_Direction", lights.directionalDirections.data (), lights.directionalCount);
	this->addUniform ("g_LDirectional_Color", lights.directionalColors.data (), lights.directionalCount);
	if (lights.directionalShadowCount > 0) {
	    this->addUniform (
		"g_LDirectional_ShadowEnabled", lights.directionalShadowEnabled.data (), lights.directionalCount
	    );
	}
    }

    if (lights.pointCount > 0) {
	this->addUniform ("g_LPoint_Origin", lights.pointOrigins.data (), lights.pointCount);
	this->addUniform ("g_LPoint_Color", lights.pointColors.data (), lights.pointCount);
	if (lights.pointShadowCount > 0) {
	    this->addUniform (
		"g_LFeature_ShadowPointProjection", lights.pointShadowProjections.data (), lights.pointCount
	    );
	    this->addUniform (
		"g_LFeature_ShadowPointProjectionTransform", lights.pointShadowTransforms.data (),
		lights.pointCount
	    );
	    this->addUniform ("g_LPoint_ShadowEnabled", lights.pointShadowEnabled.data (), lights.pointCount);
	}
    }

    if (lights.spotCount > 0) {
	this->addUniform ("g_LSpot_Origin", lights.spotOrigins.data (), lights.spotCount);
	this->addUniform ("g_LSpot_Direction", lights.spotDirections.data (), lights.spotCount);
	this->addUniform ("g_LSpot_Color", lights.spotColors.data (), lights.spotCount);
	this->addUniform ("g_LSpot_Exponent", lights.spotExponents.data (), lights.spotCount);
	if (lights.spotShadowCount > 0) {
	    this->addUniform ("g_LSpot_ShadowEnabled", lights.spotShadowEnabled.data (), lights.spotCount);
	}
    }

    if (lights.shadowFeatureCount > 0) {
	this->addUniform (
	    "g_LFeature_ShadowProjection", lights.shadowMatrices.data (), lights.shadowFeatureCount
	);
	this->addUniform (
	    "g_LFeature_ShadowProjectionTransform", lights.shadowTransforms.data (), lights.shadowFeatureCount
	);
    }

    if (lights.tubeCount > 0) {
	this->addUniform ("g_LTube_OriginA", lights.tubeOriginsA.data (), lights.tubeCount);
	this->addUniform ("g_LTube_OriginB", lights.tubeOriginsB.data (), lights.tubeCount);
	this->addUniform ("g_LTube_Color", lights.tubeColors.data (), lights.tubeCount);
    }
    // register variables like brightness and alpha by pointer so the values are re-read every
    // frame: they can be driven by user-property bindings (e.g. alpha bound to a "brightness"
    // slider) that are applied after pass construction, so a by-value copy here would freeze the
    // pre-binding defaults (alpha = 1.0) and render e.g. darkening overlays fully opaque
    // Effect/pass constants are the highest-priority source. Custom material shaders can map an
    // authored constant onto a generic object uniform, so do not replace those values with object
    // defaults. Passing Breeze (Workshop 2244339517), for example, maps "Bright" onto g_Brightness:
    // replacing its authored 5.5/10 values with the model default of 1 turns its pow(..., 4) palm
    // materials nearly black and keeps the pink triangle below the bloom threshold.
    if (!this->m_constantUniforms.contains ("g_Brightness")) {
	this->addUniform ("g_Brightness", &renderable.getBrightness ());
    }
    // Opacity effects similarly map their authored "alpha" constant onto g_UserAlpha; replacing
    // that pointer with the object's static alpha makes scripted music overlays stay permanently
    // opaque (Legendaries of Hoenn, Workshop 3101147701).
    if (!this->m_constantUniforms.contains ("g_UserAlpha")) {
	this->addUniform ("g_UserAlpha", &renderable.getUserAlpha ());
    }
    this->addUniform ("g_Alpha", &renderable.getAlpha ());
    this->addUniform ("g_Color", &renderable.getColor ());
    this->m_effectiveColor4 = renderable.getColor4 ();
    this->m_effectiveColor4.a *= renderable.getUserAlpha ();
    this->addUniform ("g_Color4", &this->m_effectiveColor4);
    if (!this->m_uniforms.contains ("g_CompositeColor")) {
	this->addUniform ("g_CompositeColor", &renderable.getCompositeColor ());
    }
    // add some external variables
    this->addUniform ("g_Time", &g_Time);
    this->addUniform ("g_Frametime", &this->m_frameTime);
    this->addUniform ("g_Daytime", &g_Daytime);
    // add model-view-projection matrix
    this->addUniform ("g_ModelViewProjectionMatrixInverse", &this->m_modelViewProjectionMatrixInverse);
    this->addUniform ("g_ModelViewProjectionMatrix", &this->m_modelViewProjectionMatrix);
    this->addUniform ("g_EffectModelViewProjectionMatrix", &this->m_modelViewProjectionMatrix);
    this->addUniform ("g_ModelMatrix", &this->m_modelMatrix);
    this->addUniform ("g_EffectModelMatrix", &this->m_modelMatrix);
    this->addUniform ("g_NormalModelMatrix", glm::identity<glm::mat3> ());
    this->addUniform ("g_ViewProjectionMatrix", &this->m_viewProjectionMatrix);
    this->addUniform ("g_PointerPosition", scene.getMousePosition ());
    this->addUniform ("g_PointerPositionLast", scene.getMousePositionLast ());
    this->addUniform ("g_ParallaxPosition", scene.getParallaxPosition ());
    this->addUniform ("g_EffectTextureProjectionMatrix", &this->m_effectTextureProjectionMatrix);
    this->addUniform ("g_EffectTextureProjectionMatrixInverse", &this->m_effectTextureProjectionMatrixInverse);
    this->addUniform ("g_TexelSize", glm::vec2 (1.0 / scene.getWidth (), 1.0 / scene.getHeight ()));
    this->addUniform ("g_TexelSizeHalf", glm::vec2 (0.5 / scene.getWidth (), 0.5 / scene.getHeight ()));
    this->addUniform ("g_AudioSpectrum16Left", recorder.audio16Left, 16);
    this->addUniform ("g_AudioSpectrum16Right", recorder.audio16Right, 16);
    this->addUniform ("g_AudioSpectrum32Left", recorder.audio32Left, 32);
    this->addUniform ("g_AudioSpectrum32Right", recorder.audio32Right, 32);
    this->addUniform ("g_AudioSpectrum64Left", recorder.audio64Left, 64);
    this->addUniform ("g_AudioSpectrum64Right", recorder.audio64Right, 64);
}

void CPass::addAttribute (const std::string& name, GLint type, GLint elements, const GLuint* value) {
    const GLint id = glGetAttribLocation (this->m_programID, name.c_str ());

    if (id == -1) {
	return;
    }

    this->m_attribs.emplace_back (std::make_unique<AttribEntry> (id, name, type, elements, value));
}

template <typename T> void CPass::addUniform (const std::string& name, UniformType type, T value) {
    GLint id = glGetUniformLocation (this->m_programID, name.c_str ());

    // parameter not found, can be ignored
    if (id == -1) {
	return;
    }
    this->leaveProgramSharing ();

    // build a copy of the value and allocate it somewhere
    auto newValue = std::make_shared<T> (value);

    // uniform found, add it to the list
    this->m_referenceUniforms.erase (name);
    this->m_uniforms.insert_or_assign (
	name, std::make_unique<UniformEntry> (id, name, type, std::move (newValue), 1)
    );
}

template <typename T> void CPass::addUniform (const std::string& name, UniformType type, T* value, int count) {
    // this version is used to reference to system variables so things like g_Time works fine
    GLint id = glGetUniformLocation (this->m_programID, name.c_str ());

    // parameter not found, can be ignored
    if (id == -1) {
	return;
    }
    this->leaveProgramSharing ();

    // uniform found, add it to the list
    this->m_referenceUniforms.erase (name);
    this->m_uniforms.insert_or_assign (name, std::make_unique<UniformEntry> (id, name, type, value, count));
}

template <typename T> void CPass::addUniform (const std::string& name, UniformType type, T** value) {
    // this version is used to reference to system variables so things like g_Time works fine
    const GLint id = glGetUniformLocation (this->m_programID, name.c_str ());

    // parameter not found, can be ignored
    if (id == -1) {
	return;
    }
    this->leaveProgramSharing ();

    // uniform found, add it to the list
    this->m_uniforms.erase (name);
    this->m_referenceUniforms.insert_or_assign (
	name, std::make_unique<ReferenceUniformEntry> (
		  id, name, type, reinterpret_cast<const void**> (value)
	      )
    );
}

void CPass::setupShaderVariables () {
    for (const auto& cur : this->m_shader->getVertex ().getParameters ()) {
	if (!this->m_uniforms.contains (cur->getName ())) {
	    this->addUniform (cur);
	}
    }

    for (const auto& cur : this->m_shader->getFragment ().getParameters ()) {
	if (!this->m_uniforms.contains (cur->getName ())) {
	    this->addUniform (cur);
	}
    }

    // apply material pass constants (e.g. constantshadervalues from the material JSON)
    for (const auto& [name, value] : this->m_pass.constants) {
	const auto [vertex, fragment] = this->m_shader->findParameter (name);

	if (vertex == nullptr && fragment == nullptr) {
	    continue;
	}

	ShaderVariable* var = vertex == nullptr ? fragment : vertex;
	this->addUniform (var, *value);
	this->m_constantUniforms.emplace (var->getName ());
    }

    // apply override constants (highest priority, overrides both defaults and pass constants)
    for (const auto& [name, value] : this->m_override.constants) {
	const auto [vertex, fragment] = this->m_shader->findParameter (name);

	if (vertex == nullptr && fragment == nullptr) {
	    continue;
	}

	ShaderVariable* var = vertex == nullptr ? fragment : vertex;
	this->addUniform (var, *value);
	this->m_constantUniforms.emplace (var->getName ());
    }
}

// define some basic methods for the template
void CPass::addUniform (const ShaderVariable* value, const UserSetting& setting) {
    m_animatedUniforms.erase (value->getName ());
    if (setting.animation == nullptr) {
	addUniform (value, setting.value.get ());
	return;
    }
    auto [entry, inserted] = m_animatedUniforms.emplace (value->getName (), AnimatedUniform {
	.setting = &setting, .sampled = std::make_unique<DynamicValue> (*setting.value)
    });
    addUniform (value, entry->second.sampled.get ());
}

void CPass::addUniform (ShaderVariable* value) {
    // no need to re-implement this, call the version that takes a CDynamicValue as second parameter
    // and that handles casting and everything
    this->addUniform (value, value);
}

void CPass::addUniform (const ShaderVariable* value, const DynamicValue* setting) {
    if (value->is<ShaderVariableFloat> ()) {
	this->addUniform (value->getName (), &setting->getFloat ());
    } else if (value->is<ShaderVariableInteger> ()) {
	this->addUniform (value->getName (), &setting->getInt ());
    } else if (value->is<ShaderVariableVector2> ()) {
	this->addUniform (value->getName (), &setting->getVec2 ());
    } else if (value->is<ShaderVariableVector3> ()) {
	this->addUniform (value->getName (), &setting->getVec3 ());
    } else if (value->is<ShaderVariableVector4> ()) {
	this->addUniform (value->getName (), &setting->getVec4 ());
    } else {
	sLog.error ("Cannot convert setting dynamic value  to ", value->getName (), ". Using default value");
    }
}

void CPass::addUniform (const std::string& name, int value) { this->addUniform (name, UniformType::Integer, value); }

void CPass::addUniform (const std::string& name, const int* value, int count) {
    this->addUniform (name, UniformType::Integer, value, count);
}

void CPass::addUniform (const std::string& name, const int** value) {
    this->addUniform (name, UniformType::Integer, value);
}

void CPass::addUniform (const std::string& name, double value) { this->addUniform (name, UniformType::Double, value); }

void CPass::addUniform (const std::string& name, const double* value, int count) {
    this->addUniform (name, UniformType::Double, value, count);
}

void CPass::addUniform (const std::string& name, const double** value) {
    this->addUniform (name, UniformType::Double, value);
}

void CPass::addUniform (const std::string& name, float value) { this->addUniform (name, UniformType::Float, value); }

void CPass::addUniform (const std::string& name, const float* value, int count) {
    this->addUniform (name, UniformType::Float, value, count);
}

void CPass::addUniform (const std::string& name, const float** value) {
    this->addUniform (name, UniformType::Float, value);
}

void CPass::addUniform (const std::string& name, glm::vec2 value) {
    this->addUniform (name, UniformType::Vector2, value);
}

void CPass::addUniform (const std::string& name, const glm::vec2* value) {
    this->addUniform (name, UniformType::Vector2, value, 1);
}

void CPass::addUniform (const std::string& name, const glm::vec2** value) {
    this->addUniform (name, UniformType::Vector2, value, 1);
}

void CPass::addUniform (const std::string& name, glm::vec3 value) {
    this->addUniform (name, UniformType::Vector3, value);
}

void CPass::addUniform (const std::string& name, const glm::vec3* value) {
    this->addUniform (name, UniformType::Vector3, value, 1);
}

void CPass::addUniform (const std::string& name, const glm::vec3** value) {
    this->addUniform (name, UniformType::Vector3, value);
}

void CPass::addUniform (const std::string& name, const glm::vec4 value) {
    this->addUniform (name, UniformType::Vector4, value);
}

void CPass::addUniform (const std::string& name, const glm::vec4* value, int count) {
    this->addUniform (name, UniformType::Vector4, value, count);
}

void CPass::addUniform (const std::string& name, const glm::vec4** value) {
    this->addUniform (name, UniformType::Vector4, value);
}

void CPass::addUniform (const std::string& name, const glm::mat3& value) {
    this->addUniform (name, UniformType::Matrix3, value);
}

void CPass::addUniform (const std::string& name, const glm::mat3* value) {
    this->addUniform (name, UniformType::Matrix3, value, 1);
}

void CPass::addUniform (const std::string& name, const glm::mat3** value) {
    this->addUniform (name, UniformType::Matrix3, value);
}

void CPass::addUniform (const std::string& name, const glm::mat4 value) {
    this->addUniform (name, UniformType::Matrix4, value);
}

void CPass::addUniform (const std::string& name, const glm::mat4* value, int count) {
    this->addUniform (name, UniformType::Matrix4, value, count);
}

void CPass::addUniform (const std::string& name, const glm::mat4x3* value, int count) {
    this->addUniform (name, UniformType::Matrix4x3, value, count);
}

void CPass::addUniform (const std::string& name, const glm::mat4** value) {
    this->addUniform (name, UniformType::Matrix4, value);
}
