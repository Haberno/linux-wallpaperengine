#include "CImage.h"

#include "CRenderable.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <ranges>
#include <sstream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/rotate_vector.hpp>
#undef GLM_ENABLE_EXPERIMENTAL

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Model/UserSetting.h"
#include "WallpaperEngine/Data/Parsers/MaterialParser.h"
#include "WallpaperEngine/Data/Parsers/MdlAnimationParser.h"
#include "WallpaperEngine/Data/Utils/BinaryReader.h"
#include "WallpaperEngine/Data/Utils/MemoryStream.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render::Objects;
using namespace WallpaperEngine::Render::Objects::Effects;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Builders;
using namespace WallpaperEngine::Data::Utils;

namespace {
glm::vec2 rotateVec2 (const glm::vec2& value, float angle) {
    const float cosAngle = std::cos (angle);
    const float sinAngle = std::sin (angle);
    return { value.x * cosAngle - value.y * sinAngle, value.x * sinAngle + value.y * cosAngle };
}

// rotate a scene-space vector by unflipped scene angles (z, then y, then x applied to the
// vector) — reduces to rotateVec2 when only z is set, which keeps z-only parent chains
// byte-identical to the previous behaviour
glm::vec3 rotateVec3 (const glm::vec3& value, const glm::vec3& angles) {
    if (angles.x == 0.0f && angles.y == 0.0f) {
	const glm::vec2 rotated = rotateVec2 ({ value.x, value.y }, angles.z);
	return { rotated.x, rotated.y, value.z };
    }

    glm::mat4 rotation = glm::mat4 (1.0f);
    rotation = glm::rotate (rotation, angles.z, glm::vec3 (0.0f, 0.0f, 1.0f));
    rotation = glm::rotate (rotation, angles.y, glm::vec3 (0.0f, 1.0f, 0.0f));
    rotation = glm::rotate (rotation, angles.x, glm::vec3 (1.0f, 0.0f, 0.0f));
    return { rotation * glm::vec4 (value, 0.0f) };
}

bool isMagentaNeonTint (const glm::vec3& color) { return color.r > 0.55f && color.g < 0.25f && color.b > 0.45f; }

std::optional<glm::vec3> findMagentaCompositeTint (const Image& image, const std::vector<int>& skippedEffectIds) {
    for (const auto& effect : image.effects) {
	if (std::find (skippedEffectIds.begin (), skippedEffectIds.end (), static_cast<int> (effect->id))
	    != skippedEffectIds.end ()) {
	    continue;
	}
	if (!effect->visible->value->getBool ()) {
	    continue;
	}

	for (const auto& passOverride : effect->passOverrides) {
	    const auto compositeCombo = passOverride->combos.find ("COMPOSITE");
	    if (compositeCombo == passOverride->combos.end () || compositeCombo->second != 2) {
		continue;
	    }

	    const auto compositeColor = passOverride->constants.find ("compositecolor");
	    if (compositeColor == passOverride->constants.end () || compositeColor->second == nullptr
		|| compositeColor->second->value == nullptr) {
		continue;
	    }

	    const auto tint = compositeColor->second->value->getVec3 ();
	    if (isMagentaNeonTint (tint)) {
		return tint;
	    }
	}
    }

    return std::nullopt;
}

}

CImage::ResolvedTransform CImage::localTransform (const Object& object, float time) {
    // Keyframed transform properties are sampled as a unit. Wallpaper Engine's
    // generic property parser allows animation on origin, scale, and angles.
    const glm::vec3 origin = object.origin->evaluateVec3 (time);
    glm::vec3 scale = glm::vec3 (1.0f);
    glm::vec3 angles = glm::vec3 (0.0f);

    if (object.is<Image> ()) {
	const auto* image = object.as<Image> ();
	scale = image->scale->evaluateVec3 (time);
	angles = image->angles->evaluateVec3 (time);
    } else if (object.is<Text> ()) {
	const auto* text = object.as<Text> ();
	scale = text->scale->evaluateVec3 (time);
	angles = object.groupAngles->evaluateVec3 (time);
    } else {
	scale = object.groupScale->evaluateVec3 (time);
	angles = object.groupAngles->evaluateVec3 (time);
    }

    return { origin, scale, angles };
}

CImage::ResolvedTransform CImage::resolveTransform (const Object& object) const {
    constexpr int kMaxParentDepth = 32;

    // Walk up the parent chain leaf-first, bounded by kMaxParentDepth to guard
    // against cycles. chain[0] is the requested object; the last entry is the root.
    const Object* chain[kMaxParentDepth + 1];
    int count = 0;
    const Object* current = &object;
    chain[count++] = current;

    while (current->parent.has_value ()) {
	if (count > kMaxParentDepth) {
	    sLog.error ("Parent transform chain is too deep; possible cycle at object id=", current->id);
	    break;
	}
	const auto* parentObject = this->getScene ().getObject (current->parent.value ());
	if (parentObject == nullptr) {
	    break;
	}
	current = &parentObject->getObject ();
	chain[count++] = current;
    }

    // Accumulate top-down: the root's local transform is already its resolved
    // transform, then fold each child onto its already-resolved parent.
    const float time = this->getScene ().getTime ();
    const auto compose = [] (const ResolvedTransform& parent, const ResolvedTransform& local) {
	const glm::vec3 offset = rotateVec3 (
	    { local.origin.x * parent.scale.x, local.origin.y * parent.scale.y, local.origin.z * parent.scale.z },
	    parent.angles
	);
	return ResolvedTransform {
	    .origin = parent.origin + offset,
	    .scale = local.scale * parent.scale,
	    .angles = local.angles + parent.angles,
	};
    };

    ResolvedTransform resolved = localTransform (*chain[count - 1], time);
    for (int i = count - 2; i >= 0; --i) {
	const Object& child = *chain[i];
	if (child.attachment.has_value () && child.parent.has_value ()) {
	    const auto* parentObject = this->getScene ().getObject (child.parent.value ());
	    const auto* parentImage = dynamic_cast<const CImage*> (parentObject);
	    if (parentImage != nullptr) {
		const auto attachment = parentImage->puppetAttachmentTransform (*child.attachment);
		if (attachment.has_value ()) {
		    resolved = compose (resolved, *attachment);
		}
	    }
	}

	resolved = compose (resolved, localTransform (child, time));
    }

    return resolved;
}

CImage::CImage (Wallpapers::CScene& scene, const Image& image) :
    CObject (scene, image), CRenderable (scene, image, *image.model->material), ScriptableObject (scene, image),
    m_sceneSpacePosition (GL_NONE), m_copySpacePosition (GL_NONE), m_passSpacePosition (GL_NONE),
    m_texcoordCopy (GL_NONE), m_texcoordPass (GL_NONE), m_modelViewProjectionScreen (),
    m_modelViewProjectionPass (glm::mat4 (1.0)), m_modelViewProjectionCopy (), m_modelViewProjectionScreenInverse (),
    m_modelViewProjectionPassInverse (glm::inverse (m_modelViewProjectionPass)), m_modelViewProjectionCopyInverse (),
    m_modelMatrix (), m_viewProjectionMatrix (), m_image (image), m_resolvedAlpha (image.alpha->value->getFloat ()),
    m_pos (), m_initialized (false) {
    // register any properties in use on this object
    this->registerProperty ("origin", *image.origin->value);
    this->registerProperty ("scale", *image.scale->value);
    this->registerProperty ("angles", *image.angles->value);
    this->registerProperty ("visible", *image.visible->value);
    this->registerProperty ("alpha", *image.alpha->value);
    this->registerProperty ("color", *image.color->value);
    this->registerProperty ("parallaxDepth", *image.parallaxDepth->value);

    // get scene width and height to calculate positions
    auto scene_width = static_cast<float> (scene.getWidth ());
    auto scene_height = static_cast<float> (scene.getHeight ());

    const auto transform = this->resolveTransform (this->getImage ());
    glm::vec3 origin = transform.origin;
    glm::vec2 size = this->getSize ();
    glm::vec3 scale = transform.scale;

    // Native composition layers have two authored forms. A layer with children
    // owns an isolated subtree surface; a childless layer is a full-frame stack
    // effect and must keep sampling the shared _rt_FullFrameBuffer. Unconditionally
    // shadowing that target leaves childless water/ripple layers with an empty input.
    if (this->isCompositionLayer () && scene.hasAuthoredChildren (image.id)) {
	const glm::vec2 compositionSize = {
	    static_cast<float> (scene.getWidth ()), static_cast<float> (scene.getHeight ())
	};
	this->m_compositionFBO = scene.create (
	    "_rt_compositionLayer_" + std::to_string (image.id), TextureFormat_ARGB8888,
	    TextureFlags_ClampUVs, 1.0f, compositionSize, compositionSize, true
	);
	// Every pass created for this image receives an FBOProvider parented to the
	// image, so this alias also redirects an authored texture slot named
	// _rt_FullFrameBuffer, not only CRenderable's primary input.
	this->alias ("_rt_FullFrameBuffer", this->m_compositionFBO);
    }

    this->detectTexture ();

    // detect texture (if any)
    if (this->m_texture == nullptr) {
	if (this->m_image.model->solidlayer && size.x == 0.0f && size.y == 0.0f) {
	    size.x = scene_width;
	    size.y = scene_height;
	}
	// if (this->m_image->isSolid ()) // layer receives cursor events:
	// https://docs.wallpaperengine.io/en/scene/scenescript/reference/event/cursor.html same applies to effects
	// TODO: create a dummy texture of correct size, fbo constructors should be enough, but this should be properly
	// handled
	this->m_texture = std::make_shared<CFBO> (
	    "", TextureFormat_ARGB8888, TextureFlags_NoFlags, 1, size.x, size.y, size.x, size.y
	);
    }

    // If the wallpaper doesn't specify a size, fall back to the texture or model dimensions
    if ((size.x == 0.0f || size.y == 0.0f) && this->m_texture != nullptr) {
	size.x = static_cast<float> (this->m_texture->getRealWidth ());
	size.y = static_cast<float> (this->m_texture->getRealHeight ());
    } else if (
	(size.x == 0.0f || size.y == 0.0f) && this->getImage ().model->width.has_value ()
	&& this->getImage ().model->height.has_value ()
    ) {
	size.x = static_cast<float> (this->getImage ().model->width.value ());
	size.y = static_cast<float> (this->getImage ().model->height.value ());
    }

    // fullscreen layers should use the whole projection's size
    // TODO: WHAT SHOULD AUTOSIZE DO?
    if (this->getImage ().model->fullscreen) {
	size = { scene_width, scene_height };
	origin = { scene_width / 2, scene_height / 2, 0 };

	// TODO: CHANGE ALIGNMENT TOO?
    }
    this->m_size = size;

    glm::vec2 scaledSize = size * glm::vec2 (scale);

    // calculate the center and shift from there
    this->m_pos.x = origin.x - (scaledSize.x / 2);
    this->m_pos.w = origin.y + (scaledSize.y / 2);
    this->m_pos.z = origin.x + (scaledSize.x / 2);
    this->m_pos.y = origin.y - (scaledSize.y / 2);

    if (this->getImage ().alignment.find ("top") != std::string::npos) {
	this->m_pos.y -= scaledSize.y / 2;
	this->m_pos.w -= scaledSize.y / 2;
    } else if (this->getImage ().alignment.find ("bottom") != std::string::npos) {
	this->m_pos.y += scaledSize.y / 2;
	this->m_pos.w += scaledSize.y / 2;
    }

    if (this->getImage ().alignment.find ("left") != std::string::npos) {
	this->m_pos.x += scaledSize.x / 2;
	this->m_pos.z += scaledSize.x / 2;
    } else if (this->getImage ().alignment.find ("right") != std::string::npos) {
	this->m_pos.x -= scaledSize.x / 2;
	this->m_pos.z -= scaledSize.x / 2;
    }

    // wallpaper engine
    this->m_pos.x -= scene_width / 2;
    this->m_pos.y = scene_height / 2 - this->m_pos.y;
    this->m_pos.z -= scene_width / 2;
    this->m_pos.w = scene_height / 2 - this->m_pos.w;

    // register both FBOs into the scene
    std::ostringstream nameA, nameB;

    // TODO: determine when _rt_imageLayerComposite and _rt_imageLayerAlbedo is used
    nameA << "_rt_imageLayerComposite_" << this->getImage ().id << "_a";
    nameB << "_rt_imageLayerComposite_" << this->getImage ().id << "_b";

    this->m_currentMainFBO = this->m_mainFBO = scene.create (
	nameA.str (), TextureFormat_ARGB8888, this->m_texture->getFlags (), 1, { size.x, size.y }, { size.x, size.y }
    );
    this->m_currentSubFBO = this->m_subFBO = scene.create (
	nameB.str (), TextureFormat_ARGB8888, this->m_texture->getFlags (), 1, { size.x, size.y }, { size.x, size.y }
    );

    // build a list of vertices, these might need some change later (or maybe invert the camera)
    GLfloat sceneSpacePosition[] = { this->m_pos.x, this->m_pos.y, 0.0f, this->m_pos.x, this->m_pos.w, 0.0f,
				     this->m_pos.z, this->m_pos.y, 0.0f, this->m_pos.z, this->m_pos.y, 0.0f,
				     this->m_pos.x, this->m_pos.w, 0.0f, this->m_pos.z, this->m_pos.w, 0.0f };

    float width = 1.0f;
    float height = 1.0f;

    if (this->getTexture ()->isAnimated ()) {
	// animated images use different coordinates as they're essentially a texture atlas
	width = static_cast<float> (this->getTexture ()->getRealWidth ())
	    / static_cast<float> (this->getTexture ()->getTextureWidth (0));
	height = static_cast<float> (this->getTexture ()->getRealHeight ())
	    / static_cast<float> (this->getTexture ()->getTextureHeight (0));
    }
    // calculate the correct texCoord limits for the texture based on the texture screen size and real size
    else if (
	this->getTexture () != nullptr
	&& (this->getTexture ()->getTextureWidth (0) != this->getTexture ()->getRealWidth ()
	    || this->getTexture ()->getTextureHeight (0) != this->getTexture ()->getRealHeight ())
    ) {
	// Account for padding in non-power-of-two textures: clamp UVs to the real content
	width = static_cast<float> (this->getTexture ()->getRealWidth ())
	    / static_cast<float> (this->getTexture ()->getTextureWidth (0));
	height = static_cast<float> (this->getTexture ()->getRealHeight ())
	    / static_cast<float> (this->getTexture ()->getTextureHeight (0));
    }

    // TODO: RECALCULATE THESE POSITIONS FOR PASSTHROUGH SO THEY TAKE THE RIGHT PART OF THE TEXTURE
    float x = 0.0f;
    float y = 0.0f;

    if (this->getTexture ()->isAnimated ()) {
	// animations should be copied completely
	x = 0.0f;
	y = 0.0f;
	width = 1.0f;
	height = 1.0f;
    }

    GLfloat realWidth = size.x;
    GLfloat realHeight = size.y;
    GLfloat realX = 0.0;
    GLfloat realY = 0.0;

    if (this->getImage ().model->passthrough) {
	// Passthrough shaders fill the destination FBO from texcoords and sample the scene using positions.
	// Keep the destination quad full-screen in local FBO space, but pass scene-space positions through.
	x = 0.0f;
	y = 0.0f;
	width = 1.0f;
	height = 1.0f;
	realX = this->m_pos.x;
	realY = this->m_pos.w;
	realWidth = this->m_pos.z;
	realHeight = this->m_pos.y;

	if (this->getImage ().model->fullscreen) {
	    realX = -1.0;
	    realY = -1.0;
	    realWidth = 1.0;
	    realHeight = 1.0;
	}
    }

    GLfloat texcoordCopy[] = { x, height, x, y, width, height, width, height, x, y, width, y };

    GLfloat copySpacePosition[] = { realX,     realHeight, 0.0f, realX, realY, 0.0f, realWidth, realHeight, 0.0f,
				    realWidth, realHeight, 0.0f, realX, realY, 0.0f, realWidth, realY,      0.0f };

    GLfloat texcoordPass[] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };

    GLfloat passSpacePosition[]
	= { -1.0, 1.0, 0.0f, -1.0, -1.0, 0.0f, 1.0, 1.0, 0.0f, 1.0, 1.0, 0.0f, -1.0, -1.0, 0.0f, 1.0, -1.0, 0.0f };

    // bind vertex list to the openGL buffers
    glGenBuffers (1, &this->m_sceneSpacePosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_sceneSpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (sceneSpacePosition), sceneSpacePosition, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_copySpacePosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_copySpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (copySpacePosition), copySpacePosition, GL_STATIC_DRAW);

    // bind pass' vertex list to the openGL buffers
    glGenBuffers (1, &this->m_passSpacePosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_passSpacePosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (passSpacePosition), passSpacePosition, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_texcoordCopy);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texcoordCopy);
    glBufferData (GL_ARRAY_BUFFER, sizeof (texcoordCopy), texcoordCopy, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_texcoordPass);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texcoordPass);
    glBufferData (GL_ARRAY_BUFFER, sizeof (texcoordPass), texcoordPass, GL_STATIC_DRAW);

    this->m_hasPuppetMesh = this->loadPuppetMesh (size);

    // compute the center of the image in scene space for rotation
    this->m_sceneCenter
	= glm::vec3 ((this->m_pos.x + this->m_pos.z) / 2.0f, (this->m_pos.y + this->m_pos.w) / 2.0f, 0.0f);

    this->m_modelViewProjectionScreen
	= this->getScene ().getCamera ().getProjection () * this->getScene ().getCamera ().getLookAt ();

    if (this->getImage ().model->passthrough) {
	this->m_modelViewProjectionCopy = this->m_modelViewProjectionScreen;
    } else {
	this->m_modelViewProjectionCopy = glm::ortho<float> (0.0, size.x, 0.0, size.y);
    }
    this->m_modelViewProjectionCopyInverse = glm::inverse (this->m_modelViewProjectionCopy);
    this->m_modelMatrix = glm::ortho<float> (0.0, size.x, 0.0, size.y);
    this->m_viewProjectionMatrix = glm::mat4 (1.0);

    // ensure the input texture is marked as used
    // this makes video playback start if it's not already
    this->m_texture->incrementUsageCount ();
}

