#include "ScriptableObject.h"

#include "ScriptEngine.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"

#include <ranges>

using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Scripting;

ScriptableObject::ScriptableObject (Wallpapers::CScene& scene, const Object& object) : CObject (scene, object) {
    // register common dynamic values
    this->registerProperty ("origin", *object.origin->value);
    this->registerProperty ("scale", *object.groupScale->value);
    this->registerProperty ("angles", *object.groupAngles->value);
    this->registerProperty ("visible", *object.groupVisible->value);
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

void ScriptableObject::registerProperty (const std::string& name, DynamicValue& value) {
    // Last registration wins. The base ScriptableObject registers the group/object fallbacks
    // (groupScale/groupAngles/groupVisible) first; a derived CImage/CText then re-registers the
    // same names with its ImageData/TextData values — and those are what the renderer actually
    // reads (localTransform uses image.scale/angles, render gates on image.visible). With a
    // first-wins emplace the script would drive the group values while the image rendered from the
    // unset image values, so thisLayer.scale/visible silently did nothing on image layers.
    const std::string key = name + "_" + std::to_string (this->getId ());
    // PropertyEntry holds a reference member (not assignable), so drop any prior registration and
    // re-emplace to let the derived value win.
    this->m_properties.erase (name);
    const auto [it, inserted] = this->m_properties.emplace (name, PropertyEntry { .key = key, .value = value });

    // queueScript is keyed and self-guards against duplicate keys, so re-registering the same name
    // re-points the property without spawning a second script module.
    this->getScene ().getScriptEngine ().queueScript (it->second.key, it->second.value, *this);
}
