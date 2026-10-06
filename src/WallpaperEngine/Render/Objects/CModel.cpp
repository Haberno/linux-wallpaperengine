#include "CModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <ranges>
#include <sstream>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "WallpaperEngine/Data/Model/MdlAnimation.h"
#include "WallpaperEngine/Data/Model/MdlMorph.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render::Objects;
using WallpaperEngine::Render::RenderSortClass;

namespace {
bool supportsModelMorphShader (const std::string& shader) {
    // Stock generic3/4 and model_vertex_v1 consumers apply the same local
    // position/normal deltas before skinning and any shader-specific motion.
    return shader == "generic3" || shader == "generic4" || shader == "chroma4"
        || shader == "foliage4" || shader == "fur4";
}

RenderSortClass passRenderClass (const BlendingMode mode) {
    switch (mode) {
	case BlendingMode_Translucent:
	    return RenderSortClass::Translucent;
	case BlendingMode_Additive:
	    return RenderSortClass::Additive;
	case BlendingMode_Normal:
	case BlendingMode_AlphaToCoverage:
	case BlendingMode_Unknown:
	default:
	    return RenderSortClass::Opaque;
    }
}
} // namespace

ModelData::ModelData (std::vector<Scripting::ModelDataShape> input) {
    glm::vec3 minimum (std::numeric_limits<float>::infinity ());
    glm::vec3 maximum (-std::numeric_limits<float>::infinity ());
    bool hasPositions = false;
    for (auto& source : input) {
	Shape shape;
	shape.input = std::move (source);
	shape.drawCount = static_cast<GLsizei> (shape.input.indexWidth == 0
	    ? shape.input.vertices.size () * sizeof (float) / shape.input.strideBytes
	    : shape.input.indices.size () / shape.input.indexWidth);
	updateLayout (shape);
	const auto& layout = shape.layout;
	if (layout.positionOffset != MdlMesh::AttributeAbsent) {
	    for (size_t vertex = 0; vertex < shape.input.vertices.size ();
		 vertex += layout.strideBytes / sizeof (float)) {
		const float* position = shape.input.vertices.data () + vertex + layout.positionOffset / sizeof (float);
		minimum = glm::min (minimum, glm::vec3 (position[0], position[1], position[2]));
		maximum = glm::max (maximum, glm::vec3 (position[0], position[1], position[2]));
		hasPositions = true;
	    }
	}
	if (shapes.empty ()) {
	    mesh = layout;
	}
	MdlSubmesh submesh;
	submesh.vertexTag = layout.vertexTag;
	submesh.strideBytes = layout.strideBytes;
	submesh.materialPath = shape.input.material;
	mesh.submeshes.push_back (std::move (submesh));
	shapes.push_back (std::move (shape));
    }
    mesh.hasBoundingBox = hasPositions;
    if (hasPositions) {
	mesh.boundingBoxMin = minimum;
	mesh.boundingBoxMax = maximum;
    }
}

void ModelData::updateLayout (Shape& shape) {
    auto& layout = shape.layout;
    layout.vertexTag = shape.input.vertexFormat;
    layout.strideBytes = shape.input.strideBytes;
    uint32_t offset = 0;
    const auto attribute = [&] (uint32_t flag, uint32_t components) {
	if ((layout.vertexTag & flag) == 0) {
	    return MdlMesh::AttributeAbsent;
	}
	const auto start = offset;
	offset += components * sizeof (float);
	return start;
    };
    layout.positionOffset = attribute (1, 3);
    layout.normalOffset = attribute (2, 3);
    layout.tangentOffset = attribute (4, 4);
    layout.uvOffset = attribute (8, 2);
    shape.colorOffset = attribute (0x8000, 4);
}

