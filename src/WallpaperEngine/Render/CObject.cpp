#include "CObject.h"

#include <utility>

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Render::Wallpapers;

CObject::CObject (Wallpapers::CScene& scene, const Object& object) :
    Helpers::ContextAware (scene), m_scene (scene), m_object (object) { }

void CObject::setup () { }
void CObject::render () { }

Wallpapers::CScene& CObject::getScene () const { return this->m_scene; }

const AssetLocator& CObject::getAssetLocator () const { return this->getScene ().getAssetLocator (); }

int CObject::getId () const { return this->m_object.id; }

const Object& CObject::getObject () const { return this->m_object; }

glm::vec2 CObject::resolveParallaxDepth () const {
    constexpr int kMaxParentDepth = 32;
    const Object* current = &this->m_object;

    for (int depth = 0; current != nullptr && depth <= kMaxParentDepth; depth++) {
	if (current->authoredParallaxDepth.has_value ()) {
	    return *current->authoredParallaxDepth;
	}

	if (!current->parent.has_value ()) {
	    break;
	}

	const auto* parentObject = this->m_scene.getObject (current->parent.value ());
	current = parentObject != nullptr ? &parentObject->getObject () : nullptr;
    }

    return glm::vec2 (1.0f);
}