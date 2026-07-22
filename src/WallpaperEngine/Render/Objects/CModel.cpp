#include "CModel.h"

#include <algorithm>
#include <sstream>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "WallpaperEngine/Data/Model/MdlAnimation.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render::Objects;

namespace {
GLuint compileShadowStage (const std::string& source, const GLenum type) {
    const GLuint shader = glCreateShader (type);
    const char* sourcePointer = source.c_str ();
    glShaderSource (shader, 1, &sourcePointer, nullptr);
    glCompileShader (shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv (shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
	GLint length = 0;
	glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &length);
	std::string log (std::max (length, 1), '\0');
	glGetShaderInfoLog (shader, length, nullptr, log.data ());
	glDeleteShader (shader);
	sLog.exception ("Cannot compile model shadow shader: ", log);
    }

    return shader;
}
} // namespace

CModel::CModel (Wallpapers::CScene& scene, const Model3D& model) :
    CObject (scene, model), CRenderable (scene, model, **model.materials.begin ()), ScriptableObject (scene, model),
    m_model (model),
    m_size (
	model.mesh.hasBoundingBox ? model.mesh.boundingBoxMax - model.mesh.boundingBoxMin : glm::vec3 (0.0f)
    ) {
    // Some older Workshop scenes bind the editor's 2D pivot script to a 3D model.
    // Expose its authored bounds as a vector-like size so those published scripts can
    // keep calculating their pivot even though current IModelLayer docs omit the field.
    this->registerProperty ("size", this->m_size);
}

CModel::~CModel () {
    for (const auto& pass : this->m_passes) {
	delete pass;
    }

    for (const auto& submesh : this->m_submeshes) {
	glDeleteBuffers (1, &submesh.vertexBuffer);
	glDeleteBuffers (1, &submesh.indexBuffer);
    }

    if (this->m_shadowVao != GL_NONE) {
	glDeleteVertexArrays (1, &this->m_shadowVao);
    }
    if (this->m_shadowProgram != GL_NONE) {
	glDeleteProgram (this->m_shadowProgram);
    }
}

