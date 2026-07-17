#include "CImage.h"

#include "CRenderable.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <optional>
#include <sstream>

#include <glm/glm.hpp>
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

struct PuppetMeshBlock {
    size_t headerOffset = 0;
    uint32_t vertexBytes = 0;
    uint32_t indexBytes = 0;
};

struct PuppetVertexLayout {
    uint32_t formatMask = 0;
    size_t stride = 0;
    size_t blendIndicesOffset = 0;
    size_t uvOffset = 0;
};

template <typename T> T puppetRead (const std::vector<char>& data, size_t& offset) {
    if (offset + sizeof (T) > data.size ()) {
	throw std::runtime_error ("puppet data ends unexpectedly");
    }

    T value;
    std::memcpy (&value, data.data () + offset, sizeof (value));
    offset += sizeof (value);
    return value;
}

size_t findPuppetSection (const std::vector<char>& data, const char* marker, size_t from) {
    const size_t markerLength = strlen (marker);
    constexpr size_t sectionHeaderLength = 9;
    for (size_t offset = from; offset + sectionHeaderLength <= data.size (); offset++) {
	if (std::memcmp (data.data () + offset, marker, markerLength) == 0
	    && std::all_of (
		data.begin () + static_cast<ptrdiff_t> (offset + markerLength),
		data.begin () + static_cast<ptrdiff_t> (offset + sectionHeaderLength - 1),
		[] (const char value) { return value >= '0' && value <= '9'; }
	    )
	    && data[offset + sectionHeaderLength - 1] == '\0') {
	    return offset;
	}
    }
    return data.size ();
}

// The official loader uses the serialized vertex-format mask, not the MDLV version,
// to calculate the record stride. Scan for a known mask whose vertex/index byte
// lengths are also self-consistent; this accepts versions such as MDLV0019 without
// treating an unknown format as the newest layout.
std::optional<PuppetMeshBlock> findPuppetMeshBlock (
    const std::vector<char>& data, size_t markerSize, size_t mdlsOffset, const PuppetVertexLayout& layout
) {
    constexpr size_t meshHeaderSize = sizeof (uint32_t) * 2;
    for (size_t offset = markerSize; offset + meshHeaderSize + sizeof (uint32_t) < mdlsOffset; offset++) {
	size_t cursor = offset;
	const auto candidateFormatMask = puppetRead<uint32_t> (data, cursor);
	if (candidateFormatMask != layout.formatMask) {
	    continue;
	}
	const auto candidateVertexBytes = puppetRead<uint32_t> (data, cursor);
	const size_t verticesOffset = offset + meshHeaderSize;
	const size_t indexLengthOffset = verticesOffset + candidateVertexBytes;

	if (candidateVertexBytes == 0 || candidateVertexBytes % layout.stride != 0
	    || indexLengthOffset + sizeof (uint32_t) > mdlsOffset) {
	    continue;
	}

	cursor = indexLengthOffset;
	const auto candidateIndexBytes = puppetRead<uint32_t> (data, cursor);
	if (candidateIndexBytes == 0 || candidateIndexBytes % (sizeof (uint16_t) * 3) != 0
	    || cursor + candidateIndexBytes > mdlsOffset) {
	    continue;
	}

	return PuppetMeshBlock { .headerOffset = offset,
				 .vertexBytes = candidateVertexBytes,
				 .indexBytes = candidateIndexBytes };
    }

    return std::nullopt;
}

}