CImage::~CImage () {
    this->m_texture->decrementUsageCount ();

    // delete passes first as they depend on the image's data
    for (auto* pass : this->m_passes) {
	delete pass;
    }

    this->m_passes.clear ();
    delete this->m_puppetAlbedoCopyPass;
    delete this->m_puppetOverlayPass;
    for (auto* pass : this->m_puppetClippingPasses) {
	delete pass;
    }
    this->m_puppetClippingPasses.clear ();

    // free any gl resources
    glDeleteBuffers (1, &this->m_sceneSpacePosition);
    glDeleteBuffers (1, &this->m_copySpacePosition);
    glDeleteBuffers (1, &this->m_passSpacePosition);
    glDeleteBuffers (1, &this->m_texcoordCopy);
    glDeleteBuffers (1, &this->m_texcoordPass);
    if (this->m_puppetSpacePosition != GL_NONE) {
	glDeleteBuffers (1, &this->m_puppetSpacePosition);
    }
    if (this->m_puppetTexCoordFirstPass != GL_NONE && this->m_puppetTexCoordFirstPass != this->m_puppetTexCoord) {
	glDeleteBuffers (1, &this->m_puppetTexCoordFirstPass);
    }
    if (this->m_puppetTexCoord != GL_NONE) {
	glDeleteBuffers (1, &this->m_puppetTexCoord);
    }
    if (this->m_puppetIndices != GL_NONE) {
	glDeleteBuffers (1, &this->m_puppetIndices);
    }
    for (GLuint* buffer : { &this->m_puppetOverlayPosition, &this->m_puppetOverlayTexCoord,
			    &this->m_puppetOverlayBlendIndices, &this->m_puppetOverlayIndices }) {
	if (*buffer != GL_NONE) {
	    glDeleteBuffers (1, buffer);
	}
    }
}

bool CImage::loadPuppetMesh (const glm::vec2& size) {
    if (!this->getImage ().model->puppet.has_value () || !this->getImage ().model->puppetMesh.has_value ()) {
	return false;
    }

    try {
	const auto& mesh = *this->getImage ().model->puppetMesh;
	if (mesh.submeshes.empty ()) {
	    throw std::runtime_error ("puppet model has no submeshes");
	}

	const auto& body = mesh.submeshes.front ();
	if (body.strideBytes == 0 || body.vertices.size () * sizeof (float) % body.strideBytes != 0
	    || body.blendIndicesOffset == MdlMesh::AttributeAbsent
	    || body.blendWeightsOffset == MdlMesh::AttributeAbsent || body.uvOffset == MdlMesh::AttributeAbsent) {
	    throw std::runtime_error ("puppet body does not use a supported skinned vertex layout");
	}

	const size_t vertexCount = body.vertices.size () * sizeof (float) / body.strideBytes;
	std::vector<GLfloat> texcoords;
	std::vector<GLushort> indices;

	this->m_puppetRawPositions.clear ();
	this->m_puppetRawPositions.reserve (vertexCount * 3);
	this->m_puppetBlendIndices.clear ();
	this->m_puppetBlendIndices.reserve (vertexCount * 4);
	this->m_puppetBlendWeights.clear ();
	this->m_puppetBlendWeights.reserve (vertexCount * 4);
	texcoords.reserve (vertexCount * 2);
	indices.reserve (body.indices.size ());

	for (size_t index = 0; index < vertexCount; index++) {
	    const auto* vertex
		= reinterpret_cast<const char*> (body.vertices.data ()) + static_cast<ptrdiff_t> (index * body.strideBytes);
	    glm::vec3 position;
	    std::memcpy (&position.x, vertex, sizeof (float));
	    std::memcpy (&position.y, vertex + sizeof (float), sizeof (float));
	    std::memcpy (&position.z, vertex + sizeof (float) * 2, sizeof (float));
	    if (!std::isfinite (position.x) || !std::isfinite (position.y) || !std::isfinite (position.z)) {
		throw std::runtime_error ("puppet vertex contains a non-finite position");
	    }
	    this->m_puppetRawPositions.insert (
		this->m_puppetRawPositions.end (), { position.x, position.y, position.z }
	    );
	    for (int component = 0; component < 4; component++) {
		uint32_t value;
		std::memcpy (
		    &value, vertex + body.blendIndicesOffset + component * sizeof (uint32_t), sizeof (value)
		);
		this->m_puppetBlendIndices.push_back (value);
	    }
	    for (int component = 0; component < 4; component++) {
		float weight;
		std::memcpy (&weight, vertex + body.blendWeightsOffset + component * sizeof (float), sizeof (weight));
		if (!std::isfinite (weight) || weight < 0.0f) {
		    throw std::runtime_error ("puppet vertex contains an invalid blend weight");
		}
		this->m_puppetBlendWeights.push_back (weight);
	    }
	    float u;
	    float v;
	    std::memcpy (&u, vertex + body.uvOffset, sizeof (u));
	    std::memcpy (&v, vertex + body.uvOffset + sizeof (float), sizeof (v));
	    if (!std::isfinite (u) || !std::isfinite (v)) {
		throw std::runtime_error ("puppet vertex contains a non-finite texture coordinate");
	    }
	    texcoords.push_back (u);
	    texcoords.push_back (v);
	}

	for (const auto value : body.indices) {
	    if (value >= vertexCount || value > std::numeric_limits<GLushort>::max ()) {
		sLog.error ("Invalid puppet mesh index ", value, " in ", *this->getImage ().model->puppet);
		return false;
	    }
	    indices.push_back (static_cast<GLushort> (value));
	}

	// MDLV vertices are already assembled even when their texture UVs point into a parts
	// atlas. The shared animation parser supplies MDLS bones, MDAT attachments, and MDLA clips.
	try {
	    this->m_puppetAnimation = MdlAnimationParser::load (
		this->getScene ().getScene ().project, *this->getImage ().model->puppet
	    );
	    const auto bindPose = MdlAnimationEvaluator::evaluate (this->m_puppetAnimation, {});
	    this->m_puppetWorldBones = bindPose.worldBones;
	    sLog.out (
		"Loaded puppet animation data ", *this->getImage ().model->puppet,
		" bones=", this->m_puppetAnimation.bones.size (),
		" attachments=", this->m_puppetAnimation.attachments.size (),
		" clips=", this->m_puppetAnimation.animations.size ()
	    );
	} catch (const std::exception& ex) {
	    sLog.error ("Could not load puppet animation data ", *this->getImage ().model->puppet, ": ", ex.what ());
	    this->m_puppetAnimation = {};
	    this->m_puppetWorldBones.clear ();
	}

	this->loadPuppetOverlay (mesh, size);
	this->updatePuppetPositionBuffer (size);

	glGenBuffers (1, &this->m_puppetTexCoord);
	glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetTexCoord);
	glBufferData (GL_ARRAY_BUFFER, texcoords.size () * sizeof (GLfloat), texcoords.data (), GL_STATIC_DRAW);

	// variant compensating for power-of-two padding, used when sampling the source texture directly
	glm::vec2 uvScale (1.0f);
	if (!this->getTexture ()->isAnimated ()
	    && (this->getTexture ()->getTextureWidth (0) != this->getTexture ()->getRealWidth ()
		|| this->getTexture ()->getTextureHeight (0) != this->getTexture ()->getRealHeight ())) {
	    uvScale.x = static_cast<float> (this->getTexture ()->getRealWidth ())
		/ static_cast<float> (this->getTexture ()->getTextureWidth (0));
	    uvScale.y = static_cast<float> (this->getTexture ()->getRealHeight ())
		/ static_cast<float> (this->getTexture ()->getTextureHeight (0));
	}

	if (uvScale == glm::vec2 (1.0f)) {
	    this->m_puppetTexCoordFirstPass = this->m_puppetTexCoord;
	} else {
	    std::vector<GLfloat> scaledTexcoords = texcoords;
	    for (size_t index = 0; index + 1 < scaledTexcoords.size (); index += 2) {
		scaledTexcoords[index] *= uvScale.x;
		scaledTexcoords[index + 1] *= uvScale.y;
	    }

	    glGenBuffers (1, &this->m_puppetTexCoordFirstPass);
	    glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetTexCoordFirstPass);
	    glBufferData (
		GL_ARRAY_BUFFER, scaledTexcoords.size () * sizeof (GLfloat), scaledTexcoords.data (), GL_STATIC_DRAW
	    );
	}

	glGenBuffers (1, &this->m_puppetIndices);
	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, this->m_puppetIndices);
	glBufferData (GL_ELEMENT_ARRAY_BUFFER, indices.size () * sizeof (GLushort), indices.data (), GL_STATIC_DRAW);

	this->m_puppetIndexCount = static_cast<GLsizei> (indices.size ());
	sLog.out (
	    "Loaded puppet mesh ", *this->getImage ().model->puppet, " version=", mesh.version, " format=0x", std::hex,
	    body.vertexTag, std::dec, " stride=", body.strideBytes, " vertices=", vertexCount,
	    " indices=", this->m_puppetIndexCount
	);

	return true;
    } catch (const std::exception& ex) {
	sLog.error ("Could not load puppet mesh ", *this->getImage ().model->puppet, ": ", ex.what ());
	return false;
    }
}