void CModel::setup () {
    if (this->m_initialized) {
	return;
    }

    this->detectTexture ();

    if (this->m_texture == nullptr) {
	// keep the pass pipeline happy; the shader's albedo defaults handle the rest
	this->m_texture = std::make_shared<CFBO> ("", TextureFormat_ARGB8888, TextureFlags_NoFlags, 1, 1, 1, 1, 1);
    }

    CRenderable::setup ();

    this->m_skinningEnabled = this->m_model.mesh.skinned && !this->m_model.animationData.bones.empty ();

    if (!this->m_model.animationData.bones.empty ()) {
	this->updateAnimationPose ();
	sLog.out (
	    "Loaded 3D model animation data ", this->m_model.filename,
	    " bones=", this->m_model.animationData.bones.size (),
	    " attachments=", this->m_model.animationData.attachments.size (),
	    " clips=", this->m_model.animationData.animations.size (),
	    " skinning=", this->m_skinningEnabled ? "gpu" : "none"
	);
    }

    for (size_t submeshIndex = 0; submeshIndex < this->m_model.mesh.submeshes.size (); submeshIndex++) {
	const auto& submesh = this->m_model.mesh.submeshes[submeshIndex];
	SubmeshBuffers buffers {};

	glGenBuffers (1, &buffers.vertexBuffer);
	glBindBuffer (GL_ARRAY_BUFFER, buffers.vertexBuffer);
	glBufferData (
	    GL_ARRAY_BUFFER, submesh.vertices.size () * sizeof (float), submesh.vertices.data (), GL_STATIC_DRAW
	);

	glGenBuffers (1, &buffers.indexBuffer);
	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, buffers.indexBuffer);
	glBufferData (
	    GL_ELEMENT_ARRAY_BUFFER, submesh.indices.size () * sizeof (uint32_t), submesh.indices.data (),
	    GL_STATIC_DRAW
	);

	buffers.indexCount = static_cast<GLsizei> (submesh.indices.size ());
	this->m_submeshes.push_back (buffers);

	for (const auto& cur : this->m_model.materials[submeshIndex]->passes) {
	    ComboMap runtimeCombos;
	    // Thin translucent model surfaces must receive light from either side of
	    // their authored normal. Otherwise a back-lit sheet collapses to black
	    // RGB while its alpha still occludes what is behind it (most visibly the
	    // Saturn ring in workshop 3589454154). generic4 already contains the
	    // stock DOUBLESIDEDLIGHTING branch; select it for translucent model
	    // passes unless the material explicitly authored a value.
	    if (cur->shader == "generic4" && cur->blending == BlendingMode_Translucent
		&& !cur->combos.contains ("DOUBLESIDEDLIGHTING")) {
		runtimeCombos.emplace ("DOUBLESIDEDLIGHTING", 1);
	    }
	    if (this->m_skinningEnabled) {
		runtimeCombos.insert_or_assign ("SKINNING", 1);
		runtimeCombos.insert_or_assign ("BONECOUNT", static_cast<int> (this->m_gpuSkinBones.size ()));
	    }

	    auto* pass = new Effects::CPass (
		*this, std::make_shared<FBOProvider> (this), *cur, std::nullopt, std::nullopt, std::nullopt,
		std::move (runtimeCombos)
	    );

	    pass->setDestination (this->getScene ().getFBO ());
	    pass->setInput (this->m_texture);
	    pass->setModelViewProjectionMatrix (&this->m_modelViewProjectionMatrix);
	    pass->setModelViewProjectionMatrixInverse (&this->m_modelViewProjectionMatrixInverse);
	    pass->setModelMatrix (&this->m_modelMatrix);
	    pass->setViewProjectionMatrix (&this->m_viewProjectionMatrix);
	    pass->addUniform ("g_NormalModelMatrix", &this->m_normalMatrix);
	    if (this->m_skinningEnabled) {
		pass->addUniform (
		    "g_Bones", this->m_gpuSkinBones.data (), static_cast<int> (this->m_gpuSkinBones.size ())
		);
	    }
	    this->setupGeometryCallback (pass, submeshIndex);

	    this->m_passes.push_back (pass);
	}
    }

    if (this->getScene ().getLights ().shadowViewCount > 0) {
	this->setupShadowProgram ();
    }

    this->m_initialized = true;
}

void CModel::setupShadowProgram () {
    std::ostringstream vertex;
    vertex << "#version 330 core\n"
	      "layout(location = 0) in vec3 a_Position;\n";
    if (this->m_skinningEnabled) {
	vertex << "layout(location = 1) in uvec4 a_BlendIndices;\n"
		  "layout(location = 2) in vec4 a_BlendWeights;\n"
		  "uniform mat4x3 u_Bones[" << this->m_gpuSkinBones.size () << "];\n";
    }
    vertex << "uniform mat4 u_LightViewProjection;\n"
	      "uniform mat4 u_Model;\n"
	      "void main() {\n"
	      "    vec3 localPosition = a_Position;\n";
    if (this->m_skinningEnabled) {
	vertex << "    mat4x3 skin = u_Bones[a_BlendIndices.x] * a_BlendWeights.x\n"
		  "        + u_Bones[a_BlendIndices.y] * a_BlendWeights.y\n"
		  "        + u_Bones[a_BlendIndices.z] * a_BlendWeights.z\n"
		  "        + u_Bones[a_BlendIndices.w] * a_BlendWeights.w;\n"
		  "    localPosition = skin * vec4(localPosition, 1.0);\n";
    }
    vertex << "    gl_Position = u_LightViewProjection * u_Model * vec4(localPosition, 1.0);\n"
	      "}\n";

    static const std::string fragment = "#version 330 core\nvoid main() {}\n";
    const GLuint vertexShader = compileShadowStage (vertex.str (), GL_VERTEX_SHADER);
    const GLuint fragmentShader = compileShadowStage (fragment, GL_FRAGMENT_SHADER);
    this->m_shadowProgram = glCreateProgram ();
    glAttachShader (this->m_shadowProgram, vertexShader);
    glAttachShader (this->m_shadowProgram, fragmentShader);
    glLinkProgram (this->m_shadowProgram);
    glDeleteShader (vertexShader);
    glDeleteShader (fragmentShader);

    GLint linked = GL_FALSE;
    glGetProgramiv (this->m_shadowProgram, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
	GLint length = 0;
	glGetProgramiv (this->m_shadowProgram, GL_INFO_LOG_LENGTH, &length);
	std::string log (std::max (length, 1), '\0');
	glGetProgramInfoLog (this->m_shadowProgram, length, nullptr, log.data ());
	sLog.exception ("Cannot link model shadow shader: ", log);
    }

    this->m_shadowLightViewProjection = glGetUniformLocation (this->m_shadowProgram, "u_LightViewProjection");
    this->m_shadowModel = glGetUniformLocation (this->m_shadowProgram, "u_Model");
    this->m_shadowBones = glGetUniformLocation (this->m_shadowProgram, "u_Bones");
}