CImage::ResolvedTransform CImage::localTransform (const Object& object, float time) {
    glm::vec3 origin = object.origin->value->getVec3 ();

    // keyframed origin animations (e.g. the train moving across workshop scene 2488626583)
    if (object.origin->animation != nullptr) {
	origin = object.origin->animation->evaluateVec3 (origin, time);
    }
    glm::vec3 scale = glm::vec3 (1.0f);
    glm::vec3 angles = glm::vec3 (0.0f);

    if (object.is<Image> ()) {
	const auto* image = object.as<Image> ();
	scale = image->scale->value->getVec3 ();
	angles = image->angles->value->getVec3 ();
    } else if (object.is<Text> ()) {
	const auto* text = object.as<Text> ();
	scale = text->scale->value->getVec3 ();
    } else {
	scale = object.groupScale->value->getVec3 ();
	angles = object.groupAngles->value->getVec3 ();
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
}

bool CImage::loadPuppetMesh (const glm::vec2& size) {
    if (!this->getImage ().model->puppet.has_value ()) {
	return false;
    }

    try {
	const auto stream = this->getScene ().getScene ().project.assetLocator->read (*this->getImage ().model->puppet);
	std::vector<char> data { std::istreambuf_iterator<char> (*stream), std::istreambuf_iterator<char> () };

	constexpr size_t markerSize = 9;
	constexpr size_t positionOffset = 0;
	constexpr std::array vertexLayouts {
	    // Newer weighted layout with one additional packed vertex field. Observed
	    // in MDLV0023 files that also carry MDMP/MDLE sections.
	    PuppetVertexLayout { .formatMask = 0x0181000e, .stride = 84, .blendIndicesOffset = 44, .uvOffset = 76 },
	    // Current weighted puppet layout. Observed in MDLV0017/0019/0021/0023.
	    PuppetVertexLayout { .formatMask = 0x0180000f, .stride = 80, .blendIndicesOffset = 40, .uvOffset = 72 },
	    // Compact weighted layout as serialized by MDLV0016.
	    PuppetVertexLayout { .formatMask = 0x01800009, .stride = 52, .blendIndicesOffset = 12, .uvOffset = 44 },
	    // Legacy compact weighted layout. Observed in MDLV0013/0014.
	    PuppetVertexLayout { .formatMask = 0x00000000, .stride = 52, .blendIndicesOffset = 12, .uvOffset = 44 },
	};

	const std::string puppetVersion
	    = data.size () >= markerSize ? std::string (data.data (), strlen ("MDLV0021")) : "";
	if (!puppetVersion.starts_with ("MDLV00")) {
	    sLog.error ("Unsupported puppet model header ", puppetVersion, " in ", *this->getImage ().model->puppet);
	    return false;
	}
	const size_t mdlsOffset = findPuppetSection (data, "MDLS", markerSize);
	const PuppetVertexLayout* vertexLayout = nullptr;
	std::optional<PuppetMeshBlock> meshBlock;
	for (const auto& candidate : vertexLayouts) {
	    meshBlock = findPuppetMeshBlock (data, markerSize, mdlsOffset, candidate);
	    if (meshBlock.has_value ()) {
		vertexLayout = &candidate;
		break;
	    }
	}
	if (!meshBlock.has_value ()) {
	    sLog.error (
		"Unsupported or malformed puppet vertex format ", puppetVersion, " in ",
		*this->getImage ().model->puppet
	    );
	    return false;
	}

	const size_t vertexCount = meshBlock->vertexBytes / vertexLayout->stride;
	const size_t verticesOffset = meshBlock->headerOffset + sizeof (uint32_t) * 2;
	const size_t indicesOffset = verticesOffset + meshBlock->vertexBytes + sizeof (uint32_t);
	const size_t indexCount = meshBlock->indexBytes / sizeof (uint16_t);
	std::vector<GLfloat> texcoords;
	std::vector<GLushort> indices;

	this->m_puppetRawPositions.clear ();
	this->m_puppetRawPositions.reserve (vertexCount * 3);
	this->m_puppetBlendIndices.clear ();
	this->m_puppetBlendIndices.reserve (vertexCount * 4);
	this->m_puppetBlendWeights.clear ();
	this->m_puppetBlendWeights.reserve (vertexCount * 4);
	texcoords.reserve (vertexCount * 2);
	indices.reserve (indexCount);

	for (size_t index = 0; index < vertexCount; index++) {
	    const size_t vertexOffset = verticesOffset + index * vertexLayout->stride;
	    size_t cursor = vertexOffset + positionOffset;
	    const glm::vec3 position {
		puppetRead<float> (data, cursor),
		puppetRead<float> (data, cursor),
		puppetRead<float> (data, cursor),
	    };
	    if (!std::isfinite (position.x) || !std::isfinite (position.y) || !std::isfinite (position.z)) {
		throw std::runtime_error ("puppet vertex contains a non-finite position");
	    }
	    this->m_puppetRawPositions.insert (
		this->m_puppetRawPositions.end (), { position.x, position.y, position.z }
	    );
	    cursor = vertexOffset + vertexLayout->blendIndicesOffset;
	    for (int component = 0; component < 4; component++) {
		this->m_puppetBlendIndices.push_back (puppetRead<uint32_t> (data, cursor));
	    }
	    for (int component = 0; component < 4; component++) {
		const float weight = puppetRead<float> (data, cursor);
		if (!std::isfinite (weight) || weight < 0.0f) {
		    throw std::runtime_error ("puppet vertex contains an invalid blend weight");
		}
		this->m_puppetBlendWeights.push_back (weight);
	    }
	    cursor = vertexOffset + vertexLayout->uvOffset;
	    const float u = puppetRead<float> (data, cursor);
	    const float v = puppetRead<float> (data, cursor);
	    if (!std::isfinite (u) || !std::isfinite (v)) {
		throw std::runtime_error ("puppet vertex contains a non-finite texture coordinate");
	    }
	    texcoords.push_back (u);
	    texcoords.push_back (v);
	}

	size_t indexCursor = indicesOffset;
	for (size_t index = 0; index < indexCount; index++) {
	    const auto value = puppetRead<uint16_t> (data, indexCursor);
	    if (value >= vertexCount) {
		sLog.error ("Invalid puppet mesh index ", value, " in ", *this->getImage ().model->puppet);
		return false;
	    }
	    indices.push_back (value);
	}

	// MDLV vertices are already assembled even when their texture UVs point into a parts
	// atlas. The shared animation parser supplies MDLS bones, MDAT attachments, and MDLA clips.
	try {
	    this->m_puppetAnimation = MdlAnimationParser::parse (data, *this->getImage ().model->puppet);
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
	    "Loaded puppet mesh ", *this->getImage ().model->puppet, " version=", puppetVersion, " format=0x", std::hex,
	    vertexLayout->formatMask, std::dec, " stride=", vertexLayout->stride, " vertices=", vertexCount,
	    " indices=", this->m_puppetIndexCount
	);

	return true;
    } catch (const std::exception& ex) {
	sLog.error ("Could not load puppet mesh ", *this->getImage ().model->puppet, ": ", ex.what ());
	return false;
    }
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
    // embedded animation. Authored layers remain in the list at zero weight so
    // their frame-zero assembled pose can still serve as the visual baseline.
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

std::optional<CImage::ResolvedTransform> CImage::puppetAttachmentTransform (const std::string& name) const {
    const auto model
	= MdlAnimationEvaluator::attachmentTransform (this->m_puppetAnimation, this->m_puppetWorldBones, name);
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
	[this] () {
	    // the Y-flip into scene space inverts triangle winding, so materials with
	    // cullmode "normal" would cull the whole mesh (e.g. the eye layer in 3558034522)
	    const GLboolean cullFaceEnabled = glIsEnabled (GL_CULL_FACE);
	    glDisable (GL_CULL_FACE);
	    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, this->m_puppetIndices);
	    glDrawElements (GL_TRIANGLES, this->m_puppetIndexCount, GL_UNSIGNED_SHORT, nullptr);
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

    // copy pass to the composite layer
    for (const auto& cur : this->getImage ().model->material->passes) {
	this->m_passes.push_back (
	    new CPass (*this, std::make_shared<FBOProvider> (this), *cur, std::nullopt, std::nullopt, std::nullopt)
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
	this->m_materials.compatibilityMaterials.emplace_back (
	    MaterialParser::load (this->getScene ().getScene ().project, "materials/util/effectpassthrough.json")
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

    // if there's more than one pass the blendmode has to be moved from the beginning to the end
    if (this->m_passes.size () > 1) {
	const auto first = this->m_passes.begin ();
	const auto last = this->m_passes.rbegin ();

	(*last)->setBlendingMode ((*first)->getBlendingMode ());
	(*first)->setBlendingMode (BlendingMode_Normal);
    }

    CRenderable::setup ();

    this->setupPasses ();
    this->m_initialized = true;
}

void CImage::setupPasses () {
    // do a pass on everything and setup proper inputs and values
    std::shared_ptr<const CFBO> drawTo = this->m_currentMainFBO;
    std::shared_ptr<const TextureProvider> asInput = this->getTexture ();
    GLuint texcoord = this->getTexCoordCopy ();

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
	    }

	    // puppet warp deforms the final on-screen geometry only; every earlier pass works on
	    // the untouched image so effect masks keep lining up with it in the intermediate FBOs
	    if (this->m_hasPuppetMesh) {
		this->setupPuppetGeometryCallback (pass, isFirstPass);
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

    if (!this->getImage ().visible->value->getBool ()) {
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
    this->m_resolvedAlpha = this->m_image.alpha->value->getFloat ();
    if (this->m_image.alpha->animation != nullptr) {
	this->m_resolvedAlpha
	    = this->m_image.alpha->animation->evaluateFloat (this->m_resolvedAlpha, this->getScene ().getTime ());
    }

    glColorMask (true, true, true, true);

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
	    glColorMask (true, true, true, false);
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

	this->m_modelViewProjectionScreen
	    = this->getScene ().getCamera ().getProjection () * this->getScene ().getCamera ().getLookAt () * world;
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

const Image& CImage::getImage () const { return this->m_image; }

glm::vec2 CImage::getSize () const {
    if (this->m_size.x > 0.0f && this->m_size.y > 0.0f) {
	return this->m_size;
    }
    if (this->m_texture == nullptr) {
	return this->getImage ().size;
    }

    return { this->m_texture->getRealWidth (), this->m_texture->getRealHeight () };
}

GLuint CImage::getSceneSpacePosition () const { return this->m_sceneSpacePosition; }

GLuint CImage::getCopySpacePosition () const { return this->m_copySpacePosition; }

GLuint CImage::getPassSpacePosition () const { return this->m_passSpacePosition; }

GLuint CImage::getTexCoordCopy () const { return this->m_texcoordCopy; }

GLuint CImage::getTexCoordPass () const { return this->m_texcoordPass; }