void CImage::loadPuppetOverlay (const MdlMesh& mesh, const glm::vec2& size) {
    // The overlay lives in a second submesh whose vertices carry a vec4 texcoord (the
    // channelmap atlas uv in xy) instead of the body's skin weights. The container parser
    // reads the whole file; the scanner above only ever finds the first mesh block.
    const MdlSubmesh* overlay = nullptr;
    for (const auto& submesh : mesh.submeshes) {
	if (submesh.texCoordVec4Offset != MdlMesh::AttributeAbsent && !submesh.indices.empty ()) {
	    overlay = &submesh;
	    break;
	}
    }

    if (overlay == nullptr) {
	return;
    }

    const size_t stride = overlay->strideBytes / sizeof (float);
    const size_t vertexCount = stride > 0 ? overlay->vertices.size () / stride : 0;
    if (vertexCount == 0) {
	return;
    }

    std::vector<GLfloat> texcoords;
    std::vector<GLuint> blendIndices;
    std::vector<GLushort> indices;
    std::vector<GLfloat> positions;
    positions.reserve (vertexCount * 3);
    texcoords.reserve (vertexCount * 4);
    blendIndices.reserve (vertexCount * 4);
    indices.reserve (overlay->indices.size ());

    // attribute offsets are in bytes, the vertices are stored as floats
    const size_t uvElement = overlay->texCoordVec4Offset / sizeof (float);
    const size_t blendElement = overlay->blendIndicesOffset != MdlMesh::AttributeAbsent
	? overlay->blendIndicesOffset / sizeof (float)
	: 0;

    for (size_t vertex = 0; vertex < vertexCount; vertex++) {
	const float* base = overlay->vertices.data () + vertex * stride;
	// Channel quads are authored in top-left-origin image pixels. They are texture
	// composition geometry, not skinned body geometry: flip Y into the local FBO's
	// bottom-left coordinate system and keep them entirely in albedo space.
	positions.push_back (base[0]);
	positions.push_back (size.y - base[1]);
	positions.push_back (base[2]);

	for (int component = 0; component < 4; component++) {
	    texcoords.push_back (base[uvElement + component]);
	}

	// the shader picks a g_BlendMap element with these, they are not skin bones
	for (int component = 0; component < 4; component++) {
	    uint32_t value = 0;
	    if (overlay->blendIndicesOffset != MdlMesh::AttributeAbsent) {
		std::memcpy (&value, base + blendElement + component, sizeof (value));
	    }
	    blendIndices.push_back (value);
	}
    }

    for (const auto index : overlay->indices) {
	if (index >= vertexCount) {
	    sLog.error ("Invalid puppet overlay index ", index, " in ", *this->getImage ().model->puppet);
	    return;
	}
	indices.push_back (static_cast<GLushort> (index));
    }

    glGenBuffers (1, &this->m_puppetOverlayPosition);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetOverlayPosition);
    glBufferData (GL_ARRAY_BUFFER, positions.size () * sizeof (GLfloat), positions.data (), GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_puppetOverlayTexCoord);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetOverlayTexCoord);
    glBufferData (GL_ARRAY_BUFFER, texcoords.size () * sizeof (GLfloat), texcoords.data (), GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_puppetOverlayBlendIndices);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetOverlayBlendIndices);
    glBufferData (GL_ARRAY_BUFFER, blendIndices.size () * sizeof (GLuint), blendIndices.data (), GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_puppetOverlayIndices);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, this->m_puppetOverlayIndices);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, indices.size () * sizeof (GLushort), indices.data (), GL_STATIC_DRAW);

    this->m_puppetOverlayIndexCount = static_cast<GLsizei> (indices.size ());
    this->m_puppetOverlayMaterial = overlay->materialPath;
    sLog.out (
	"Loaded puppet channelmap overlay ", overlay->materialPath, " vertices=", vertexCount,
	" indices=", this->m_puppetOverlayIndexCount
    );
}

void CImage::selectPuppetAnimations (const float sceneTime) {
    this->m_puppetActiveLayers.clear ();

    if (this->m_puppetAnimation.animations.empty ()) {
	return;
    }

    const auto findAnimation = [this] (const uint32_t id) -> const MdlAnimationClip* {
	const auto found = std::find_if (
	    this->m_puppetAnimation.animations.begin (), this->m_puppetAnimation.animations.end (),
	    [id] (const MdlAnimationClip& animation) { return animation.id == id; }
	);
	return found != this->m_puppetAnimation.animations.end () ? &*found : nullptr;
    };

    // Legacy scenes sometimes omit animationlayers entirely and rely on the first
    // embedded animation. Authored zero-weight layers remain in the list to keep
    // native layer ordering and playback state stable; the evaluator owns the
    // model's shared reference pose independently of the active layer order.
    if (this->getImage ().animationLayers.empty ()) {
	this->m_puppetActiveLayers.push_back (
	    {
		.animation = &this->m_puppetAnimation.animations.front (),
		.time = sceneTime,
	    }
	);
	return;
    }

    for (const auto& layer : this->getImage ().animationLayers) {
	const bool visible = layer->visible->value->getBool ();
	const bool blendIn = layer->blendIn->value->getBool ();
	const bool blendOut = layer->blendOut->value->getBool ();
	const float blendTime = std::max (layer->blendTime->value->getFloat (), 0.0f);
	const float rate = layer->rate->value->getFloat ();
	const auto animationId = static_cast<uint32_t> (layer->animation->value->getInt ());
	auto& playback = this->m_puppetLayerPlayback[layer->id];
	float deltaTime = 0.0f;

	if (!playback.initialized) {
	    playback.initialized = true;
	    playback.animation = animationId;
	    playback.visible = visible;
	    playback.time = sceneTime * rate;
	    playback.lastSceneTime = sceneTime;
	    playback.visibilityWeight = visible && blendIn ? 0.0f : visible ? 1.0f : 0.0f;
	} else {
	    deltaTime = sceneTime - playback.lastSceneTime;
	    playback.lastSceneTime = sceneTime;

	    // A timeline seek/reset should seek deterministic scene-driven layers too.
	    if (deltaTime < 0.0f) {
		playback.time = sceneTime * rate;
		deltaTime = 0.0f;
	    } else if (playback.playing && !playback.stopped) {
		playback.time += deltaTime * rate;
	    }

	    if (playback.animation != animationId) {
		playback.animation = animationId;
		playback.time = 0.0f;
		playback.stopped = false;
	    }

	    if (visible != playback.visible) {
		playback.visible = visible;
		if ((visible && !blendIn) || (!visible && !blendOut)) {
		    playback.visibilityWeight = visible ? 1.0f : 0.0f;
		}
	    }
	}

	if (visible) {
	    if (!blendIn || blendTime <= 0.0f) {
		playback.visibilityWeight = 1.0f;
	    } else {
		playback.visibilityWeight
		    = std::min (1.0f, playback.visibilityWeight + std::max (deltaTime, 0.0f) / blendTime);
	    }
	} else if (!blendOut || blendTime <= 0.0f) {
	    playback.visibilityWeight = 0.0f;
	} else {
	    playback.visibilityWeight
		= std::max (0.0f, playback.visibilityWeight - std::max (deltaTime, 0.0f) / blendTime);
	}

	const float weight = std::clamp (layer->blend->value->getFloat (), 0.0f, 1.0f) * playback.visibilityWeight;
	const MdlAnimationClip* animation = findAnimation (animationId);
	if (animation != nullptr) {
	    this->m_puppetActiveLayers.push_back (
		{
		    .animation = animation,
		    .time = playback.stopped ? 0.0f : playback.time,
		    .weight = weight,
		    .additive = layer->additive->value->getBool (),
		}
	    );
	}
    }
}

void CImage::updatePuppetPositionBuffer (const glm::vec2& size) {
    if (this->m_puppetRawPositions.empty ()) {
	return;
    }

    const float sceneTime = this->getScene ().getTime ();
    this->selectPuppetAnimations (sceneTime);

    // Vertex positions are stored in the assembled rest pose, so each bone applies
    // animatedWorld * inverseBindWorld. The same evaluator now drives 3D models too.
    auto pose = MdlAnimationEvaluator::evaluate (this->m_puppetAnimation, this->m_puppetActiveLayers);
    this->m_puppetWorldBones = std::move (pose.worldBones);
    this->m_puppetSkinBones = std::move (pose.skinBones);

    // ponytail: only g_BlendMap row zero, which covers BLENDROWCOUNT 1 (every channelmap
    // material in the corpus). More rows would need a vec4 array uniform instead.
    this->m_puppetBlendMap = glm::vec4 (0.0f);
    for (size_t track = 0; track < pose.blendWeights.size () && track < 4; track++) {
	this->m_puppetBlendMap[static_cast<glm::length_t> (track)] = pose.blendWeights[track];
    }

    this->m_puppetPositions.clear ();
    this->m_puppetPositions.reserve (this->m_puppetRawPositions.size ());
    for (size_t index = 0; index + 2 < this->m_puppetRawPositions.size (); index += 3) {
	glm::vec3 position (
	    this->m_puppetRawPositions[index], this->m_puppetRawPositions[index + 1],
	    this->m_puppetRawPositions[index + 2]
	);

	if (!this->m_puppetSkinBones.empty ()) {
	    const size_t vertex = index / 3;
	    const glm::vec4 rest (position, 1.0f);
	    glm::vec4 posed (0.0f);
	    float totalWeight = 0.0f;

	    for (size_t influence = 0; influence < 4; influence++) {
		const float weight = this->m_puppetBlendWeights[vertex * 4 + influence];
		const uint32_t bone = this->m_puppetBlendIndices[vertex * 4 + influence];

		if (weight <= 0.0f || bone >= this->m_puppetSkinBones.size ()) {
		    continue;
		}

		posed += weight * (this->m_puppetSkinBones[bone] * rest);
		totalWeight += weight;
	    }

	    if (totalWeight > 0.0f) {
		position = glm::vec3 (posed) / totalWeight;
	    }
	}

	// map the posed model-space position onto the object's scene-space quad (m_pos), the
	// same mapping the plain quad's corners get; parts posed outside the image bounds
	// simply extend beyond the quad instead of being clipped by an FBO
	const float u = 0.5f + position.x / size.x;
	const float v = 0.5f - position.y / size.y;
	this->m_puppetPositions.push_back (this->m_pos.x + u * (this->m_pos.z - this->m_pos.x));
	this->m_puppetPositions.push_back (this->m_pos.w + v * (this->m_pos.y - this->m_pos.w));
	// puppet z encodes part layering, not scene depth (up to ±700 in e.g. 3100265648's
	// gojo); the projection would clip it away, and draw order already comes from the
	// index buffer, so flatten to the quad's plane
	this->m_puppetPositions.push_back (0.0f);
    }

    if (this->m_puppetSpacePosition == GL_NONE) {
	glGenBuffers (1, &this->m_puppetSpacePosition);
    }
    glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetSpacePosition);
    const size_t positionBytes = this->m_puppetPositions.size () * sizeof (GLfloat);
    if (positionBytes != this->m_puppetPositionBufferBytes) {
	glBufferData (GL_ARRAY_BUFFER, positionBytes, this->m_puppetPositions.data (), GL_DYNAMIC_DRAW);
	this->m_puppetPositionBufferBytes = positionBytes;
    } else if (positionBytes > 0) {
	glBufferSubData (GL_ARRAY_BUFFER, 0, positionBytes, this->m_puppetPositions.data ());
    }
}