void CModel::setupGeometryCallback (Effects::CPass* pass, size_t submeshIndex) {
    const auto stride = static_cast<GLsizei> (this->m_model.mesh.strideBytes);

    pass->setGeometryCallback (
	[this, pass, stride, submeshIndex] () {
	    const auto& mesh = this->m_model.mesh;
	    const GLint position = glGetAttribLocation (pass->getProgramID (), "a_Position");
	    const GLint normal = glGetAttribLocation (pass->getProgramID (), "a_Normal");
	    const GLint tangent = glGetAttribLocation (pass->getProgramID (), "a_Tangent4");
	    const GLint texCoord = glGetAttribLocation (pass->getProgramID (), "a_TexCoord");
	    const GLint blendIndices = glGetAttribLocation (pass->getProgramID (), "a_BlendIndices");
	    const GLint blendWeights = glGetAttribLocation (pass->getProgramID (), "a_BlendWeights");

	    glBindBuffer (GL_ARRAY_BUFFER, this->m_submeshes[submeshIndex].vertexBuffer);

	    if (position >= 0) {
		glEnableVertexAttribArray (position);
		glVertexAttribPointer (
		    position, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*> (mesh.positionOffset)
		);
	    }

	    if (normal >= 0) {
		glEnableVertexAttribArray (normal);
		glVertexAttribPointer (
		    normal, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*> (mesh.normalOffset)
		);
	    }

	    if (tangent >= 0) {
		glEnableVertexAttribArray (tangent);
		glVertexAttribPointer (
		    tangent, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*> (mesh.tangentOffset)
		);
	    }

	    if (texCoord >= 0) {
		glEnableVertexAttribArray (texCoord);
		glVertexAttribPointer (
		    texCoord, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*> (mesh.uvOffset)
		);
	    }

	    if (this->m_skinningEnabled && blendIndices >= 0) {
		glEnableVertexAttribArray (blendIndices);
		glVertexAttribIPointer (
		    blendIndices, 4, GL_UNSIGNED_INT, stride, reinterpret_cast<void*> (mesh.blendIndicesOffset)
		);
	    }

	    if (this->m_skinningEnabled && blendWeights >= 0) {
		glEnableVertexAttribArray (blendWeights);
		glVertexAttribPointer (
		    blendWeights, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*> (mesh.blendWeightsOffset)
		);
	    }
	},
	[this, submeshIndex] () {
	    // Wallpaper Engine is a Direct3D engine: with D3D's default rasterizer state the
	    // kept faces are the mirror of OpenGL's default, so clockwise is front here
	    // (verified against this scene's skybox, which is only visible from inside).
	    // A Y-flipped projection mirrors the winding once more, back to counter-clockwise.
	    glFrontFace (this->getScene ().getCamera ().isYFlipped () ? GL_CCW : GL_CW);
	    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, this->m_submeshes[submeshIndex].indexBuffer);
	    glDrawElements (GL_TRIANGLES, this->m_submeshes[submeshIndex].indexCount, GL_UNSIGNED_INT, nullptr);
	    glFrontFace (GL_CCW);
	},
	[pass] () {
	    for (const auto* name :
		 { "a_Position", "a_Normal", "a_Tangent4", "a_TexCoord", "a_BlendIndices", "a_BlendWeights" }) {
		const GLint location = glGetAttribLocation (pass->getProgramID (), name);

		if (location >= 0) {
		    glDisableVertexAttribArray (location);
		}
	    }
	}
    );
}

