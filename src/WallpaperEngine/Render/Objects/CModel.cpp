#include "CModel.h"

#include <glm/gtc/matrix_inverse.hpp>

#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render::Objects;

CModel::CModel (Wallpapers::CScene& scene, const Model3D& model) :
    CObject (scene, model), CRenderable (scene, model, **model.materials.begin ()), ScriptableObject (scene, model),
    m_model (model) { }

CModel::~CModel () {
    for (const auto& pass : this->m_passes) {
	delete pass;
    }

    for (const auto& submesh : this->m_submeshes) {
	glDeleteBuffers (1, &submesh.vertexBuffer);
	glDeleteBuffers (1, &submesh.indexBuffer);
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
	    auto* pass = new Effects::CPass (
		*this, std::make_shared<FBOProvider> (this), *cur, std::nullopt, std::nullopt, std::nullopt
	    );

	    pass->setDestination (this->getScene ().getFBO ());
	    pass->setInput (this->m_texture);
	    pass->setModelViewProjectionMatrix (&this->m_modelViewProjectionMatrix);
	    pass->setModelViewProjectionMatrixInverse (&this->m_modelViewProjectionMatrixInverse);
	    pass->setModelMatrix (&this->m_modelMatrix);
	    pass->setViewProjectionMatrix (&this->m_viewProjectionMatrix);
	    pass->addUniform ("g_NormalModelMatrix", &this->m_normalMatrix);
	    this->setupGeometryCallback (pass, submeshIndex);

	    this->m_passes.push_back (pass);
	}
    }

    this->m_initialized = true;
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
	    for (const auto* name : { "a_Position", "a_Normal", "a_Tangent4", "a_TexCoord" }) {
		const GLint location = glGetAttribLocation (pass->getProgramID (), name);

		if (location >= 0) {
		    glDisableVertexAttribArray (location);
		}
	    }
	}
    );
}

void CModel::updateMatrices () {
    const auto& camera = this->getScene ().getCamera ();

    this->m_modelMatrix = this->resolveWorldMatrix ();
    this->m_viewProjectionMatrix = camera.getProjection () * camera.getLookAt ();
    this->m_modelViewProjectionMatrix = this->m_viewProjectionMatrix * this->m_modelMatrix;
    this->m_modelViewProjectionMatrixInverse = glm::inverse (this->m_modelViewProjectionMatrix);
    this->m_normalMatrix = glm::inverseTranspose (glm::mat3 (this->m_modelMatrix));
}

void CModel::render () {
    if (!this->m_initialized) {
	return;
    }

    if (!this->m_model.groupVisible->value->getBool ()) {
	return;
    }

    this->updateMatrices ();

#if !NDEBUG
    const std::string debugName = "Model " + this->m_model.name + " (" + std::to_string (this->getId ()) + ", "
	+ this->m_model.filename + ")";

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

const Model3D& CModel::getModel () const { return this->m_model; }

const float& CModel::getBrightness () const { return this->m_brightness; }

const float& CModel::getUserAlpha () const { return this->m_alpha; }

const float& CModel::getAlpha () const { return this->m_alpha; }

const glm::vec3& CModel::getColor () const { return this->m_color; }

const glm::vec4& CModel::getColor4 () const { return this->m_color4; }

const glm::vec3& CModel::getCompositeColor () const { return this->m_color; }