void CImage::setupPuppetAlbedoPasses () {
    if (this->m_puppetOverlayIndexCount == 0 || this->m_puppetOverlayMaterial.empty ()) {
	return;
    }

    if (this->m_material.passes.empty ()) {
	sLog.error ("Puppet channelmap has no base texture for object ", this->getId ());
	return;
    }
    const auto baseTexture = this->m_material.passes.front ()->textures.find (0);
    if (baseTexture == this->m_material.passes.front ()->textures.end ()) {
	sLog.error ("Puppet channelmap has no base texture for object ", this->getId ());
	return;
    }

    try {
	// Wallpaper Engine creates this exact target, copies the layer's base image
	// into it with fullscreenlayer, and only then draws puppettexturechannels.
	const std::string albedoName = "_rt_imageLayerAlbedo_" + std::to_string (this->getId ());
	auto copyMaterial = MaterialParser::load (
	    this->getScene ().getScene ().project, "materials/util/fullscreenlayer.json"
	);
	auto overlayMaterial
	    = MaterialParser::load (this->getScene ().getScene ().project, this->m_puppetOverlayMaterial);
	if (copyMaterial->passes.empty () || overlayMaterial->passes.empty ()) {
	    throw std::runtime_error ("puppet albedo material has no passes");
	}

	const auto* stableCopyMaterial
	    = this->m_materials.compatibilityMaterials.emplace_back (std::move (copyMaterial)).get ();
	const auto* stableOverlayMaterial
	    = this->m_materials.compatibilityMaterials.emplace_back (std::move (overlayMaterial)).get ();

	this->m_puppetAlbedoFBO = this->create (
	    albedoName, TextureFormat_ARGB8888, this->getTexture ()->getFlags (), 1.0f, this->m_size, this->m_size
	);

	auto copyOverride = std::make_unique<ImageEffectPassOverride> (ImageEffectPassOverride {
	    .id = -1,
	    .combos = this->getTexture ()->isAnimated () ? ComboMap { { "SPRITESHEET", 1 } } : ComboMap {},
	    .constants = {},
	    .textures = { { 0, baseTexture->second } },
	});
	const auto& stableCopyOverride = *this->m_materials.compatibilityOverrides.emplace_back (
	    std::move (copyOverride)
	);

	auto inputOverride = std::make_unique<ImageEffectPassOverride> (ImageEffectPassOverride {
	    .id = -1,
	    .combos = {},
	    .constants = {},
	    .textures = { { 0, albedoName } },
	});
	this->m_puppetAlbedoInputOverride
	    = this->m_materials.compatibilityOverrides.emplace_back (std::move (inputOverride)).get ();

	this->m_puppetAlbedoCopyPass = new CPass (
	    *this, std::make_shared<FBOProvider> (this), **stableCopyMaterial->passes.begin (), stableCopyOverride,
	    std::nullopt, std::nullopt
	);
	this->m_puppetAlbedoCopyPass->setBlendingMode (BlendingMode_Normal);
	this->m_puppetAlbedoCopyPass->setDestination (this->m_puppetAlbedoFBO);
	this->m_puppetAlbedoCopyPass->setInput (this->getTexture ());
	this->m_puppetAlbedoCopyPass->setPosition (this->getPassSpacePosition ());
	this->m_puppetAlbedoCopyPass->setTexCoord (this->getTexCoordCopy ());
	this->m_puppetAlbedoCopyPass->setModelViewProjectionMatrix (&this->m_modelViewProjectionPass);
	this->m_puppetAlbedoCopyPass->setModelViewProjectionMatrixInverse (&this->m_modelViewProjectionPassInverse);
	this->m_puppetAlbedoCopyPass->setModelMatrix (&this->m_modelMatrix);
	this->m_puppetAlbedoCopyPass->setViewProjectionMatrix (&this->m_viewProjectionMatrix);

	this->m_puppetOverlayPass = new CPass (
	    *this, std::make_shared<FBOProvider> (this), **stableOverlayMaterial->passes.begin (), std::nullopt,
	    std::nullopt, std::nullopt
	);

	// The channel material supplies the channel atlas in texture 0 and the
	// original base image in texture 1. Its DOUBLEBUFFERED path mixes those
	// pixels using g_BlendMap, directly into the local albedo target.
	this->m_puppetOverlayPass->setDestination (this->m_puppetAlbedoFBO);
	this->m_puppetOverlayPass->setInput (this->getTexture ());
	this->m_puppetOverlayPass->setModelViewProjectionMatrix (&this->m_modelViewProjectionCopy);
	this->m_puppetOverlayPass->setModelViewProjectionMatrixInverse (&this->m_modelViewProjectionCopyInverse);
	this->m_puppetOverlayPass->setModelMatrix (&this->m_modelMatrix);
	this->m_puppetOverlayPass->setViewProjectionMatrix (&this->m_viewProjectionMatrix);
	this->m_puppetOverlayPass->addUniform ("g_BlendMap", &this->m_puppetBlendMap, 1);

	CPass* pass = this->m_puppetOverlayPass;
	pass->setGeometryCallback (
	    [this, pass] () {
		const GLint position = glGetAttribLocation (pass->getProgramID (), "a_Position");
		const GLint texCoord = glGetAttribLocation (pass->getProgramID (), "a_TexCoordVec4");
		const GLint blendIndices = glGetAttribLocation (pass->getProgramID (), "a_BlendIndices");

		if (position >= 0) {
		    glEnableVertexAttribArray (position);
		    glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetOverlayPosition);
		    glVertexAttribPointer (position, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
		}

		if (texCoord >= 0) {
		    glEnableVertexAttribArray (texCoord);
		    glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetOverlayTexCoord);
		    glVertexAttribPointer (texCoord, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
		}

		if (blendIndices >= 0) {
		    glEnableVertexAttribArray (blendIndices);
		    glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetOverlayBlendIndices);
		    // an integer attribute must not go through the float path or it reads as 0.0
		    glVertexAttribIPointer (blendIndices, 4, GL_UNSIGNED_INT, 0, nullptr);
		}
	    },
	    [this] () {
		const GLboolean cullFaceEnabled = glIsEnabled (GL_CULL_FACE);
		glDisable (GL_CULL_FACE);
		glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, this->m_puppetOverlayIndices);
		glDrawElements (GL_TRIANGLES, this->m_puppetOverlayIndexCount, GL_UNSIGNED_SHORT, nullptr);
		if (cullFaceEnabled) {
		    glEnable (GL_CULL_FACE);
		}
	    },
	    [pass] () {
		for (const auto* name : { "a_Position", "a_TexCoordVec4", "a_BlendIndices" }) {
		    const GLint attribute = glGetAttribLocation (pass->getProgramID (), name);
		    if (attribute >= 0) {
			glDisableVertexAttribArray (attribute);
		    }
		}
	    }
	);

	sLog.out (
	    "Loaded puppet albedo compositor ", this->m_puppetOverlayMaterial,
	    " target=", this->m_puppetAlbedoFBO->getName ()
	);
    } catch (const std::exception& ex) {
	sLog.error ("Could not set up puppet albedo composition ", this->m_puppetOverlayMaterial, ": ", ex.what ());
	delete this->m_puppetAlbedoCopyPass;
	delete this->m_puppetOverlayPass;
	this->m_puppetAlbedoCopyPass = nullptr;
	this->m_puppetOverlayPass = nullptr;
	this->m_puppetAlbedoFBO = nullptr;
	this->m_puppetAlbedoInputOverride = nullptr;
    }
}

void CImage::renderPuppetAlbedo () {
    if (this->m_puppetAlbedoCopyPass == nullptr || this->m_puppetOverlayPass == nullptr) {
	return;
    }

    glColorMask (true, true, true, true);
    this->m_puppetAlbedoCopyPass->render ();
    this->m_puppetOverlayPass->render ();
}

std::optional<CImage::ResolvedTransform> CImage::puppetAttachmentTransform (const std::string& name) const {
    const auto model = this->getAttachmentTransform (name);
    if (!model.has_value ()) {
	return std::nullopt;
    }

    // Attachment matrices and scene object origins use the same authoring-space axes.
    // The scene render path performs its Y flip later, when it builds screen geometry;
    // flipping here as well would send every attached child in the opposite direction.
    glm::vec3 scale {
	glm::length (glm::vec3 ((*model)[0])),
	glm::length (glm::vec3 ((*model)[1])),
	glm::length (glm::vec3 ((*model)[2])),
    };
    glm::mat3 rotation (1.0f);
    for (int column = 0; column < 3; column++) {
	if (scale[column] > 1e-8f) {
	    rotation[column] = glm::vec3 ((*model)[column]) / scale[column];
	}
    }
    if (glm::determinant (rotation) < 0.0f) {
	scale.x = -scale.x;
	rotation[0] = -rotation[0];
    }
    const glm::vec3 angles = glm::eulerAngles (glm::normalize (glm::quat_cast (rotation)));

    return ResolvedTransform {
	.origin = { (*model)[3][0], (*model)[3][1], (*model)[3][2] },
	.scale = scale,
	.angles = angles,
    };
}

void CImage::setupPuppetGeometryCallback (Effects::CPass* pass, bool samplesSourceTexture) const {
    this->setupPuppetRangeGeometryCallback (
	pass, samplesSourceTexture,
	{ MdlDrawRange {
	    .submeshIndex = 0,
	    .firstIndex = 0,
	    .indexCount = static_cast<uint32_t> (this->m_puppetIndexCount),
	} }
    );
}