void CModel::updateAnimationPose () const {
    const auto& animationData = this->m_model.animationData;
    if (animationData.bones.empty ()) {
	return;
    }

    std::vector<MdlActiveAnimation> activeAnimations;
    const float sceneTime = this->getScene ().getTime ();
    if (sceneTime == this->m_poseTime) {
	return;
    }

    if (this->m_model.animationLayers.empty ()) {
	if (!animationData.animations.empty ()) {
	    activeAnimations.push_back ({ .animation = &animationData.animations.front (), .time = sceneTime });
	}
    } else {
	for (const auto& layer : this->m_model.animationLayers) {
	    const auto animationId = static_cast<uint32_t> (layer->animation->value->getInt ());
	    const auto found = std::find_if (
		animationData.animations.begin (), animationData.animations.end (),
		[animationId] (const MdlAnimationClip& animation) { return animation.id == animationId; }
	    );
	    if (found == animationData.animations.end ()) {
		continue;
	    }

	    const bool visible = layer->visible->value->getBool ();
	    activeAnimations.push_back (
		{
		    .animation = &*found,
		    .time = sceneTime * layer->rate->value->getFloat (),
		    .weight = visible ? std::clamp (layer->blend->value->getFloat (), 0.0f, 1.0f) : 0.0f,
		    .additive = layer->additive->value->getBool (),
		}
	    );
	}
    }

    auto pose = MdlAnimationEvaluator::evaluate (animationData, activeAnimations);
    this->m_worldBones = std::move (pose.worldBones);
    this->m_skinBones = std::move (pose.skinBones);
    this->m_gpuSkinBones.resize (this->m_skinBones.size ());
    for (size_t bone = 0; bone < this->m_skinBones.size (); bone++) {
	this->m_gpuSkinBones[bone] = glm::mat4x3 (this->m_skinBones[bone]);
    }
    this->m_poseTime = sceneTime;
}

void CModel::updateMatrices () {
    const auto& camera = this->getScene ().getCamera ();

    this->m_modelMatrix = this->resolveWorldMatrix ();
    this->m_viewProjectionMatrix = camera.getProjection () * camera.getLookAt ();
    this->m_modelViewProjectionMatrix = this->m_viewProjectionMatrix * this->m_modelMatrix;
    this->m_modelViewProjectionMatrixInverse = glm::inverse (this->m_modelViewProjectionMatrix);
    this->m_normalMatrix = glm::inverseTranspose (glm::mat3 (this->m_modelMatrix));

    // Wallpaper Engine's generic model shaders use the normal matrix to build
    // the tangent basis but do not normalize its tangent/bitangent outputs.
    // Keep the inverse-transpose orientation while removing object scale from
    // each basis vector. Small authored model scales (commonly 0.01) otherwise
    // magnify tangent-space normals by 100 and create clipped, noisy lighting.
    for (int column = 0; column < 3; column++) {
	const float length = glm::length (this->m_normalMatrix[column]);
	if (length > 1e-6f) {
	    this->m_normalMatrix[column] /= length;
	}
    }
}

void CModel::render () {
    if (!this->m_initialized) {
	return;
    }

    if (!this->m_model.groupVisible->value->getBool ()) {
	return;
    }

    this->updateAnimationPose ();
    this->updateMatrices ();

#if !NDEBUG
    const std::string debugName
	= "Model " + this->m_model.name + " (" + std::to_string (this->getId ()) + ", " + this->m_model.filename + ")";

    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, debugName.c_str ());
#endif /* DEBUG */

    // keep the scene framebuffer's alpha untouched, same as image layers do
    glColorMask (true, true, true, false);

    for (const auto& pass : this->m_passes) {
	pass->render ();
    }

    glColorMask (true, true, true, true);

#if !NDEBUG
    glPopDebugGroup ();
#endif /* DEBUG */
}

