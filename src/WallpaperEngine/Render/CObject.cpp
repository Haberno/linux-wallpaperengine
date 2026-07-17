#include "CObject.h"

#include <glm/gtc/matrix_transform.hpp>
#include <utility>

#include "WallpaperEngine/Data/Model/Object.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Render::Wallpapers;

namespace {
RenderSortClass classifyMaterial (const Material& material) {
    RenderSortClass result = RenderSortClass::Opaque;
    for (const auto& pass : material.passes) {
	if (pass->blending == BlendingMode_Additive) {
	    return RenderSortClass::Additive;
	}
	if (pass->blending == BlendingMode_Translucent) {
	    result = RenderSortClass::Translucent;
	}
    }
    return result;
}

glm::mat4 localMatrix (const Object& object, const float time) {
    glm::vec3 origin = object.origin->value->getVec3 ();

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
	scale = object.as<Text> ()->scale->value->getVec3 ();
    } else {
	scale = object.groupScale->value->getVec3 ();
	angles = object.groupAngles->value->getVec3 ();
    }

    glm::mat4 local = glm::translate (glm::mat4 (1.0f), origin);
    // same rotation order and radian units as rotateVec3 in CImage.cpp
    local = glm::rotate (local, angles.z, glm::vec3 (0.0f, 0.0f, 1.0f));
    local = glm::rotate (local, angles.y, glm::vec3 (0.0f, 1.0f, 0.0f));
    local = glm::rotate (local, angles.x, glm::vec3 (1.0f, 0.0f, 0.0f));
    return glm::scale (local, scale);
}
} // namespace

CObject::CObject (Wallpapers::CScene& scene, const Object& object) :
    Helpers::ContextAware (scene), m_scene (scene), m_object (object) { }

void CObject::setup () { }
void CObject::render () { }

Wallpapers::CScene& CObject::getScene () const { return this->m_scene; }

const AssetLocator& CObject::getAssetLocator () const { return this->getScene ().getAssetLocator (); }

int CObject::getId () const { return this->m_object.id; }

const Object& CObject::getObject () const { return this->m_object; }

std::optional<glm::mat4> CObject::getAttachmentTransform (const std::string&) const { return std::nullopt; }

glm::vec2 CObject::resolveParallaxDepth () const {
    constexpr int kMaxParentDepth = 32;
    const Object* current = &this->m_object;

    // Wallpaper Engine applies camera parallax while rendering a root layer and then
    // renders the complete child subtree under that translated transform. Consequently,
    // only the root-most layer's depth controls the subtree; a child-authored depth does
    // not override its parent (3367988661 pins a date/clock subtree this way).
    for (int depth = 0; current->parent.has_value () && depth <= kMaxParentDepth; depth++) {
	const auto* parentObject = this->m_scene.getObject (current->parent.value ());
	if (parentObject == nullptr) {
	    break;
	}
	current = &parentObject->getObject ();
    }

    if (current->authoredParallaxDepth.has_value ()) {
	// Read the live typed property so user settings and SceneScript changes keep working.
	if (current->is<Image> ()) {
	    return current->as<Image> ()->parallaxDepth->value->getVec2 ();
	}
	if (current->is<Particle> ()) {
	    return current->as<Particle> ()->parallaxDepth->value->getVec2 ();
	}
	if (current->is<Text> ()) {
	    return current->as<Text> ()->parallaxDepth->value->getVec2 ();
	}
	return *current->authoredParallaxDepth;
    }

    // The official Layer constructor initializes parallaxDepth to 1,1. Older workshop
    // scenes often omit the field and depend on that default (Gojo 3100265648 and the
    // puppet/window layer in 3487328036). Explicit 0,0 is the authored pin value.
    return glm::vec2 (1.0f);
}

bool CObject::isVisibleThroughParents () const {
    constexpr int kMaxParentDepth = 32;
    const Object* current = &this->m_object;

    for (int depth = 0; current->parent.has_value () && depth <= kMaxParentDepth; depth++) {
	const auto* parentObject = this->m_scene.getObject (current->parent.value ());

	if (parentObject == nullptr) {
	    break;
	}

	const Object& parent = parentObject->getObject ();
	// image/text parents render from their own visible value; plain group containers
	// only carry the group fallback
	const auto& visible = parent.is<Image> () ? parent.as<Image> ()->visible
	    : parent.is<Text> ()                  ? parent.as<Text> ()->visible
						  : parent.groupVisible;

	if (visible != nullptr && visible->value != nullptr && !visible->value->getBool ()) {
	    return false;
	}

	current = &parent;
    }

    return true;
}

glm::mat4 CObject::resolveWorldMatrix () const {
    constexpr int kMaxParentDepth = 32;

    // walk leaf-first like resolveParallaxDepth, bounded to guard against cycles
    const Object* chain[kMaxParentDepth + 1];
    int count = 0;
    const Object* current = &this->m_object;
    chain[count++] = current;

    while (current->parent.has_value () && count <= kMaxParentDepth) {
	const auto* parentObject = this->m_scene.getObject (current->parent.value ());

	if (parentObject == nullptr) {
	    break;
	}

	current = &parentObject->getObject ();
	chain[count++] = current;
    }

    const float time = this->m_scene.getTime ();
    glm::mat4 world = glm::mat4 (1.0f);

    for (int i = count - 1; i >= 0; --i) {
	world = world * localMatrix (*chain[i], time);

	// Attachments live on the parent and are inserted between that parent's local
	// transform and the attached child's local transform.
	if (i > 0 && chain[i - 1]->attachment.has_value ()) {
	    const CObject* parent = this->m_scene.getObject (chain[i]->id);
	    if (parent != nullptr) {
		const auto attachment = parent->getAttachmentTransform (*chain[i - 1]->attachment);
		if (attachment.has_value ()) {
		    world *= *attachment;
		}
	    }
	}
    }

    return world;
}

RenderSortClass CObject::getRenderSortClass () const {
    const Object& object = this->getObject ();
    if (object.is<Model3D> ()) {
	RenderSortClass result = RenderSortClass::Opaque;
	for (const auto& material : object.as<Model3D> ()->materials) {
	    const RenderSortClass current = classifyMaterial (*material);
	    if (current == RenderSortClass::Additive) {
		return current;
	    }
	    if (current == RenderSortClass::Translucent) {
		result = current;
	    }
	}
	return result;
    }
    return RenderSortClass::Opaque;
}