void ModelData::upload () {
    GLint arrayBuffer = 0;
    glGetIntegerv (GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    for (auto& shape : shapes) {
	glGenBuffers (1, &shape.vertexBuffer);
	glBindBuffer (GL_ARRAY_BUFFER, shape.vertexBuffer);
	glBufferData (
	    GL_ARRAY_BUFFER, shape.input.vertices.size () * sizeof (float), shape.input.vertices.data (),
	    shape.input.vertexDynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW
	);
	if (shape.input.indexWidth != 0) {
	    glGenBuffers (1, &shape.indexBuffer);
	    glBindBuffer (GL_ARRAY_BUFFER, shape.indexBuffer);
	    glBufferData (
		GL_ARRAY_BUFFER, shape.input.indices.size (), shape.input.indices.data (),
		shape.input.indexDynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW
	    );
	}
    }
    glBindBuffer (GL_ARRAY_BUFFER, static_cast<GLuint> (arrayBuffer));
}

const char* ModelData::applyData (const std::vector<Scripting::ModelDataShape>& updates) {
    if (updates.size () > shapes.size ()) {
	return "Cannot add shapes in IModelData.update";
    }
    for (size_t index = 0; index < updates.size (); ++index) {
	const auto& update = updates[index];
	auto& shape = shapes[index];
	const auto& original = shape.input;
	if (update.deleteShape || update.deleteIndices) {
	    return "Cannot delete shape or buffers in IModelData.update";
	}
	if (!update.material.empty () && update.material != original.material) {
	    return "Material cannot be changed in IModelData.update";
	}
	if (update.vertexFormat != 0 && update.vertexFormat != original.vertexFormat) {
	    return "Vertex format cannot be changed in IModelData.update";
	}
	const size_t indexCount = update.indexCount;
	const size_t originalIndexCount = original.indexWidth == 0 ? 0 : original.indices.size () / original.indexWidth;
	if ((update.hasVertices && (!original.vertexDynamic || update.vertexFloatCount > original.vertices.size ()))
	    || (update.indexWidth != 0 && (!original.indexDynamic || indexCount > originalIndexCount))) {
	    return "Model data not created as dynamic";
	}
	auto uploadBytes = [] (GLuint buffer, size_t bytes, const void* data) {
	    GLint previous = 0;
	    glGetIntegerv (GL_ARRAY_BUFFER_BINDING, &previous);
	    glBindBuffer (GL_ARRAY_BUFFER, buffer);
	    glBufferSubData (GL_ARRAY_BUFFER, 0, bytes, data);
	    glBindBuffer (GL_ARRAY_BUFFER, static_cast<GLuint> (previous));
	};
	if (update.hasVertices) {
	    // Native maps the full allocation and copies that size, including a short
	    // view's backing tail. Parsing already copied and bounds-checked those bytes.
	    uploadBytes (shape.vertexBuffer, original.vertices.size () * sizeof (float), update.vertices.data ());
	    std::copy_n (update.vertices.begin (), original.vertices.size (), shape.input.vertices.begin ());
	    const size_t count = original.indexWidth == 0 ? update.vertexFloatCount / original.strideBytes : indexCount;
	    if (count > 0) {
		shape.drawCount = static_cast<GLsizei> (count);
	    }
	}
	if (update.indexWidth != 0) {
	    if (update.indexWidth != original.indexWidth) {
		return "Incorrect index buffer type";
	    }
	    uploadBytes (shape.indexBuffer, original.indices.size (), update.indices.data ());
	    std::copy_n (update.indices.begin (), original.indices.size (), shape.input.indices.begin ());
	    shape.drawCount = static_cast<GLsizei> (indexCount);
	}
    }
    return nullptr;
}

const char* ModelData::replaceData (std::vector<Scripting::ModelDataShape> replacements) {
    // The same shader remains compatible when every original semantic is kept.
    // Material changes and semantic removals still require shader validation.
    if (replacements.size () > shapes.size ()) {
	return "IModelData.replaceData currently requires the existing shape count";
    }
    for (size_t index = 0; index < replacements.size (); ++index) {
	const auto& replacement = replacements[index];
	const auto& original = shapes[index].input;
	const bool layoutChanged = replacement.vertexFormat != 0 && replacement.vertexFormat != original.vertexFormat;
	const size_t originalIndexCount = original.indexWidth == 0 ? 0 : original.indices.size () / original.indexWidth;
	const bool recreate = layoutChanged || (replacement.deleteIndices && original.indexWidth != 0)
	    || (replacement.hasVertices && (!original.vertexDynamic || replacement.vertexFloatCount > original.vertices.size ()))
	    || (replacement.indexWidth != 0 && (!original.indexDynamic || replacement.indexCount > originalIndexCount));
	if (replacement.deleteShape
	    || (!replacement.material.empty () && replacement.material != original.material)
	    || (layoutChanged && ((replacement.vertexFormat & original.vertexFormat) != original.vertexFormat
		|| (replacement.vertexFormat & ~0x800fu) != 0))
	    || (!layoutChanged && replacement.vertexFormat != 0 && replacement.strideBytes != original.strideBytes)
	    || (!recreate && original.indexWidth == 0 && replacement.indexWidth != 0)
	    || (recreate && (!replacement.hasVertices
		|| replacement.vertexFormat == 0 || replacement.strideBytes == 0
		|| replacement.material != original.material
		|| replacement.vertexFloatCount > replacement.vertices.size ()
		|| (replacement.indexWidth != 0 && (replacement.indexWidth != 2 && replacement.indexWidth != 4))
		|| (replacement.indexWidth != 0 && replacement.indexCount > replacement.indices.size () / replacement.indexWidth)))) {
	    return "IModelData.replaceData currently requires unchanged material, retained semantics and complete recreated shapes";
	}
	if (!recreate && ((replacement.hasVertices && replacement.vertices.size () != original.vertices.size ())
	    || (replacement.indexWidth != 0 && replacement.indexWidth == original.indexWidth
		&& replacement.indices.size () != original.indices.size ()))) {
	    return "Model replacement backing copy does not match the original allocation";
	}
	uint32_t packedStride = 0;
	if (layoutChanged) {
	    for (const auto [flag, components] : std::array<std::pair<uint32_t, uint32_t>, 5> {
		     { { 1, 3 }, { 2, 3 }, { 4, 4 }, { 8, 2 }, { 0x8000, 4 } } }) {
		if ((replacement.vertexFormat & flag) != 0) {
		    packedStride += components * sizeof (float);
		}
	    }
	}
	if (recreate && ((layoutChanged && replacement.strideBytes != packedStride)
	    || (replacement.vertexFloatCount * sizeof (float)) % replacement.strideBytes != 0)) {
	    return "Inconsistent vertex buffer size";
	}
    }
    GLint previous = 0;
    glGetIntegerv (GL_ARRAY_BUFFER_BINDING, &previous);
    for (size_t index = 0; index < replacements.size (); ++index) {
	auto& shape = shapes[index];
	auto& replacement = replacements[index];
	const bool layoutChanged = replacement.vertexFormat != 0 && replacement.vertexFormat != shape.input.vertexFormat;
	const size_t originalIndexCount = shape.input.indexWidth == 0 ? 0 : shape.input.indices.size () / shape.input.indexWidth;
	const bool recreate = layoutChanged || (replacement.deleteIndices && shape.input.indexWidth != 0)
	    || (replacement.hasVertices && (!shape.input.vertexDynamic || replacement.vertexFloatCount > shape.input.vertices.size ()))
	    || (replacement.indexWidth != 0 && (!shape.input.indexDynamic || replacement.indexCount > originalIndexCount));
	if (!recreate) {
	    // Native maps only supplied buffers and retains original flags. Its vertex
	    // write/count change survives a subsequently rejected index buffer type.
	    if (replacement.hasVertices) {
		glBindBuffer (GL_ARRAY_BUFFER, shape.vertexBuffer);
		glBufferSubData (GL_ARRAY_BUFFER, 0, replacement.vertices.size () * sizeof (float), replacement.vertices.data ());
		const size_t count = shape.input.indexWidth == 0
		    ? replacement.vertexFloatCount / shape.input.strideBytes : replacement.indexCount;
		if (count > 0) {
		    shape.drawCount = static_cast<GLsizei> (count);
		}
		shape.input.vertices = std::move (replacement.vertices);
	    }
	    if (replacement.indexWidth != 0) {
		if (replacement.indexWidth != shape.input.indexWidth) {
		    glBindBuffer (GL_ARRAY_BUFFER, static_cast<GLuint> (previous));
		    return "Incorrect index buffer type";
		}
		glBindBuffer (GL_ARRAY_BUFFER, shape.indexBuffer);
		glBufferSubData (GL_ARRAY_BUFFER, 0, replacement.indices.size (), replacement.indices.data ());
		shape.drawCount = static_cast<GLsizei> (replacement.indexCount);
		shape.input.indices = std::move (replacement.indices);
	    }
	    continue;
	}
	// Parsing may have captured dynamic capacities before a property getter
	// changed the original flags. Recreated buffers use only their logical views.
	replacement.vertices.resize (replacement.vertexFloatCount);
	replacement.indices.resize (replacement.indexCount * replacement.indexWidth);
	shape.input = std::move (replacement);
	if (layoutChanged) {
	    updateLayout (shape);
	    mesh.submeshes[index].vertexTag = shape.layout.vertexTag;
	    mesh.submeshes[index].strideBytes = shape.layout.strideBytes;
	    if (index == 0) {
		mesh.vertexTag = shape.layout.vertexTag;
		mesh.strideBytes = shape.layout.strideBytes;
		mesh.positionOffset = shape.layout.positionOffset;
		mesh.normalOffset = shape.layout.normalOffset;
		mesh.tangentOffset = shape.layout.tangentOffset;
		mesh.uvOffset = shape.layout.uvOffset;
	    }
	}
	// Keep the names referenced by every live color/shadow VAO while replacing
	// their storage. Static buffers, capacity growth or layout changes recreate both complete buffers.
	glBindBuffer (GL_ARRAY_BUFFER, shape.vertexBuffer);
	glBufferData (GL_ARRAY_BUFFER, shape.input.vertices.size () * sizeof (float), shape.input.vertices.data (),
	    shape.input.vertexDynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
	if (shape.input.indexWidth != 0) {
	    if (shape.indexBuffer == GL_NONE) {
		glGenBuffers (1, &shape.indexBuffer);
	    }
	    glBindBuffer (GL_ARRAY_BUFFER, shape.indexBuffer);
	    glBufferData (GL_ARRAY_BUFFER, shape.input.indices.size (), shape.input.indices.data (),
		shape.input.indexDynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
	} else if (shape.indexBuffer != GL_NONE) {
	    glDeleteBuffers (1, &shape.indexBuffer);
	    shape.indexBuffer = GL_NONE;
	}
	shape.drawCount = static_cast<GLsizei> (shape.input.indexWidth == 0
	    ? shape.input.vertices.size () * sizeof (float) / shape.input.strideBytes
	    : shape.input.indices.size () / shape.input.indexWidth);
    }
    glBindBuffer (GL_ARRAY_BUFFER, static_cast<GLuint> (previous));
    return nullptr;
}

ModelData::~ModelData () {
    for (const auto& shape : shapes) {
	glDeleteBuffers (1, &shape.vertexBuffer);
	glDeleteBuffers (1, &shape.indexBuffer);
    }
}

CModel::CModel (Wallpapers::CScene& scene, const Model3D& model) :
    CObject (scene, model), CRenderable (scene, model, **model.materials.begin ()), ScriptableObject (scene, model),
    m_model (model), m_modelData (scene.findModelData (model.modelDataToken)),
    m_size (model.mesh.hasBoundingBox ? model.mesh.boundingBoxMax - model.mesh.boundingBoxMin : glm::vec3 (0.0f)) {
    // Some older Workshop scenes bind the editor's 2D pivot script to a 3D model.
    // Expose its authored bounds as a vector-like size so those published scripts can
    // keep calculating their pivot even though current IModelLayer docs omit the field.
    this->registerProperty ("size", this->m_size);
    this->registerProperty ("castshadow", *model.castShadow->value);
    this->registerProperty ("perspective", *model.perspective->value);
    for (size_t materialIndex = 0; materialIndex < model.materials.size (); ++materialIndex) {
	const auto& material = *model.materials[materialIndex];
	for (size_t passIndex = 0; passIndex < material.passes.size (); ++passIndex) {
	    const auto scope = "material_" + std::to_string (materialIndex) + "_pass_" + std::to_string (passIndex) + "_";
	    for (const auto& [name, setting] : material.passes[passIndex]->constants) {
		if (setting->animation != nullptr || setting->value->getScriptSource ().has_value ()) {
		    registerProperty (scope + name, *setting, scope);
		}
	    }
	}
    }
    const auto addLayer = [&] (const ImageAnimationLayer* settings, const MdlAnimationClip* clip) {
	auto layer = std::make_unique<AnimationLayer> ();
	layer->settings = settings;
	layer->clip = clip;
	layer->key = "__model_animation_" + std::to_string (m_nextAnimationLayer++);
	layer->timeline.name = settings != nullptr ? settings->name : clip->name;
	if (clip != nullptr) {
	    layer->timeline.fps = clip->fps;
	    layer->timeline.length = static_cast<float> (clip->frameCount);
	    layer->timeline.mode = clip->mode.empty () ? "loop" : clip->mode;
	    layer->timeline.events = clip->events;
	}
	if (settings != nullptr) layer->timeline.rate = settings->rate->value->getFloat ();
	registerAnimation (layer->key, layer->timeline);
	m_animationLayers.push_back (std::move (layer));
    };
    for (const auto& settings : model.animationLayers) {
	const auto found = std::find_if (model.animationData.animations.begin (), model.animationData.animations.end (),
	    [&] (const MdlAnimationClip& clip) {
		return clip.id == static_cast<uint32_t> (settings->animation->value->getInt ());
	    });
	addLayer (settings.get (), found == model.animationData.animations.end () ? nullptr : &*found);
	const auto prefix = "animation_" + std::to_string (settings->id) + "_";
	for (const auto& [name, value] : std::initializer_list<std::pair<const char*, const UserSetting*>> {
	    { "rate", settings->rate.get () }, { "blend", settings->blend.get () },
	    { "visible", settings->visible.get () }, { "animation", settings->animation.get () },
	    { "additive", settings->additive.get () }, { "blendin", settings->blendIn.get () },
	    { "blendout", settings->blendOut.get () }, { "blendtime", settings->blendTime.get () } }) {
	    registerProperty (prefix + name, *value);
	    if (value->animation != nullptr) registerAnimation (prefix + name, *value->animation);
	}
    }
}

CModel::~CModel () {
    for (const auto& pass : this->m_passes) {
	delete pass;
    }

    for (const auto& submesh : this->m_submeshes) {
	if (!m_modelData) {
	    glDeleteBuffers (1, &submesh.vertexBuffer);
	    glDeleteBuffers (1, &submesh.indexBuffer);
	}
	if (submesh.shadowVao != GL_NONE) glDeleteVertexArrays (1, &submesh.shadowVao);
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
    this->m_morphingEnabled = !m_modelData && !m_model.animationData.bones.empty ()
        && std::ranges::any_of (m_model.mesh.submeshes, [] (const auto& body) { return !body.morphTargets.empty (); });

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

	if (m_modelData) {
	    const auto& shape = m_modelData->shapes[submeshIndex];
	    buffers.vertexBuffer = shape.vertexBuffer;
	    buffers.indexBuffer = shape.indexBuffer;
	    buffers.vertexCount
		= static_cast<GLsizei> (shape.input.vertices.size () * sizeof (float) / shape.input.strideBytes);
	    buffers.indexType = shape.input.indexWidth == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	    buffers.indexCount = shape.input.indexWidth == 0
		? 0
		: static_cast<GLsizei> (shape.input.indices.size () / shape.input.indexWidth);
	} else {
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

	    // a container can pair vertex layouts (puppet models add a channelmap overlay next to
	    // the skinned body); the attribute setup below is built for the mesh-level tag, so a
	    // submesh that disagrees would read its vertices through the wrong stride
	    buffers.indexCount = submesh.vertexTag == this->m_model.mesh.vertexTag
		? static_cast<GLsizei> (submesh.indices.size ())
		: 0;
	}
	const auto& material = this->m_model.materials[submeshIndex];
	const auto opaquePass = std::find_if (material->passes.begin (), material->passes.end (), [] (const auto& pass) {
	    return pass->blending == BlendingMode_Normal;
	});
	buffers.castsShadow = opaquePass != material->passes.end ();
	buffers.shadowCulling = buffers.castsShadow && (*opaquePass)->cullmode == CullingMode_Normal;
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
            if (m_morphingEnabled && supportsModelMorphShader (cur->shader)) {
                // Position/normal morphs are already applied to the shared color/shadow VBO.
                runtimeCombos.insert_or_assign ("MORPHING", 0);
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
	    pass->setModelViewProjectionMatrix (&this->m_modelViewProjectionMatrix, getScene ().getCamera ().usesReverseDepth ());
	    pass->setModelViewProjectionMatrixInverse (&this->m_modelViewProjectionMatrixInverse);
	    pass->setModelMatrix (&this->m_modelMatrix);
	    pass->setViewProjectionMatrix (&this->m_viewProjectionMatrix);
	    pass->addUniform ("g_NormalModelMatrix", &this->m_normalMatrix);
	    pass->addUniform ("g_EyePosition", &this->m_eyePosition);
	    if (this->m_skinningEnabled) {
		pass->addUniform (
		    "g_Bones", this->m_gpuSkinBones.data (), static_cast<int> (this->m_gpuSkinBones.size ())
		);
	    }
	    this->setupGeometryCallback (pass, submeshIndex);

	    this->m_passes.push_back (pass);
	    this->getScene ().loadingCheckpoint ();
	}
    }

    std::vector<BlendingMode> passModes;
    passModes.reserve (this->m_passes.size ());
    for (const auto* pass : this->m_passes) {
	passModes.push_back (pass->getBlendingMode ());
    }

    const std::vector<size_t> permutation = calculatePassRenderPermutation (passModes);
    std::vector<Effects::CPass*> orderedPasses;
    orderedPasses.reserve (this->m_passes.size ());
    for (const size_t index : permutation) {
	orderedPasses.push_back (this->m_passes[index]);
    }
    this->m_passes = std::move (orderedPasses);
    for (const auto* pass : this->m_passes) {
	const RenderSortClass renderClass = passRenderClass (pass->getBlendingMode ());
	if (std::ranges::find (this->m_renderSortClasses, renderClass) == this->m_renderSortClasses.end ()) {
	    this->m_renderSortClasses.push_back (renderClass);
	}
    }

    if (this->getScene ().getLights ().shadowViewCount > 0) {
	this->setupShadowProgram ();
	this->m_shadowBounds = ModelBounds (this->m_model.mesh, this->m_skinningEnabled ? this->m_gpuSkinBones.size () : 0);
    }

    this->m_initialized = true;
}

std::vector<size_t> CModel::calculatePassRenderPermutation (const std::vector<BlendingMode>& modes) {
    std::vector<size_t> permutation (modes.size ());
    std::iota (permutation.begin (), permutation.end (), 0);
    // Native model sorting puts opaque/coverage surfaces first, but treats
    // translucent and additive surfaces as peers. The glass over Sly's roof
    // lamps must therefore remain after the earlier additive lamp surface.
    std::stable_sort (permutation.begin (), permutation.end (), [&modes] (const size_t left, const size_t right) {
	return (passRenderClass (modes[left]) != RenderSortClass::Opaque)
	    < (passRenderClass (modes[right]) != RenderSortClass::Opaque);
    });
    return permutation;
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
    std::shared_ptr<Shaders::ShaderProgramCache::SharingGroup> group;
    this->m_shadowProgram = this->getScene ().getContext ().getShaderProgramCache ().interfaceProgram (
	vertex.str (), fragment, group
    );
    // Identical depth shaders upload their complete matrix/bone layout on every draw.
    this->m_sharedShadowProgram = Shaders::ShaderProgramCache::shareProgram (
	this->m_shadowProgram->id, group, "model-shadow"
    );

    this->m_shadowLightViewProjection = glGetUniformLocation (this->m_shadowProgram->id, "u_LightViewProjection");
    this->m_shadowModel = glGetUniformLocation (this->m_shadowProgram->id, "u_Model");
    this->m_shadowBones = glGetUniformLocation (this->m_shadowProgram->id, "u_Bones");
}

void CModel::setupGeometryCallback (Effects::CPass* pass, size_t submeshIndex) {
    pass->setGeometryCallback (
	[this, pass, submeshIndex, revision = uint64_t (0), vertexTag = uint32_t (0),
	 locations = std::array<GLint, 7> { -1, -1, -1, -1, -1, -1, -1 }] () mutable {
	    const auto& mesh = m_modelData ? m_modelData->shapes[submeshIndex].layout : m_model.mesh;
	    if (revision == pass->getShaderRevision () && vertexTag == mesh.vertexTag) {
		return;
	    }
	    revision = pass->getShaderRevision ();
	    vertexTag = mesh.vertexTag;
	    const auto stride = static_cast<GLsizei> (mesh.strideBytes);
	    // Every retained pass refreshes after a shader reload or shared layout change.
	    for (const auto location : locations) {
		if (location >= 0) {
		    glDisableVertexAttribArray (location);
		}
	    }
	    const GLint position = glGetAttribLocation (pass->getProgramID (), "a_Position");
	    const GLint normal = glGetAttribLocation (pass->getProgramID (), "a_Normal");
	    const GLint tangent = glGetAttribLocation (pass->getProgramID (), "a_Tangent4");
	    const GLint texCoord = glGetAttribLocation (pass->getProgramID (), "a_TexCoord");
	    const GLint blendIndices = glGetAttribLocation (pass->getProgramID (), "a_BlendIndices");
	    const GLint blendWeights = glGetAttribLocation (pass->getProgramID (), "a_BlendWeights");

	    const GLint color = glGetAttribLocation (pass->getProgramID (), "a_Color");
	    locations = { position, normal, tangent, texCoord, blendIndices, blendWeights, color };
	    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, m_modelData ? m_modelData->shapes[submeshIndex].indexBuffer
		: this->m_submeshes[submeshIndex].indexBuffer);
	    glBindBuffer (GL_ARRAY_BUFFER, this->m_submeshes[submeshIndex].vertexBuffer);

	    if (position >= 0 && mesh.positionOffset != MdlMesh::AttributeAbsent) {
		glEnableVertexAttribArray (position);
		glVertexAttribPointer (
		    position, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*> (mesh.positionOffset)
		);
	    }

	    // a shader can ask for an attribute the vertex tag left out; leaving the array
	    // disabled feeds the generic default instead of reinterpreting another attribute
	    if (normal >= 0 && mesh.normalOffset != MdlMesh::AttributeAbsent) {
		glEnableVertexAttribArray (normal);
		glVertexAttribPointer (
		    normal, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*> (mesh.normalOffset)
		);
	    }

	    if (tangent >= 0 && mesh.tangentOffset != MdlMesh::AttributeAbsent) {
		glEnableVertexAttribArray (tangent);
		glVertexAttribPointer (
		    tangent, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*> (mesh.tangentOffset)
		);
	    }

	    if (texCoord >= 0 && mesh.uvOffset != MdlMesh::AttributeAbsent) {
		glEnableVertexAttribArray (texCoord);
		glVertexAttribPointer (
		    texCoord, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*> (mesh.uvOffset)
		);
	    }

	    if (color >= 0 && m_modelData
		&& m_modelData->shapes[submeshIndex].colorOffset != MdlMesh::AttributeAbsent) {
		glEnableVertexAttribArray (color);
		glVertexAttribPointer (
		    color, 4, GL_FLOAT, GL_FALSE, stride,
		    reinterpret_cast<void*> (m_modelData->shapes[submeshIndex].colorOffset)
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
	    // Compensate only the renderer's output/orthographic Y conversions.
	    // Authored negative scales can intentionally turn a skybox inside out.
	    const auto& camera = this->getScene ().getCamera ();
	    const bool windingReversed = camera.isYFlipped () != camera.isOrthogonal ();
	    glFrontFace (windingReversed ? GL_CW : GL_CCW);
	    const auto& buffers = m_submeshes[submeshIndex];
	    const auto* shape = m_modelData ? &m_modelData->shapes[submeshIndex] : nullptr;
	    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, shape ? shape->indexBuffer : buffers.indexBuffer);
	    if (shape && shape->input.indexWidth == 0) {
		glDrawArrays (GL_TRIANGLES, 0, shape->drawCount);
	    } else {
		glDrawElements (GL_TRIANGLES, shape ? shape->drawCount : buffers.indexCount,
		    shape ? (shape->input.indexWidth == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT) : buffers.indexType, nullptr);
	    }
	    glFrontFace (GL_CCW);
	},
	[] () { }
    );
}

void CModel::syncAnimationLayers () const {
    const float time = getScene ().getTime ();
    for (const auto& layer : m_animationLayers) {
	if (layer->settings == nullptr) continue;
	const auto& settings = *layer->settings;
	const auto id = static_cast<uint32_t> (settings.animation->value->getInt ());
	if (layer->clip == nullptr || layer->clip->id != id) {
	    const auto& clips = m_model.animationData.animations;
	    const auto found = std::find_if (clips.begin (), clips.end (),
		[id] (const MdlAnimationClip& clip) { return clip.id == id; });
	    layer->clip = found == clips.end () ? nullptr : &*found;
	    if (layer->clip != nullptr) {
		layer->timeline.fps = layer->clip->fps;
		layer->timeline.length = static_cast<float> (layer->clip->frameCount);
		layer->timeline.mode = layer->clip->mode.empty () ? "loop" : layer->clip->mode;
		layer->timeline.events = layer->clip->events;
		layer->timeline.setFrame (0.0f, time);
	    } else {
		layer->timeline.events.clear ();
	    }
	    m_poseTime = std::numeric_limits<float>::quiet_NaN ();
	}
	const float rate = settings.rate->value->getFloat ();
	if (rate != layer->timeline.rate) layer->timeline.setRate (rate, time);
    }
}

void CModel::prepareAnimationEvents () {
    syncAnimationLayers ();
    const float time = getScene ().getTime ();
    // Keep a completed clip until its last event interval has been dispatched.
    // Retire only here, never from a callback while ScriptEngine iterates events.
    std::erase_if (m_animationLayers, [&] (const auto& layer) {
	if (!layer->single || layer->timeline.elapsedFrame (time) < layer->timeline.length
	    || layer->timeline.previousEventFrame < layer->timeline.length) return false;
	unregisterAnimation (layer->key);
	return true;
    });
}

CModel::AnimationLayer* CModel::findAnimationLayer (const std::string& key) {
    for (const auto& layer : m_animationLayers) if (layer->key == key) return layer.get ();
    return nullptr;
}

CModel::AnimationLayer* CModel::getAnimationLayer (const std::string& name) {
    for (const auto& layer : m_animationLayers) if (layer->timeline.name == name) return layer.get ();
    return nullptr;
}

CModel::AnimationLayer* CModel::getAnimationLayer (const size_t index) {
    return index < m_animationLayers.size () ? m_animationLayers[index].get () : nullptr;
}

size_t CModel::getAnimationLayerCount () const { return m_animationLayers.size (); }

CModel::AnimationLayer* CModel::playSingleAnimation (const std::string& name, const SingleAnimationConfig& config) {
    const auto& clips = m_model.animationData.animations;
    const auto found = std::find_if (clips.begin (), clips.end (),
	[&] (const MdlAnimationClip& clip) { return clip.name == name; });
    if (found == clips.end () || found->fps <= 0.0f || found->frameCount == 0) return nullptr;
    auto layer = std::make_unique<AnimationLayer> ();
    layer->clip = &*found;
    layer->single = true;
    layer->blendIn = config.blendIn;
    // Native one-shots of loop clips keep their authored return pose until
    // retirement; fading them early exposes a paused idle before its reset event.
    layer->blendOut = config.blendOut && found->mode == "single";
    layer->blendTime = std::max (config.blendTime, 0.0f);
    layer->key = "__model_animation_" + std::to_string (m_nextAnimationLayer++);
    layer->timeline.fps = found->fps;
    layer->timeline.length = static_cast<float> (found->frameCount);
    layer->timeline.mode = "single";
    layer->timeline.name = found->name;
    layer->timeline.events = found->events;
    layer->timeline.setFrame (0.0f, getScene ().getTime ());
    registerAnimation (layer->key, layer->timeline);
    auto* result = layer.get ();
    // Native one-shot layers blend over the base animation, before additive detail.
    const auto position = std::find_if (m_animationLayers.begin (), m_animationLayers.end (), [] (const auto& other) {
	return other->settings != nullptr && other->settings->additive->value->getBool ();
    });
    m_animationLayers.insert (position, std::move (layer));
    m_poseTime = std::numeric_limits<float>::quiet_NaN ();
    return result;
}

void CModel::animationControlsChanged (PropertyAnimation& animation) {
    for (const auto& layer : m_animationLayers) {
	if (&layer->timeline == &animation && layer->settings != nullptr
	    && layer->settings->rate->value->getFloat () != animation.rate) {
	    layer->settings->rate->value->update (animation.rate, DynamicValue::UpdateSource::User);
	}
    }
    m_poseTime = std::numeric_limits<float>::quiet_NaN ();
}

void CModel::updateAnimationPose () const {
    const auto& animationData = this->m_model.animationData;
    if (animationData.bones.empty ()) {
	return;
    }

    std::vector<MdlActiveAnimation> activeAnimations;
    const float sceneTime = this->getScene ().getTime ();
    syncAnimationLayers ();
    if (sceneTime == this->m_poseTime) {
	return;
    }

    for (const auto& layer : m_animationLayers) {
	if (layer->clip == nullptr) continue;
	const auto* settings = layer->settings;
	const bool visible = settings != nullptr ? settings->visible->value->getBool () : layer->visible;
	float weight = visible ? std::clamp (settings != nullptr ? settings->blend->value->getFloat () : layer->blend,
	    0.0f, 1.0f) : 0.0f;
	if (layer->single) {
	    const float elapsed = layer->timeline.elapsedFrame (sceneTime) / layer->timeline.fps;
	    const float duration = layer->timeline.length / layer->timeline.fps;
	    if (elapsed >= duration) continue;
	    if (layer->blendTime > 0.0f) {
		if (layer->blendIn) weight *= std::clamp (elapsed / layer->blendTime, 0.0f, 1.0f);
		if (layer->blendOut) weight *= std::clamp ((duration - elapsed) / layer->blendTime, 0.0f, 1.0f);
	    }
        }
	activeAnimations.push_back ({ .animation = layer->clip, .weight = weight,
	    .additive = settings != nullptr && settings->additive->value->getBool (),
	    .frame = layer->timeline.frameAt (sceneTime) });
    }

    if (m_morphingEnabled) {
        auto morphs = MdlAnimationEvaluator::evaluateModelMorphs (activeAnimations);
        if (!std::equal (morphs.begin (), morphs.end (), m_modelMorphs.begin (), m_modelMorphs.end (),
            [] (const auto& next, const auto& previous) { return next.mask == previous.mask && next.weights == previous.weights; })) {
            m_modelMorphs = std::move (morphs);
            m_morphDirty = true;
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

void CModel::updateMorphBuffers () {
    if (!m_morphDirty) return;
    m_morphDirty = false;
    m_morphActive = false;
    GLint previous = 0;
    glGetIntegerv (GL_ARRAY_BUFFER_BINDING, &previous);
    for (size_t index = 0; index < m_submeshes.size (); ++index) {
        auto& buffers = m_submeshes[index];
        const auto& body = m_model.mesh.submeshes[index];
        if (!std::ranges::all_of (m_model.materials[index]->passes,
            [] (const auto& pass) { return supportsModelMorphShader (pass->shader); })) continue;
        const bool wasMorphed = !buffers.morphVertices.empty ();
        const bool morphed = index < m_modelMorphs.size () && deformModelMorphVertices (
            m_model.mesh, body, m_modelMorphs[index].weights, m_modelMorphs[index].mask, buffers.morphVertices
        );
        m_morphActive |= morphed;
        if (!morphed && !wasMorphed) continue;
        const auto& vertices = morphed ? buffers.morphVertices : body.vertices;
        glBindBuffer (GL_ARRAY_BUFFER, buffers.vertexBuffer);
        glBufferSubData (GL_ARRAY_BUFFER, 0, vertices.size () * sizeof (float), vertices.data ());
        if (!morphed) buffers.morphVertices.clear ();
    }
    glBindBuffer (GL_ARRAY_BUFFER, static_cast<GLuint> (previous));
}

glm::mat4 CModel::resolveModelMatrix () const {
    glm::mat4 model = this->resolveWorldMatrix ();
    const auto& camera = this->getScene ().getCamera ();

    if (camera.isOrthogonal ()) {
	// Orthographic layer origins are authored from the canvas's top-left in
	// Y-down coordinates. The camera, however, is centered and Y-up. Image and
	// particle layers already perform this conversion; model layers need the
	// same basis change or the canvas center lands at the bottom-right and the
	// mesh is vertically mirrored.
	const glm::mat4 flipY = glm::scale (glm::mat4 (1.0f), glm::vec3 (1.0f, -1.0f, 1.0f));
	const glm::mat4 sceneToRender = glm::translate (
	    glm::mat4 (1.0f), glm::vec3 (-camera.getWidth () * 0.5f, camera.getHeight () * 0.5f, 0.0f)
	) * flipY;
	// The root offset is already in render coordinates. Prepend it so a
	// parent's rotation or scale does not change the scene-space drift.
	model = glm::translate (glm::mat4 (1.0f), glm::vec3 (this->resolveParallaxOffset (), 0.0f))
	    * sceneToRender * model;
    }

    return model;
}

void CModel::updateMatrices () {
    const auto& camera = this->getScene ().getCamera ();

    this->m_modelMatrix = this->resolveModelMatrix ();
    this->m_viewProjectionMatrix = camera.getRasterProjection () * camera.getLookAt ();
    const bool perspective = camera.isOrthogonal () && this->m_model.perspective->value->getBool ();
    if (perspective) {
	// Native derives the virtual eye from the current orthographic scale,
	// preserving the size of geometry on Z=0 while giving depth perspective.
	const float fov = glm::radians (glm::clamp (camera.getFov (), 0.1f, 179.9f));
	const float distance = 1.0f / (std::tan (fov * 0.5f) * camera.getProjection ()[1][1]);
	glm::mat4 view = camera.getLookAt ();
	view[3][2] = -distance;
	const auto& output = this->getScene ().getOutputSize ();
	const glm::mat4 projection = glm::perspective (fov, output.x / output.y, 5.0f,
	    glm::max (15000.0f, distance + 1000.0f));
	this->m_viewProjectionMatrix = projection * view;
	this->m_eyePosition = glm::vec3 (glm::inverse (view)[3]);
    }
    this->m_modelViewProjectionMatrix = this->m_viewProjectionMatrix * this->m_modelMatrix;
    this->m_modelViewProjectionMatrixInverse = glm::inverse (this->m_modelViewProjectionMatrix);
    this->m_normalMatrix = glm::inverseTranspose (glm::mat3 (this->m_modelMatrix));
    if (!perspective && camera.isOrthogonal ()) {
	const float depth = glm::max (glm::abs (camera.getNearZ ()), glm::abs (camera.getFarZ ()));
	this->m_eyePosition = glm::vec3 (0.0f, 0.0f, depth);
    } else if (!perspective) {
	this->m_eyePosition = camera.getEye ();
    }

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

const std::vector<RenderSortClass>& CModel::getRenderSortClasses () const { return this->m_renderSortClasses; }

void CModel::render () { this->renderPasses (std::nullopt); }

void CModel::render (const RenderSortClass renderClass) { this->renderPasses (renderClass); }

void CModel::prepareRender () {
    // Publish script-controlled poses after callbacks, including hidden attachment sources.
    this->updateAnimationPose ();
    // Material sorting may prepare an attached model before its parent.
    const Object* current = &this->m_model;
    for (int depth = 0; current->parent.has_value () && depth <= 32; ++depth) {
        const auto* parent = this->getScene ().getObject (*current->parent);
        if (parent == nullptr) break;
        if (const auto* model = dynamic_cast<const CModel*> (parent)) model->updateAnimationPose ();
        current = &parent->getObject ();
    }
    // A hidden container hides its whole subtree.
    this->m_frameVisible = this->m_initialized && this->m_model.groupVisible->value->getBool ()
	&& this->isVisibleThroughParents ();
    if (!this->m_frameVisible) return;
    this->updateMatrices ();
    this->updateMorphBuffers ();
    this->m_posedShadowBounds = this->m_shadowBounds.evaluate (this->m_gpuSkinBones);
    // Rest-pose bounds cannot reject geometry displaced by an active morph.
    if (m_morphActive) m_posedShadowBounds = {};
    if (m_modelData && m_modelData->mesh.hasBoundingBox) {
	m_posedShadowBounds.minimum = m_modelData->mesh.boundingBoxMin;
	m_posedShadowBounds.maximum = m_modelData->mesh.boundingBoxMax;
    }
}

void CModel::renderPasses (const std::optional<RenderSortClass> renderClass) {
    if (!this->m_frameVisible) return;

#if !NDEBUG
    const std::string debugName
	= "Model " + this->m_model.name + " (" + std::to_string (this->getId ()) + ", " + this->m_model.filename + ")";

    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, debugName.c_str ());
#endif /* DEBUG */

    // Scene alpha belongs to the compositor; isolated composition targets need
    // the model's authored alpha, just like image and particle children.
    glColorMask (true, true, true, this->getScene ().isRenderingToComposition () ? GL_TRUE : GL_FALSE);

    for (const auto& pass : this->m_passes) {
	if (!renderClass.has_value () || passRenderClass (pass->getBlendingMode ()) == *renderClass) {
	    pass->render ();
	}
    }

    glColorMask (true, true, true, true);

#if !NDEBUG
    glPopDebugGroup ();
#endif /* DEBUG */
}

void CModel::renderShadow (const glm::mat4& lightViewProjection) {
    if (!this->m_frameVisible || !this->m_shadowProgram
	|| !this->m_model.castShadow->value->getBool ()
	|| !this->m_posedShadowBounds.intersects (lightViewProjection * this->m_modelMatrix)) {
	return;
    }

    // Every depth draw uploads its full layout, so the query program is safe as a fallback.
    glUseProgram (this->m_sharedShadowProgram ? this->m_sharedShadowProgram->id : this->m_shadowProgram->id);
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

    for (size_t submeshIndex = 0; submeshIndex < this->m_submeshes.size (); submeshIndex++) {
	auto& buffers = this->m_submeshes[submeshIndex];
	const auto& layout = m_modelData ? m_modelData->shapes[submeshIndex].layout : m_model.mesh;
	const auto stride = static_cast<GLsizei> (layout.strideBytes);
	if (!buffers.castsShadow || (m_modelData ? m_modelData->shapes[submeshIndex].drawCount == 0
	    : buffers.indexCount == 0 && buffers.vertexCount == 0)
	    || layout.positionOffset == MdlMesh::AttributeAbsent) {
	    continue;
	}
	if (buffers.shadowCulling) {
	    glEnable (GL_CULL_FACE);
	} else {
	    glDisable (GL_CULL_FACE);
	}
	// Shadows have no output flip; retain authored winding changes here too.
	glFrontFace (this->getScene ().getCamera ().isOrthogonal () ? GL_CW : GL_CCW);

	if (buffers.shadowVao == GL_NONE || buffers.shadowVertexTag != layout.vertexTag) {
	    if (buffers.shadowVao == GL_NONE) {
		glGenVertexArrays (1, &buffers.shadowVao);
	    }
	    buffers.shadowVertexTag = layout.vertexTag;
	    glBindVertexArray (buffers.shadowVao);
	    glBindBuffer (GL_ARRAY_BUFFER, buffers.vertexBuffer);
	    glEnableVertexAttribArray (0);
	    glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*> (layout.positionOffset));
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
	    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, m_modelData ? m_modelData->shapes[submeshIndex].indexBuffer : buffers.indexBuffer);
	}
	glBindVertexArray (buffers.shadowVao);
	if (m_modelData) {
	    const auto& shape = m_modelData->shapes[submeshIndex];
	    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, shape.indexBuffer);
	    if (shape.input.indexWidth == 0) {
		glDrawArrays (GL_TRIANGLES, 0, shape.drawCount);
	    } else {
		glDrawElements (GL_TRIANGLES, shape.drawCount,
		    shape.input.indexWidth == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, nullptr);
	    }
	} else {
	    glDrawElements (GL_TRIANGLES, buffers.indexCount, buffers.indexType, nullptr);
	}
    }

    glFrontFace (GL_CCW);
}

const Model3D& CModel::getModel () const { return this->m_model; }

std::optional<glm::mat4> CModel::getAttachmentTransform (const std::string& name) const {
    // Native attachment queries retain the evaluated pose until the next scene update.
    // Keep lazy initialization for models queried before setup has evaluated their bones.
    if (this->m_worldBones.empty ()) this->updateAnimationPose ();
    return MdlAnimationEvaluator::attachmentTransform (this->m_model.animationData, this->m_worldBones, name);
}

std::optional<size_t> CModel::getAttachmentIndex (const std::string& name) const {
    size_t index = 0;
    for (const auto& attachmentName : this->m_model.animationData.attachments | std::views::keys) {
	if (attachmentName == name) {
	    return index;
	}
	index++;
    }
    return std::nullopt;
}

std::optional<std::string> CModel::getAttachmentName (const size_t requestedIndex) const {
    size_t index = 0;
    for (const auto& attachmentName : this->m_model.animationData.attachments | std::views::keys) {
	if (index++ == requestedIndex) {
	    return attachmentName;
	}
    }
    return std::nullopt;
}

std::optional<size_t> CModel::getBoneIndex (const std::string& name) const {
    if (name.empty ()) return std::nullopt;
    for (size_t index = 0; index < this->m_model.animationData.bones.size (); ++index) {
	if (this->m_model.animationData.bones[index].name == name) return index;
    }
    return std::nullopt;
}

std::optional<glm::mat4> CModel::getBoneTransform (const size_t index) const {
    if (this->m_worldBones.empty ()) this->updateAnimationPose ();
    if (index >= this->m_worldBones.size ()) return std::nullopt;
    return this->m_worldBones[index];
}

const float& CModel::getBrightness () const { return this->m_brightness; }

const float& CModel::getUserAlpha () const { return this->m_alpha; }

const float& CModel::getAlpha () const { return this->m_alpha; }

const glm::vec3& CModel::getColor () const { return this->m_color; }

const glm::vec4& CModel::getColor4 () const { return this->m_color4; }

const glm::vec3& CModel::getCompositeColor () const { return this->m_color; }