void CModel::renderShadow (const glm::mat4& lightViewProjection) {
    if (!this->m_initialized || this->m_shadowProgram == GL_NONE
	|| !this->m_model.groupVisible->value->getBool () || !this->isVisibleThroughParents ()) {
	return;
    }

    this->updateAnimationPose ();
    this->m_modelMatrix = this->resolveWorldMatrix ();

    if (this->m_shadowVao == GL_NONE) {
	glGenVertexArrays (1, &this->m_shadowVao);
    }
    glBindVertexArray (this->m_shadowVao);
    glUseProgram (this->m_shadowProgram);
    glUniformMatrix4fv (
	this->m_shadowLightViewProjection, 1, GL_FALSE, glm::value_ptr (lightViewProjection)
    );
    glUniformMatrix4fv (this->m_shadowModel, 1, GL_FALSE, glm::value_ptr (this->m_modelMatrix));
    if (this->m_skinningEnabled && this->m_shadowBones >= 0 && !this->m_gpuSkinBones.empty ()) {
	glUniformMatrix4x3fv (
	    this->m_shadowBones, static_cast<GLsizei> (this->m_gpuSkinBones.size ()), GL_FALSE,
	    glm::value_ptr (this->m_gpuSkinBones.front ())
	);
    }

    const GLsizei stride = static_cast<GLsizei> (this->m_model.mesh.strideBytes);
    for (size_t submeshIndex = 0; submeshIndex < this->m_submeshes.size (); submeshIndex++) {
	const auto& material = this->m_model.materials[submeshIndex];
	const auto opaquePass = std::find_if (
	    material->passes.begin (), material->passes.end (), [] (const auto& pass) {
		return pass->blending == BlendingMode_Normal;
	    }
	);
	if (opaquePass == material->passes.end ()) {
	    continue;
	}

	if ((*opaquePass)->cullmode == CullingMode_Normal) {
	    glEnable (GL_CULL_FACE);
	} else {
	    glDisable (GL_CULL_FACE);
	}
	// Model data follows Direct3D's winding convention; the shadow projection has
	// no output-dependent Y flip, so clockwise triangles are front-facing here.
	glFrontFace (GL_CW);

	glBindBuffer (GL_ARRAY_BUFFER, this->m_submeshes[submeshIndex].vertexBuffer);
	glEnableVertexAttribArray (0);
	glVertexAttribPointer (
	    0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*> (this->m_model.mesh.positionOffset)
	);
	if (this->m_skinningEnabled) {
	    glEnableVertexAttribArray (1);
	    glVertexAttribIPointer (
		1, 4, GL_UNSIGNED_INT, stride,
		reinterpret_cast<void*> (this->m_model.mesh.blendIndicesOffset)
	    );
	    glEnableVertexAttribArray (2);
	    glVertexAttribPointer (
		2, 4, GL_FLOAT, GL_FALSE, stride,
		reinterpret_cast<void*> (this->m_model.mesh.blendWeightsOffset)
	    );
	}

	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, this->m_submeshes[submeshIndex].indexBuffer);
	glDrawElements (
	    GL_TRIANGLES, this->m_submeshes[submeshIndex].indexCount, GL_UNSIGNED_INT, nullptr
	);
    }

    glDisableVertexAttribArray (0);
    if (this->m_skinningEnabled) {
	glDisableVertexAttribArray (1);
	glDisableVertexAttribArray (2);
    }
}

const Model3D& CModel::getModel () const { return this->m_model; }

std::optional<glm::mat4> CModel::getAttachmentTransform (const std::string& name) const {
    this->updateAnimationPose ();
    return MdlAnimationEvaluator::attachmentTransform (this->m_model.animationData, this->m_worldBones, name);
}

const float& CModel::getBrightness () const { return this->m_brightness; }

const float& CModel::getUserAlpha () const { return this->m_alpha; }

const float& CModel::getAlpha () const { return this->m_alpha; }

const glm::vec3& CModel::getColor () const { return this->m_color; }

const glm::vec4& CModel::getColor4 () const { return this->m_color4; }

const glm::vec3& CModel::getCompositeColor () const { return this->m_color; }
