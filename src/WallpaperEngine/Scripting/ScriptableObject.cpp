#include "ScriptableObject.h"

#include "ScriptEngine.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"

#include <ranges>

using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Scripting;

ScriptableObject::~ScriptableObject () {
    // registerProperty queued a script per property, each holding this object by reference and by
    // raw pointer. CScene::dispatchObjectType deletes objects whose setup() throws, and
    // CScene::destroyObjects deletes every object while the engine is still alive, so both paths
    // would otherwise leave the engine calling hooks on freed memory.
    this->getScene ().getScriptEngine ().unregisterScriptable (this);
}

ScriptableObject::ScriptableObject (Wallpapers::CScene& scene, const Object& object) : CObject (scene, object) {
    // A queued module keeps a reference to its initial DynamicValue. Select the
    // concrete renderer's fields before queueing: Image/Particle/Text parse their
    // own transform settings separately from the generic group fallbacks.
    const auto* scale = object.groupScale.get ();
    const auto* angles = object.groupAngles.get ();
    const auto* visible = object.groupVisible.get ();
    if (object.is<Image> ()) {
	const auto* image = object.as<Image> ();
	scale = image->scale.get ();
	angles = image->angles.get ();
	visible = image->visible.get ();
    } else if (object.is<Particle> ()) {
	const auto* particle = object.as<Particle> ();
	scale = particle->scale.get ();
	angles = particle->angles.get ();
	visible = particle->visible.get ();
    } else if (object.is<Text> ()) {
	const auto* text = object.as<Text> ();
	scale = text->scale.get ();
	visible = text->visible.get ();
    }
    this->registerProperty ("origin", *object.origin);
    this->registerProperty ("scale", *scale);
    this->registerProperty ("angles", *angles);
    this->registerProperty ("visible", *visible);

    for (const auto& projection : scene.getScene ().camera.objectProjections) {
	if (projection.id != object.id) {
	    continue;
	}
	if (projection.fov != nullptr) {
	    this->registerProperty ("fov", *projection.fov);
	}
	if (projection.zoom != nullptr) {
	    this->registerProperty ("zoom", *projection.zoom);
	}
	break;
    }
}

DynamicValue& ScriptableObject::getProperty (const std::string& name) {
    const auto it = this->m_properties.find (name);

    if (it == this->m_properties.end ()) {
	sLog.exception ("Property '" + name + "' not found on object '" + this->getObject ().name + "'");
    }

    return it->second.value;
}

const std::map<std::string, ScriptableObject::PropertyEntry>& ScriptableObject::getProperties () const {
    return this->m_properties;
}

void ScriptableObject::registerProperty (const std::string& name, const UserSetting& setting) {
    registerProperty (name, *setting.value);
    if (setting.animation != nullptr && !setting.animation->name.empty ()) {
	m_animations.insert_or_assign (setting.animation->name, setting.animation.get ());
    }
}

void ScriptableObject::registerProperty (const std::string& name, DynamicValue& value) {
    // Derived renderers repeat some common registrations and add their own
    // properties. Both this map and the queued script must refer to the final
    // values consumed by the renderer.
    const std::string key = name + "_" + std::to_string (this->getId ());
    // PropertyEntry holds a reference member (not assignable), so drop any prior registration and
    // re-emplace to let the derived value win.
    this->m_properties.erase (name);
    const auto [it, inserted] = this->m_properties.emplace (name, PropertyEntry { .key = key, .value = value });

    // Re-registering the same value is safe: the keyed module is initialized only
    // once. The constructor must select the final value before its first queue.
    this->getScene ().getScriptEngine ().queueScript (it->second.key, it->second.value, *this);
}