void CImage::setupPuppetRangeGeometryCallback (
    Effects::CPass* pass, const bool samplesSourceTexture, const std::vector<MdlDrawRange>& ranges
) const {
    // when the final pass samples the source texture directly (no effects) the UVs need the
    // same padding compensation the copy texcoords get; FBO content always spans the full range
    const GLuint texCoordBuffer = samplesSourceTexture ? this->m_puppetTexCoordFirstPass : this->m_puppetTexCoord;

    pass->setGeometryCallback (
	[this, pass, texCoordBuffer] () {
	    const GLint position = glGetAttribLocation (pass->getProgramID (), "a_Position");
	    const GLint texCoord = glGetAttribLocation (pass->getProgramID (), "a_TexCoord");

	    if (position >= 0) {
		glEnableVertexAttribArray (position);
		glBindBuffer (GL_ARRAY_BUFFER, this->m_puppetSpacePosition);
		glVertexAttribPointer (position, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
	    }

	    if (texCoord >= 0) {
		glEnableVertexAttribArray (texCoord);
		glBindBuffer (GL_ARRAY_BUFFER, texCoordBuffer);
		glVertexAttribPointer (texCoord, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	    }
	},
	[this, ranges] () {
	    // the Y-flip into scene space inverts triangle winding, so materials with
	    // cullmode "normal" would cull the whole mesh (e.g. the eye layer in 3558034522)
	    const GLboolean cullFaceEnabled = glIsEnabled (GL_CULL_FACE);
	    glDisable (GL_CULL_FACE);
	    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, this->m_puppetIndices);
	    for (const auto& range : ranges) {
		if (range.submeshIndex != 0 || range.indexCount == 0) {
		    continue;
		}
		const auto byteOffset = static_cast<uintptr_t> (range.firstIndex) * sizeof (GLushort);
		glDrawElements (
		    GL_TRIANGLES, static_cast<GLsizei> (range.indexCount), GL_UNSIGNED_SHORT,
		    reinterpret_cast<const void*> (byteOffset)
		);
	    }
	    if (cullFaceEnabled) {
		glEnable (GL_CULL_FACE);
	    }
	},
	[pass] () {
	    const GLint position = glGetAttribLocation (pass->getProgramID (), "a_Position");
	    const GLint texCoord = glGetAttribLocation (pass->getProgramID (), "a_TexCoord");

	    if (position >= 0) {
		glDisableVertexAttribArray (position);
	    }

	    if (texCoord >= 0) {
		glDisableVertexAttribArray (texCoord);
	    }
	}
    );
}

bool CImage::setupPuppetClippingPasses (
    Effects::CPass& finalPass, const std::shared_ptr<const TextureProvider>& input,
    const std::shared_ptr<const CFBO>& destination, const glm::mat4* projection,
    const glm::mat4* inverseProjection, const bool samplesSourceTexture
) {
    const auto& model = *this->getImage ().model;
    if (!model.puppetMesh.has_value () || model.puppetMesh->clippingDescriptors.empty ()) {
	return false;
    }

    try {
	const auto& mesh = *model.puppetMesh;
	const auto plan = buildPuppetClippingPlan (mesh, 0);
	if (plan.requiresComposition) {
	    sLog.error (
		"Puppet clipping for ", *model.puppet,
		" requires the multi-mask composition path; rendering the un-clipped body for now"
	    );
	    return false;
	}
	for (const auto& mask : plan.masks) {
	    const auto& descriptor = mesh.clippingDescriptors[mask.descriptorIndex];
	    if (descriptor.flags != 0) {
		sLog.error (
		    "Puppet clipping for ", *model.puppet, " uses unsupported descriptor flags 0x", std::hex,
		    descriptor.flags, std::dec, "; rendering the un-clipped body for now"
		);
		return false;
	    }
	    for (const uint32_t rangeIndex : mask.sourceRanges) {
		if (mesh.drawRanges[rangeIndex].submeshIndex != 0) {
		    sLog.error (
			"Puppet clipping source references unsupported submesh ",
			mesh.drawRanges[rangeIndex].submeshIndex, " in ", *model.puppet
		    );
		    return false;
		}
	    }
	}
	for (const auto& draw : plan.draws) {
	    if (draw.submeshIndex != 0) {
		sLog.error ("Puppet clipping draw references unsupported submesh ", draw.submeshIndex, " in ", *model.puppet);
		return false;
	    }
	}

	// CLIPPINGTARGET and CLIPPINGUVS are implemented by genericimage4. Puppet
	// effect chains deliberately end in effectpassthrough_4 when descriptors exist.
	if (finalPass.getPass ().shader != "genericimage4") {
	    sLog.error (
		"Puppet clipping final shader ", finalPass.getPass ().shader,
		" has no genericimage4 clipping path in ", *model.puppet
	    );
	    return false;
	}

	const auto sceneSize = glm::vec2 (this->getScene ().getWidth (), this->getScene ().getHeight ());
	const std::string maskName = "_rt_puppetClipping_" + std::to_string (this->getId ());
	this->m_puppetClippingFBO = this->create (
	    maskName, TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0f, sceneSize, sceneSize
	);

	this->m_materials.compatibilityMaterials.emplace_back (
	    MaterialParser::load (this->getScene ().getScene ().project, "materials/util/clippingmaskimage4.json")
	);
	const auto& maskMaterial = *this->m_materials.compatibilityMaterials.back ();
	const auto provider = std::make_shared<FBOProvider> (this);
	std::vector<Effects::CPass*> maskPasses;
	maskPasses.reserve (plan.masks.size ());

	const auto configurePass = [this, &input, &destination, projection, inverseProjection] (Effects::CPass& pass) {
	    pass.setDestination (destination);
	    pass.setInput (input);
	    pass.setModelViewProjectionMatrix (projection);
	    pass.setModelViewProjectionMatrixInverse (inverseProjection);
	    pass.setModelMatrix (&this->m_modelMatrix);
	    pass.setViewProjectionMatrix (&this->m_viewProjectionMatrix);
	    pass.setEffectTextureProjectionMatrix (
		&this->m_effectTextureProjectionMatrix, &this->m_effectTextureProjectionMatrixInverse
	    );
	    if (this->getScene ().getScene ().camera.projection.isPerspective) {
		pass.setModelMatrix (&this->m_sceneModelMatrix);
		pass.setViewProjectionMatrix (&this->m_sceneViewProjectionMatrix);
		pass.addUniform ("g_NormalModelMatrix", &this->m_sceneNormalModelMatrix);
	    }
	};

	for (const auto& mask : plan.masks) {
	    const auto& descriptor = mesh.clippingDescriptors[mask.descriptorIndex];
	    auto maskOverride = std::make_unique<ImageEffectPassOverride> (ImageEffectPassOverride {
		.id = -1,
		.combos = {},
		.constants = {},
		.textures = { { 1, descriptor.maskAsset } },
	    });
	    const auto& stableOverride = *this->m_materials.compatibilityOverrides.emplace_back (
		std::move (maskOverride)
	    );

	    auto* pass = new CPass (
		*this, provider, **maskMaterial.passes.begin (), stableOverride, std::nullopt, std::nullopt
	    );
	    this->m_puppetClippingPasses.push_back (pass);
	    maskPasses.push_back (pass);
	    pass->setDestination (this->m_puppetClippingFBO);
	    pass->setInput (input);
	    pass->setModelViewProjectionMatrix (projection);
	    pass->setModelViewProjectionMatrixInverse (inverseProjection);
	    pass->setModelMatrix (&this->m_modelMatrix);
	    pass->setViewProjectionMatrix (&this->m_viewProjectionMatrix);
	    pass->setEffectTextureProjectionMatrix (
		&this->m_effectTextureProjectionMatrix, &this->m_effectTextureProjectionMatrixInverse
	    );
	    pass->setBlendEquation (GL_MAX, GL_FUNC_ADD);
	    this->m_puppetClippingRenderVars.emplace_back (std::make_unique<glm::vec4> (0.0f));
	    pass->addUniform ("g_RenderVar0", this->m_puppetClippingRenderVars.back ().get ());

	    std::vector<MdlDrawRange> sourceRanges;
	    sourceRanges.reserve (mask.sourceRanges.size ());
	    for (const uint32_t rangeIndex : mask.sourceRanges) {
		sourceRanges.push_back (mesh.drawRanges[rangeIndex]);
	    }
	    this->setupPuppetRangeGeometryCallback (pass, samplesSourceTexture, sourceRanges);
	}

	size_t activeMask = std::numeric_limits<size_t>::max ();
	for (const auto& draw : plan.draws) {
	    const bool clipped = !draw.masks.empty ();
	    if (clipped && activeMask != draw.masks.front ()) {
		activeMask = draw.masks.front ();
		this->m_puppetClippingCommands.push_back (PuppetClippingRenderCommand {
		    .buildsMask = true,
		    .pass = maskPasses[activeMask],
		});
	    }

	    std::optional<std::reference_wrapper<const TextureMap>> binds = finalPass.getBinds ();
	    ComboMap runtimeCombos;
	    if (clipped) {
		auto targetBinds = std::make_unique<TextureMap> (finalPass.getBinds ());
		targetBinds->insert_or_assign (8, maskName);
		binds = *this->m_puppetClippingBinds.emplace_back (std::move (targetBinds));
		runtimeCombos = {
		    { "CLIPPINGUVS", 1 },
		    { "CLIPPINGTARGET", 1 },
		};
	    }

	    auto* pass = new CPass (
		*this, provider, finalPass.getPass (), finalPass.getOverride (), binds, std::nullopt,
		std::move (runtimeCombos)
	    );
	    this->m_puppetClippingPasses.push_back (pass);
	    configurePass (*pass);
	    pass->setBlendingMode (finalPass.getBlendingMode ());
	    pass->setDepthtestMode (finalPass.getDepthtestMode ());
	    pass->setDepthwriteMode (finalPass.getDepthwriteMode ());
	    this->setupPuppetRangeGeometryCallback (
		pass, samplesSourceTexture,
		{ MdlDrawRange {
		    .submeshIndex = draw.submeshIndex,
		    .firstIndex = draw.firstIndex,
		    .indexCount = draw.indexCount,
		} }
	    );
	    this->m_puppetClippingCommands.push_back (PuppetClippingRenderCommand {
		.buildsMask = false,
		.pass = pass,
	    });
	}

	sLog.out (
	    "Loaded puppet clipping ", *model.puppet, " masks=", plan.masks.size (), " draws=", plan.draws.size ()
	);
	return !this->m_puppetClippingCommands.empty ();
    } catch (const std::exception& ex) {
	sLog.error ("Could not set up puppet clipping for ", *model.puppet, ": ", ex.what ());
	this->m_puppetClippingCommands.clear ();
	return false;
    }
}

void CImage::renderPuppetClipping () {
    GLfloat clearColor[4];
    glGetFloatv (GL_COLOR_CLEAR_VALUE, clearColor);

    for (const auto& command : this->m_puppetClippingCommands) {
	if (command.buildsMask) {
	    const GLboolean scissorEnabled = glIsEnabled (GL_SCISSOR_TEST);
	    glDisable (GL_SCISSOR_TEST);
	    glColorMask (true, true, true, true);
	    glBindFramebuffer (GL_FRAMEBUFFER, this->m_puppetClippingFBO->getFramebuffer ());
	    glViewport (
		0, 0, this->m_puppetClippingFBO->getRealWidth (), this->m_puppetClippingFBO->getRealHeight ()
	    );
	    glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
	    glClear (GL_COLOR_BUFFER_BIT);
	    if (scissorEnabled) {
		glEnable (GL_SCISSOR_TEST);
	    }
	} else {
	    // Scene alpha is owned by the wallpaper compositor, but an isolated
	    // composition surface needs child alpha so its parent can blend the group.
	    glColorMask (
		true, true, true, this->getScene ().isRenderingToComposition () ? GL_TRUE : GL_FALSE
	    );
	}
	command.pass->render ();
    }

    glClearColor (clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
}

void CImage::setup () {
    // do not double-init stuff, that's bad!
    if (this->m_initialized) {
	return;
    }

    // TODO: CHECK ORDER OF THINGS, 2419444134'S ID 27 DEPENDS ON 104'S COMPOSITE_A WHEN OUR LAST RENDER IS ON
    // COMPOSITE_B
    // TODO: SUPPORT PASSTHROUGH (IT'S A SHADER)
    if (this->m_image.model->passthrough) {
	// passthrough images without effects are bad, do not draw them
	if (this->m_image.effects.empty ()) {
	    return;
	}

	// Some have attempted to declare effects with visible set to false.
	bool allEffectsInvisible = true;
	for (const auto& cur : this->m_image.effects) {
	    if (cur->visible->value->getBool ()) {
		allEffectsInvisible = false;
		break;
	    }
	}

	if (allEffectsInvisible) {
	    return;
	}
    }

    const auto& debug = this->getScene ().getContext ().getApp ().getContext ().settings.render.debug;

    // Build the binary-compatible channelmap prepass before the base material:
    // the base pass receives _rt_imageLayerAlbedo_<id> instead of the source
    // texture when a channelmap submesh is present.
    this->setupPuppetAlbedoPasses ();
    const std::optional<std::reference_wrapper<const ImageEffectPassOverride>> baseOverride
	= this->m_puppetAlbedoInputOverride != nullptr
	? std::optional<std::reference_wrapper<const ImageEffectPassOverride>> (*this->m_puppetAlbedoInputOverride)
	: std::nullopt;

    // copy pass to the composite layer
    for (const auto& cur : this->getImage ().model->material->passes) {
	this->m_passes.push_back (
	    new CPass (*this, std::make_shared<FBOProvider> (this), *cur, baseOverride, std::nullopt, std::nullopt)
	);
    }

    // prepare the passes list
    if (!debug.baseOnly && !this->getImage ().effects.empty ()) {
	// generate the effects used by this material
	for (const auto& cur : this->m_image.effects) {
	    if (std::find (debug.skipEffects.begin (), debug.skipEffects.end (), static_cast<int> (cur->id))
		!= debug.skipEffects.end ()) {
		continue;
	    }

	    // do not add non-visible effects, this might need some adjustements tho as some effects might not be
	    // visible but affect the output of the image...
	    if (!cur->visible->value->getBool ()) {
		continue;
	    }

	    // Register any script-driven shader constants in this effect's pass overrides so their per-frame
	    // scripts actually run. The script source is parsed onto the constant's DynamicValue, but unlike
	    // object-level properties these effect constants are never queued — so e.g. a "rainbow" colour
	    // cycle (a tint colour driven by a JS update()) stays frozen at its static fallback. Queue them
	    // here (visible effects only) so tick() advances the value and CPass uploads the live result.
	    {
		int passIndex = 0;
		for (const auto& passOverride : cur->passOverrides) {
		    for (const auto& [constantName, setting] : passOverride->constants) {
			if (setting->value != nullptr && setting->value->getScriptSource ().has_value ()) {
			    this->getScene ().getScriptEngine ().queueScript (
				constantName + "_fx" + std::to_string (cur->id) + "_p" + std::to_string (passIndex)
				    + "_" + std::to_string (this->getId ()),
				*setting->value, *this
			    );
			}
		    }
		    ++passIndex;
		}
	    }

	    const auto fboProvider = std::make_shared<FBOProvider> (this);

	    // create all the fbos for this effect
	    for (const auto& fbo : cur->effect->fbos) {
		fboProvider->create (*fbo, this->m_texture->getFlags (), this->getSize ());
	    }

	    // TODO: MAKE USE OF ZIP OPERATOR IN BOOST? WAY OVERKILL JUST FOR THIS...

	    auto curEffect = cur->effect->passes.begin ();
	    auto endEffect = cur->effect->passes.end ();
	    auto curOverride = cur->passOverrides.begin ();
	    auto endOverride = cur->passOverrides.end ();

	    for (; curEffect != endEffect; ++curEffect) {
		if (!(*curEffect)->material.has_value ()) {
		    if (!(*curEffect)->command.has_value ()) {
			sLog.error ("Pass without material and command not supported");
			continue;
		    }

		    if (!(*curEffect)->source.has_value ()) {
			sLog.error ("Pass without material and source not supported");
			continue;
		    }

		    if (!(*curEffect)->target.has_value ()) {
			sLog.error ("Pass without material and target not supported");
			continue;
		    }

		    if ((*curEffect)->command != Command_Copy) {
			sLog.error ("Only copy command is supported for pass without material");
			continue;
		    }

		    auto virtualPass
			= std::make_unique<MaterialPass> (MaterialPass { .blending = BlendingMode_Normal,
									 .cullmode = CullingMode_Disable,
									 .depthtest = DepthtestMode_Disabled,
									 .depthwrite = DepthwriteMode_Disabled,
									 .shader = "commands/copy",
									 .textures = { { 0, *(*curEffect)->source } },
									 .combos = {},
									 .constants = {} });

		    const auto& config = *this->m_virtualPassess.emplace_back (std::move (virtualPass));

		    // build a pass for a copy shader
		    this->m_passes.push_back (new CPass (
			*this, fboProvider, config, std::nullopt, std::nullopt, (*curEffect)->target.value ()
		    ));
		} else {
		    for (auto& pass : (*curEffect)->material.value ()->passes) {
			const auto override = curOverride != endOverride
			    ? **curOverride
			    : std::optional<std::reference_wrapper<const ImageEffectPassOverride>> (std::nullopt);
			const auto target = (*curEffect)->target.has_value ()
			    ? *(*curEffect)->target
			    : std::optional<std::reference_wrapper<std::string>> (std::nullopt);

			this->m_passes.push_back (
			    new CPass (*this, fboProvider, *pass, override, (*curEffect)->binds, target)
			);
		    }

		    if (curOverride != endOverride) {
			++curOverride;
		    }
		}
	    }
	}
    }

    if (!debug.baseOnly) {
	const auto magentaCompositeTint = findMagentaCompositeTint (this->m_image, debug.skipEffects);
	if (magentaCompositeTint.has_value ()) {
	    auto tintOverride = std::make_unique<ImageEffectPassOverride> (ImageEffectPassOverride {
		.id = -1,
		.combos = {
		    { "BLENDMODE", 30 },
		},
		.constants = {},
		.textures = {},
	    });
	    tintOverride->constants.emplace ("color", UserSettingBuilder::fromValue (magentaCompositeTint.value ()));
	    tintOverride->constants.emplace ("alpha", UserSettingBuilder::fromValue (1.0f));

	    this->m_materials.compatibilityMaterials.emplace_back (
		MaterialParser::load (this->getScene ().getScene ().project, "materials/effects/tint.json")
	    );
	    this->m_materials.compatibilityOverrides.emplace_back (std::move (tintOverride));

	    this->m_passes.push_back (new CPass (
		*this, std::make_shared<FBOProvider> (this),
		**this->m_materials.compatibilityMaterials.back ()->passes.begin (),
		*this->m_materials.compatibilityOverrides.back (), std::nullopt, std::nullopt
	    ));
	}
    }

    // extra render pass if there's any blending to be done
    if (!debug.baseOnly && this->m_image.colorBlendMode->value->getInt () > 0) {
	this->m_materials.colorBlending.material
	    = MaterialParser::load (this->getScene ().getScene ().project, "materials/util/effectpassthrough.json");
	this->m_materials.colorBlending.override = std::make_unique<ImageEffectPassOverride> (ImageEffectPassOverride {
            .id = -1,
            .combos = {
                {"BLENDMODE", this->m_image.colorBlendMode->value->getInt()},
            },
            .constants = {},
            .textures = {},
        });

	this->m_passes.push_back (new CPass (
	    *this, std::make_shared<FBOProvider> (this), **this->m_materials.colorBlending.material->passes.begin (),
	    *this->m_materials.colorBlending.override, std::nullopt, std::nullopt
	));
    }

    // puppet-warped images with effects get a dedicated composite pass: effect shaders can
    // manipulate a_Position in image space (e.g. pulse, foliagesway), so hijacking the last
    // effect pass with scene-space puppet geometry mangles the mesh (gojo in 3100265648);
    // a passthrough pass is position-neutral and safe to warp
    if (this->m_hasPuppetMesh && this->m_passes.size () > 1) {
	const bool hasClipping = this->getImage ().model->puppetMesh.has_value ()
	    && !this->getImage ().model->puppetMesh->clippingDescriptors.empty ();
	this->m_materials.compatibilityMaterials.emplace_back (
	    MaterialParser::load (
		this->getScene ().getScene ().project,
		hasClipping ? "materials/util/effectpassthrough_4.json" : "materials/util/effectpassthrough.json"
	    )
	);
	this->m_materials.compatibilityOverrides.emplace_back (
	    std::make_unique<ImageEffectPassOverride> (ImageEffectPassOverride {
		.id = -1,
		.combos = {},
		.constants = {},
		.textures = {},
	    })
	);

	this->m_passes.push_back (new CPass (
	    *this, std::make_shared<FBOProvider> (this),
	    **this->m_materials.compatibilityMaterials.back ()->passes.begin (),
	    *this->m_materials.compatibilityOverrides.back (), std::nullopt, std::nullopt
	));
    }

    // If there is more than one pass, the first pass renders only to an intermediate FBO while the
    // last pass draws the layer geometry into the scene. Move explicitly enabled depth state from
    // the base material to that final composite pass. Keeping an effect's usual depthtest=disabled
    // there lets a depth-tested background layer overwrite 3D models in front of it (Ventura 4K,
    // Workshop 3298178668). Leave all-disabled 2D effect chains untouched: forcing their state
    // breaks full-resolution blur results followed by another effect (Legendaries of Hoenn,
    // Workshop 3101147701).
    if (this->m_passes.size () > 1) {
	const auto first = this->m_passes.begin ();
	const auto last = this->m_passes.rbegin ();

	(*last)->setBlendingMode ((*first)->getBlendingMode ());
	(*first)->setBlendingMode (BlendingMode_Normal);
	if ((*first)->getDepthtestMode () == DepthtestMode_Enabled) {
	    (*last)->setDepthtestMode (DepthtestMode_Enabled);
	    (*first)->setDepthtestMode (DepthtestMode_Disabled);
	}
	if ((*first)->getDepthwriteMode () == DepthwriteMode_Enabled) {
	    (*last)->setDepthwriteMode (DepthwriteMode_Enabled);
	    (*first)->setDepthwriteMode (DepthwriteMode_Disabled);
	}
    }

    CRenderable::setup ();

    this->setupPasses ();
    this->m_initialized = true;
}

void CImage::setupPasses () {
    // do a pass on everything and setup proper inputs and values
    std::shared_ptr<const CFBO> drawTo = this->m_currentMainFBO;
    std::shared_ptr<const TextureProvider> asInput
	= this->m_puppetAlbedoFBO != nullptr ? this->m_puppetAlbedoFBO : this->getTexture ();
    GLuint texcoord = this->m_puppetAlbedoFBO != nullptr ? this->getTexCoordPass () : this->getTexCoordCopy ();

    auto cur = this->m_passes.begin ();
    auto end = this->m_passes.end ();
    bool first = true;
    bool inTargetEffectSequence = false;
    std::shared_ptr<const TextureProvider> effectInput = nullptr;

    for (; cur != end; ++cur) {
	// TODO: PROPERLY CHECK EFFECT'S VISIBILITY AND TAKE IT INTO ACCOUNT
	// TODO: THIS REQUIRES ON-THE-FLY EVALUATION OF EFFECTS VISIBILITY TO FIGURE OUT
	// TODO: WHICH ONE IS THE LAST + A FEW OTHER THINGS
	Effects::CPass* pass = *cur;
	std::shared_ptr<const CFBO> prevDrawTo = drawTo;
	bool writesToTarget = false;
	const bool isFirstPass = first;
	GLuint spacePosition = (isFirstPass) ? this->getCopySpacePosition () : this->getPassSpacePosition ();
	const glm::mat4* projection
	    = (isFirstPass) ? &this->m_modelViewProjectionCopy : &this->m_modelViewProjectionPass;
	const glm::mat4* inverseProjection
	    = (isFirstPass) ? &this->m_modelViewProjectionCopyInverse : &this->m_modelViewProjectionPassInverse;
	first = false;

	pass->setModelMatrix (&this->m_modelMatrix);
	pass->setViewProjectionMatrix (&this->m_viewProjectionMatrix);
	pass->setEffectTextureProjectionMatrix (
	    &this->m_effectTextureProjectionMatrix, &this->m_effectTextureProjectionMatrixInverse
	);

	writesToTarget = this->configurePassTarget (pass, drawTo, asInput, effectInput, inTargetEffectSequence);
	// determine if it's the last element in the list as this is a screen-copy-like process
	// TODO: PROPERLY CHECK IF THIS IS ALL THAT'S NEEDED
	if (!writesToTarget && this->shouldRenderFinalPass (std::next (cur) == end)) {
	    // TODO: PROPERLY CHECK EFFECT'S VISIBILITY AND TAKE IT INTO ACCOUNT
	    drawTo = this->getScene ().getFBO ();
	    this->m_finalPassDrawsToScene = true;

	    if (this->getImage ().model->passthrough && this->getImage ().model->fullscreen) {
		// A fullscreen passthrough layer is a whole-frame post-process (it sampled the scene from
		// _rt_FullFrameBuffer and graded it). Its writeback must cover the entire framebuffer. The
		// scene-space quad routes through m_modelViewProjectionScreen (ortho * camera lookAt), whose
		// lookAt tilts this flat z=0 quad so its corners fall outside the [-1,1] depth-clip volume —
		// the GPU clips a slab and the uncovered pixels keep the ungraded scene (the "coloring applies
		// only halfway" artifact on Starscape). Use the identity-projected full -1..1 NDC quad instead,
		// exactly like the copy/intermediate passes (and like WE's untransformed passthrough.vert).
		spacePosition = this->getPassSpacePosition ();
		projection = &this->m_modelViewProjectionPass;
		inverseProjection = &this->m_modelViewProjectionPassInverse;
	    } else {
		spacePosition = this->getSceneSpacePosition ();
		projection = &this->m_modelViewProjectionScreen;
		inverseProjection = &this->m_modelViewProjectionScreenInverse;

		// genericimage3/4's LIGHTING branch does not use g_ModelViewProjectionMatrix for
		// vertex placement. It multiplies by g_ModelMatrix and g_ViewProjectionMatrix
		// separately so lighting can retain world-space position and normals. Point the
		// final scene pass at the same world/camera split used by 3D models; leaving the
		// image-copy matrices here turns lit image layers into giant top-left slabs.
		if (this->getScene ().getScene ().camera.projection.isPerspective) {
		    pass->setModelMatrix (&this->m_sceneModelMatrix);
		    pass->setViewProjectionMatrix (&this->m_sceneViewProjectionMatrix);
		    pass->addUniform ("g_NormalModelMatrix", &this->m_sceneNormalModelMatrix);
		}
	    }

		    // puppet warp deforms the final on-screen geometry only; every earlier pass works on
		    // the untouched image so effect masks keep lining up with it in the intermediate FBOs
		    if (this->m_hasPuppetMesh) {
			const bool samplesSourceTexture = isFirstPass && this->m_puppetAlbedoFBO == nullptr;
			this->m_hasPuppetClipping = this->setupPuppetClippingPasses (
			    *pass, asInput, drawTo, projection, inverseProjection, samplesSourceTexture
			);
			if (!this->m_hasPuppetClipping) {
			    this->setupPuppetGeometryCallback (pass, samplesSourceTexture);
			}
		    }
	}

	pass->setDestination (drawTo);
	pass->setInput (asInput);
	pass->setPreviousInput (inTargetEffectSequence ? effectInput : nullptr);
	pass->setPosition (spacePosition);
	pass->setTexCoord (texcoord);
	pass->setModelViewProjectionMatrix (projection);
	pass->setModelViewProjectionMatrixInverse (inverseProjection);

	texcoord = this->getTexCoordPass ();

	if (writesToTarget) {
	    asInput = drawTo;
	    drawTo = prevDrawTo;
	} else {
	    drawTo = prevDrawTo;
	    this->pinpongFramebuffer (&drawTo, &asInput);
	    inTargetEffectSequence = false;
	    effectInput = nullptr;
	}
    }
}

bool CImage::shouldRenderFinalPass (bool isLastPass) const {
    if (!isLastPass || !this->getImage ().visible->value->getBool ()) {
	return false;
    }

    const auto& debug = this->getScene ().getContext ().getApp ().getContext ().settings.render.debug;
    return !(debug.noSolidFinal && this->getImage ().model->solidlayer);
}

bool CImage::configurePassTarget (
    Effects::CPass* pass, std::shared_ptr<const CFBO>& drawTo, const std::shared_ptr<const TextureProvider>& asInput,
    std::shared_ptr<const TextureProvider>& effectInput, bool& inTargetEffectSequence
) {
    if (!pass->getTarget ().has_value ()) {
	return false;
    }

    const std::string target = pass->getTarget ().value ();
    std::shared_ptr<const CFBO> resolved = pass->getFBOProvider ()->find (target);
    if (resolved == nullptr) {
	resolved = this->getScene ().findFBO (target);
    }
    if (resolved == nullptr) {
	sLog.error (
	    "Pass target FBO '", target, "' could not be resolved for object ", pass->getRenderable ().getId (),
	    " shader=", pass->getPass ().shader
	);
	return false;
    }

    if (!inTargetEffectSequence) {
	effectInput = asInput;
	inTargetEffectSequence = true;
    }
    drawTo = resolved;
    return true;
}

void CImage::pinpongFramebuffer (std::shared_ptr<const CFBO>* drawTo, std::shared_ptr<const TextureProvider>* asInput) {
    // temporarily store FBOs used
    std::shared_ptr<const CFBO> currentMainFBO = this->m_currentMainFBO;
    std::shared_ptr<const CFBO> currentSubFBO = this->m_currentSubFBO;

    if (drawTo != nullptr) {
	*drawTo = currentSubFBO;
    }
    if (asInput != nullptr) {
	*asInput = currentMainFBO;
    }

    // swap the FBOs
    this->m_currentMainFBO = currentSubFBO;
    this->m_currentSubFBO = currentMainFBO;
}

void CImage::render () {
    // do not try to render something that did not initialize successfully
    if (!this->m_initialized) {
	return;
    }

    // A hidden layer still has to fill its own composite render target. 3D characters keep their
    // face on a separate image layer and sample it from the model's material as
    // _rt_imageLayerComposite_<id>_a, and those layers are authored invisible on purpose so the
    // quad itself never appears in the scene (3737268876 does this for all 27 of its faces, plus
    // its water normal map). Such a layer had no pass pointed at the scene FBO in the first place
    // (shouldRenderFinalPass), so rendering it can only touch its own targets. Layers that were
    // visible when their passes were built do reach the scene, and those still stop here when
    // hidden at runtime.
    if (!this->getImage ().visible->value->getBool () && this->m_finalPassDrawsToScene) {
	return;
    }

    // a hidden container hides its whole subtree; children often have no visible of their own
    if (!this->isVisibleThroughParents ()) {
	return;
    }

    // Image opacity can be keyframed independently of its texture animation.
    // Keep the evaluated value in stable member storage because CPass uniforms
    // retain a pointer returned by getAlpha()/getUserAlpha(). This is especially
    // important for authored opening overlays that fade away to reveal the live
    // scene below them.
    this->m_resolvedAlpha = this->m_image.alpha->evaluateFloat (this->getScene ().getTime ());

    glColorMask (true, true, true, true);

    // Compose animated channel-map quads into the base albedo before any effect
    // pass or puppet deformation consumes it.
    this->renderPuppetAlbedo ();

    // Always update screen transform (handles rotation + parallax dynamically);
    // fullscreen/autosize/locked layers are excluded from parallax inside
    this->updateScreenSpacePosition ();

#if !NDEBUG
    std::string str = "Image ";

    if (this->getScene ().getScene ().camera.bloom.enabled->value->getBool () && this->getId () == -1) {
	str += "bloom";
    } else {
	str += this->getImage ().name + " (" + std::to_string (this->getId ()) + ", "
	    + this->getImage ().model->material->filename + ")";
    }

    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, str.c_str ());
#endif /* DEBUG */

    auto cur = this->m_passes.begin ();

    for (const auto end = this->m_passes.end (); cur != end; ++cur) {
	if (std::next (cur) == end) {
	    if (this->m_hasPuppetClipping) {
		this->renderPuppetClipping ();
		continue;
	    }
	    // Preserve alpha only on the real scene target. A child image rendered
	    // into a composition layer must contribute its authored alpha.
	    glColorMask (
		true, true, true, this->getScene ().isRenderingToComposition () ? GL_TRUE : GL_FALSE
	    );
	}

	(*cur)->render ();
    }

    // Restore alpha writes: leaving the mask disabled leaks it into the next frame's scene clear (the
    // clear silently stops writing alpha) and into any FBO created afterwards (an in-process wallpaper
    // rebuild "clears" its new framebuffers to uninitialized VRAM). The scene buffer's alpha then sticks
    // at whatever the allocation contained, and every alpha-blended writeback composites against it.
    glColorMask (true, true, true, true);

#if !NDEBUG
    glPopDebugGroup ();
#endif /* DEBUG */
}

const float& CImage::getBrightness () const { return this->m_image.brightness->value->getFloat (); }

const float& CImage::getUserAlpha () const { return this->m_resolvedAlpha; }

const float& CImage::getAlpha () const { return this->m_resolvedAlpha; }

const glm::vec3& CImage::getColor () const { return this->m_image.color->value->getVec3 (); }

const glm::vec4& CImage::getColor4 () const { return this->m_image.color->value->getVec4 (); }

const glm::vec3& CImage::getCompositeColor () const { return this->m_image.color->value->getVec3 (); }

size_t CImage::getPuppetAnimationLayerCount () const { return this->m_image.animationLayers.size (); }

std::optional<size_t> CImage::findPuppetAnimationLayer (const int index) const {
    if (index < 0 || static_cast<size_t> (index) >= this->m_image.animationLayers.size ()) {
	return std::nullopt;
    }
    return static_cast<size_t> (index);
}

std::optional<size_t> CImage::findPuppetAnimationLayer (const std::string& name) const {
    for (size_t index = 0; index < this->m_image.animationLayers.size (); index++) {
	if (this->m_image.animationLayers[index]->name == name) {
	    return index;
	}
    }
    return std::nullopt;
}

void CImage::playPuppetAnimationLayer (const std::optional<size_t> index) {
    const auto play = [this] (const size_t layerIndex) {
	if (layerIndex >= this->m_image.animationLayers.size ()) {
	    return;
	}
	auto& playback = this->m_puppetLayerPlayback[this->m_image.animationLayers[layerIndex]->id];
	playback.playing = true;
	playback.stopped = false;
    };

    if (index.has_value ()) {
	play (*index);
    } else {
	for (size_t layerIndex = 0; layerIndex < this->m_image.animationLayers.size (); layerIndex++) {
	    play (layerIndex);
	}
    }
}

void CImage::pausePuppetAnimationLayer (const std::optional<size_t> index) {
    const auto pause = [this] (const size_t layerIndex) {
	if (layerIndex < this->m_image.animationLayers.size ()) {
	    this->m_puppetLayerPlayback[this->m_image.animationLayers[layerIndex]->id].playing = false;
	}
    };

    if (index.has_value ()) {
	pause (*index);
    } else {
	for (size_t layerIndex = 0; layerIndex < this->m_image.animationLayers.size (); layerIndex++) {
	    pause (layerIndex);
	}
    }
}

void CImage::stopPuppetAnimationLayer (const std::optional<size_t> index) {
    const auto stop = [this] (const size_t layerIndex) {
	if (layerIndex >= this->m_image.animationLayers.size ()) {
	    return;
	}
	auto& playback = this->m_puppetLayerPlayback[this->m_image.animationLayers[layerIndex]->id];
	playback.playing = false;
	playback.stopped = true;
	playback.time = 0.0f;
    };

    if (index.has_value ()) {
	stop (*index);
    } else {
	for (size_t layerIndex = 0; layerIndex < this->m_image.animationLayers.size (); layerIndex++) {
	    stop (layerIndex);
	}
    }
}

bool CImage::isPuppetAnimationLayerPlaying (const std::optional<size_t> index) const {
    const auto isPlaying = [this] (const size_t layerIndex) {
	if (layerIndex >= this->m_image.animationLayers.size ()) {
	    return false;
	}
	const int layerId = this->m_image.animationLayers[layerIndex]->id;
	const auto playback = this->m_puppetLayerPlayback.find (layerId);
	return playback == this->m_puppetLayerPlayback.end ()
	    || (playback->second.playing && !playback->second.stopped);
    };

    if (index.has_value ()) {
	return isPlaying (*index);
    }
    for (size_t layerIndex = 0; layerIndex < this->m_image.animationLayers.size (); layerIndex++) {
	if (isPlaying (layerIndex)) {
	    return true;
	}
    }
    return this->m_image.animationLayers.empty () && !this->m_puppetAnimation.animations.empty ();
}

glm::vec2 CImage::resolveGeometrySize (float sceneWidth, float sceneHeight, glm::vec3& origin) const {
    glm::vec2 size = this->getSize ();

    if ((size.x == 0.0f || size.y == 0.0f) && this->m_texture != nullptr) {
	size.x = static_cast<float> (this->m_texture->getRealWidth ());
	size.y = static_cast<float> (this->m_texture->getRealHeight ());
    } else if (
	(size.x == 0.0f || size.y == 0.0f) && this->getImage ().model->width.has_value ()
	&& this->getImage ().model->height.has_value ()
    ) {
	size.x = static_cast<float> (this->getImage ().model->width.value ());
	size.y = static_cast<float> (this->getImage ().model->height.value ());
    }

    if (this->getImage ().model->fullscreen) {
	size = { sceneWidth, sceneHeight };
	origin = { sceneWidth / 2.0f, sceneHeight / 2.0f, 0.0f };
    }

    return size;
}

void CImage::updateScenePosition (
    const glm::vec3& origin, const glm::vec2& size, const glm::vec3& scale, float sceneWidth, float sceneHeight
) {
    // 3D scenes: image quads are plain world-space quads centered on their local origin;
    // origin/angles/scale (and the parent chain) are applied by the world matrix instead
    // of being baked into the vertices, and there is no screen-space y-flip
    if (this->getScene ().getScene ().camera.projection.isPerspective) {
	this->m_pos.x = -size.x / 2.0f;
	this->m_pos.z = size.x / 2.0f;
	this->m_pos.y = -size.y / 2.0f;
	this->m_pos.w = size.y / 2.0f;
	return;
    }

    const glm::vec2 scaledSize = size * glm::vec2 (scale);
    this->m_pos.x = origin.x - (scaledSize.x / 2.0f);
    this->m_pos.w = origin.y + (scaledSize.y / 2.0f);
    this->m_pos.z = origin.x + (scaledSize.x / 2.0f);
    this->m_pos.y = origin.y - (scaledSize.y / 2.0f);

    if (this->getImage ().alignment.find ("top") != std::string::npos) {
	this->m_pos.y -= scaledSize.y / 2.0f;
	this->m_pos.w -= scaledSize.y / 2.0f;
    } else if (this->getImage ().alignment.find ("bottom") != std::string::npos) {
	this->m_pos.y += scaledSize.y / 2.0f;
	this->m_pos.w += scaledSize.y / 2.0f;
    }

    if (this->getImage ().alignment.find ("left") != std::string::npos) {
	this->m_pos.x += scaledSize.x / 2.0f;
	this->m_pos.z += scaledSize.x / 2.0f;
    } else if (this->getImage ().alignment.find ("right") != std::string::npos) {
	this->m_pos.x -= scaledSize.x / 2.0f;
	this->m_pos.z -= scaledSize.x / 2.0f;
    }

    this->m_pos.x -= sceneWidth / 2.0f;
    this->m_pos.y = sceneHeight / 2.0f - this->m_pos.y;
    this->m_pos.z -= sceneWidth / 2.0f;
    this->m_pos.w = sceneHeight / 2.0f - this->m_pos.w;
}

void CImage::uploadGeometryBuffers (const glm::vec2& size) {
    const std::array<GLfloat, 18> sceneSpacePosition = {
	this->m_pos.x, this->m_pos.y, 0.0f, this->m_pos.x, this->m_pos.w, 0.0f, this->m_pos.z, this->m_pos.y, 0.0f,
	this->m_pos.z, this->m_pos.y, 0.0f, this->m_pos.x, this->m_pos.w, 0.0f, this->m_pos.z, this->m_pos.w, 0.0f
    };

    float width = 1.0f;
    float height = 1.0f;
    if (this->getTexture () != nullptr && !this->getTexture ()->isAnimated ()
	&& (this->getTexture ()->getTextureWidth (0) != this->getTexture ()->getRealWidth ()
	    || this->getTexture ()->getTextureHeight (0) != this->getTexture ()->getRealHeight ())) {
	width = static_cast<float> (this->getTexture ()->getRealWidth ())
	    / static_cast<float> (this->getTexture ()->getTextureWidth (0));
	height = static_cast<float> (this->getTexture ()->getRealHeight ())
	    / static_cast<float> (this->getTexture ()->getTextureHeight (0));
    }

    float x = 0.0f;
    float y = 0.0f;
    GLfloat realWidth = size.x;
    GLfloat realHeight = size.y;
    GLfloat realX = 0.0f;
    GLfloat realY = 0.0f;

    if (this->getImage ().model->passthrough) {
	width = 1.0f;
	height = 1.0f;
	realX = this->m_pos.x;
	realY = this->m_pos.w;
	realWidth = this->m_pos.z;
	realHeight = this->m_pos.y;

	if (this->getImage ().model->fullscreen) {
	    realX = -1.0f;
	    realY = -1.0f;
	    realWidth = 1.0f;
	    realHeight = 1.0f;
	}
    }

    const std::array<GLfloat, 12> texcoordCopy = { x, height, x, y, width, height, width, height, x, y, width, y };
    const std::array<GLfloat, 18> copySpacePosition
	= { realX,     realHeight, 0.0f, realX, realY, 0.0f, realWidth, realHeight, 0.0f,
	    realWidth, realHeight, 0.0f, realX, realY, 0.0f, realWidth, realY,      0.0f };

    const auto uploadIfChanged = [this] (GLuint buffer, const auto& values, auto& cached) {
	if (this->m_geometryBufferCacheValid && values == cached) {
	    return;
	}

	glBindBuffer (GL_ARRAY_BUFFER, buffer);
	glBufferData (GL_ARRAY_BUFFER, sizeof (values), values.data (), GL_DYNAMIC_DRAW);
	cached = values;
    };

    uploadIfChanged (this->m_sceneSpacePosition, sceneSpacePosition, this->m_cachedSceneSpacePosition);
    uploadIfChanged (this->m_copySpacePosition, copySpacePosition, this->m_cachedCopySpacePosition);
    uploadIfChanged (this->m_texcoordCopy, texcoordCopy, this->m_cachedTexcoordCopy);
    this->m_geometryBufferCacheValid = true;

    this->m_sceneCenter
	= glm::vec3 ((this->m_pos.x + this->m_pos.z) / 2.0f, (this->m_pos.y + this->m_pos.w) / 2.0f, 0.0f);
    this->m_modelViewProjectionCopy = this->getImage ().model->passthrough
	? this->m_modelViewProjectionScreen
	: glm::ortho<float> (0.0, size.x, 0.0, size.y);
    this->m_modelViewProjectionCopyInverse = glm::inverse (this->m_modelViewProjectionCopy);
    this->m_modelMatrix = glm::ortho<float> (0.0, size.x, 0.0, size.y);
}

CImage::ResolvedTransform CImage::updateGeometryBuffers () {
    auto sceneWidth = static_cast<float> (this->getScene ().getWidth ());
    auto sceneHeight = static_cast<float> (this->getScene ().getHeight ());
    const auto transform = this->resolveTransform (this->getImage ());
    glm::vec3 origin = transform.origin;
    const glm::vec3 scale = transform.scale;
    const glm::vec2 size = this->resolveGeometrySize (sceneWidth, sceneHeight, origin);
    this->m_size = size;
    this->updateScenePosition (origin, size, scale, sceneWidth, sceneHeight);
    this->uploadGeometryBuffers (size);

    // puppet positions depend on the freshly updated scene quad (m_pos) and the animation time,
    // so re-skin after the scene position is known
    if (this->m_hasPuppetMesh) {
	this->updatePuppetPositionBuffer (size);
    }
    return transform;
}

void CImage::updateScreenSpacePosition () {
    const ResolvedTransform transform = this->updateGeometryBuffers ();

    // 3D scenes: the world matrix carries the full transform chain and the camera provides
    // a real perspective view; 2D-only concerns (scene-center rotation, mouse parallax)
    // do not apply
    if (this->getScene ().getScene ().camera.projection.isPerspective) {
	const glm::mat4 world = this->resolveWorldMatrix ();
	const glm::mat4 viewProjection
	    = this->getScene ().getCamera ().getProjection () * this->getScene ().getCamera ().getLookAt ();
	const PerspectiveSceneMatrices matrices = calculatePerspectiveSceneMatrices (world, viewProjection);

	this->m_sceneModelMatrix = matrices.model;
	this->m_sceneViewProjectionMatrix = matrices.viewProjection;
	this->m_sceneNormalModelMatrix = matrices.normalModel;
	this->m_modelViewProjectionScreen = matrices.modelViewProjection;
	this->m_modelViewProjectionScreenInverse = glm::inverse (this->m_modelViewProjectionScreen);

	if (this->getImage ().model->passthrough) {
	    this->m_modelViewProjectionCopy = this->m_modelViewProjectionScreen;
	    this->m_modelViewProjectionCopyInverse = this->m_modelViewProjectionScreenInverse;
	}

	return;
    }

    // Build rotation from angles (already in radians from scene.json — see CParticle.cpp:2119)
    // Negate X and Z rotations to account for Y-flipped coordinate system (CParticle.cpp:2120)
    // all three axes are resolved through the parent chain by resolveTransform (PR #479)
    const glm::vec3 angles = transform.angles;
    glm::mat4 rotModel = glm::mat4 (1.0f);
    if (glm::dot (angles, angles) > 0.0f) {
	rotModel = glm::translate (rotModel, this->m_sceneCenter);
	rotModel = glm::rotate (rotModel, -angles.z, glm::vec3 (0.0f, 0.0f, 1.0f));
	rotModel = glm::rotate (rotModel, angles.y, glm::vec3 (0.0f, 1.0f, 0.0f));
	rotModel = glm::rotate (rotModel, angles.x, glm::vec3 (-1.0f, 0.0f, 0.0f));
	rotModel = glm::translate (rotModel, -this->m_sceneCenter);

	// same rotation as rotModel above, without the scene-center translate: a direction-only
	// local->world matrix for effect shaders (e.g. depthparallax) that rotate the parallax
	// input into the layer's own axes via g_EffectTextureProjectionMatrixInverse
	this->m_effectTextureProjectionMatrix = glm::mat4 (1.0f);
	this->m_effectTextureProjectionMatrix
	    = glm::rotate (this->m_effectTextureProjectionMatrix, -angles.z, glm::vec3 (0.0f, 0.0f, 1.0f));
	this->m_effectTextureProjectionMatrix
	    = glm::rotate (this->m_effectTextureProjectionMatrix, angles.y, glm::vec3 (0.0f, 1.0f, 0.0f));
	this->m_effectTextureProjectionMatrix
	    = glm::rotate (this->m_effectTextureProjectionMatrix, angles.x, glm::vec3 (-1.0f, 0.0f, 0.0f));
	// pure rotation matrix: inverse == transpose, cheaper and numerically exact
	this->m_effectTextureProjectionMatrixInverse = glm::transpose (this->m_effectTextureProjectionMatrix);
    } else {
	this->m_effectTextureProjectionMatrix = glm::mat4 (1.0f);
	this->m_effectTextureProjectionMatrixInverse = glm::mat4 (1.0f);
    }

    glm::mat4 mvp = this->getScene ().getCamera ().getProjection () * this->getScene ().getCamera ().getLookAt ();

    // The 2D geometry buffer stores authoring-space X/Y only. Preserve the
    // resolved Z origin in the model transform so tilted image layers remain at
    // their authored depth instead of being forced onto the camera plane.
    mvp = glm::translate (mvp, { 0.0f, 0.0f, transform.origin.z });

    // Apply parallax displacement if enabled — folded into the matrix before the rotation,
    // so the offset stays in scene space instead of being rotated with the object (PR #479)
    // fullscreen layers always cover the projection, so parallax never moves them.
    // locktransforms does NOT exclude a layer: it's an editor-UI lock, and wallpapers like
    // 3135984503 author every layer locked with real parallax depths; layers that shouldn't
    // move simply have parallaxDepth 0 (e.g. the background in 2665939987)
    const bool excludedFromParallax = this->getImage ().model->fullscreen;
    if (this->getScene ().getScene ().camera.parallax.enabled->value->getBool ()
	&& !this->getScene ().getContext ().getApp ().getContext ().settings.mouse.disableparallax) {
	const float parallaxAmount = this->getScene ().getScene ().camera.parallax.amount->value->getFloat ();
	const glm::vec2 depth = this->resolveParallaxDepth ();
	const glm::vec2* displacement = this->getScene ().getParallaxDisplacement ();
	// displacement is normalized per-axis ([-1,1] across width for x, across height for
	// y), so each axis converts to pixels with its own extent — using width for y would
	// overshoot the vertical travel by the aspect ratio and reveal the clear color
	const float span = excludedFromParallax ? 0.0f : Wallpapers::CScene::PARALLAX_TRANSLATION_SPAN;
	const float referenceX = static_cast<float> (this->getScene ().getWidth ()) * span;
	const float referenceY = static_cast<float> (this->getScene ().getHeight ()) * span;
	// x is negated: panning the camera towards the cursor shifts positive-depth
	// layers the opposite way on screen; the y displacement already accounts for
	// this through the viewport UV flip
	float x = -depth.x * parallaxAmount * displacement->x * referenceX;
	float y = depth.y * parallaxAmount * displacement->y * referenceY;
	mvp = glm::translate (mvp, { x, y, 0.0f });
    }

    mvp *= rotModel;

    this->m_modelViewProjectionScreen = mvp;
    this->m_modelViewProjectionScreenInverse = glm::inverse (mvp);
    if (this->getImage ().model->passthrough) {
	this->m_modelViewProjectionCopy = this->m_modelViewProjectionScreen;
	this->m_modelViewProjectionCopyInverse = this->m_modelViewProjectionScreenInverse;
    }
}

auto CImage::calculatePerspectiveSceneMatrices (const glm::mat4& world, const glm::mat4& viewProjection)
    -> PerspectiveSceneMatrices {
    return PerspectiveSceneMatrices {
	.model = world,
	.viewProjection = viewProjection,
	.modelViewProjection = viewProjection * world,
	.normalModel = glm::inverseTranspose (glm::mat3 (world)),
    };
}

const Image& CImage::getImage () const { return this->m_image; }

bool CImage::isCompositionLayer () const {
    return this->m_image.model != nullptr
	&& this->m_image.model->filename == "models/util/composelayer.json";
}

bool CImage::copiesCompositionBackground () const { return this->m_image.copyBackground; }

std::shared_ptr<const CFBO> CImage::getCompositionFBO () const { return this->m_compositionFBO; }

glm::vec2 CImage::getSize () const {
    if (this->m_size.x > 0.0f && this->m_size.y > 0.0f) {
	return this->m_size;
    }
    if (this->m_texture == nullptr) {
	return this->getImage ().size;
    }

    return { this->m_texture->getRealWidth (), this->m_texture->getRealHeight () };
}

std::optional<glm::mat4> CImage::getAttachmentTransform (const std::string& name) const {
    return MdlAnimationEvaluator::attachmentTransform (this->m_puppetAnimation, this->m_puppetWorldBones, name);
}

std::optional<size_t> CImage::getAttachmentIndex (const std::string& name) const {
    size_t index = 0;
    for (const auto& attachmentName : this->m_puppetAnimation.attachments | std::views::keys) {
	if (attachmentName == name) {
	    return index;
	}
	index++;
    }
    return std::nullopt;
}

std::optional<std::string> CImage::getAttachmentName (const size_t requestedIndex) const {
    size_t index = 0;
    for (const auto& attachmentName : this->m_puppetAnimation.attachments | std::views::keys) {
	if (index++ == requestedIndex) {
	    return attachmentName;
	}
    }
    return std::nullopt;
}

std::optional<glm::vec3> CImage::cursorLocalPosition (const glm::vec3& worldPosition) const {
    if (!this->m_image.visible->value->getBool () || !this->isVisibleThroughParents ()) {
	return std::nullopt;
    }

    const glm::mat4 world = this->resolveWorldMatrix ();
    const float determinant = glm::determinant (world);
    if (!std::isfinite (determinant) || std::abs (determinant) < 1e-8f) {
	return std::nullopt;
    }

    const glm::vec4 local4 = glm::inverse (world) * glm::vec4 (worldPosition, 1.0f);
    const glm::vec3 local = glm::vec3 (local4);
    const glm::vec2 size = this->getSize ();

    float left = -size.x * 0.5f;
    float right = size.x * 0.5f;
    float bottom = -size.y * 0.5f;
    float top = size.y * 0.5f;
    if (this->m_image.alignment.find ("left") != std::string::npos) {
	left = 0.0f;
	right = size.x;
    } else if (this->m_image.alignment.find ("right") != std::string::npos) {
	left = -size.x;
	right = 0.0f;
    }
    if (this->m_image.alignment.find ("top") != std::string::npos) {
	bottom = -size.y;
	top = 0.0f;
    } else if (this->m_image.alignment.find ("bottom") != std::string::npos) {
	bottom = 0.0f;
	top = size.y;
    }

    if (local.x < left || local.x > right || local.y < bottom || local.y > top) {
	return std::nullopt;
    }
    return local;
}

GLuint CImage::getSceneSpacePosition () const { return this->m_sceneSpacePosition; }

GLuint CImage::getCopySpacePosition () const { return this->m_copySpacePosition; }

GLuint CImage::getPassSpacePosition () const { return this->m_passSpacePosition; }

GLuint CImage::getTexCoordCopy () const { return this->m_texcoordCopy; }

GLuint CImage::getTexCoordPass () const { return this->m_texcoordPass; }
