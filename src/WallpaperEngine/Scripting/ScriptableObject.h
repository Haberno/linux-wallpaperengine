#pragma once
#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/Render/CObject.h"

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Scripting {
class ScriptableObject : virtual public CObject {
public:
    struct PropertyEntry {
	std::string key;
	DynamicValue& value;
    };

    ScriptableObject (Wallpapers::CScene& scene, const Object& object);
    /** Drops this object's queued scripts, which hold a reference to it. */
    ~ScriptableObject () override;

    DynamicValue& getProperty (const std::string& name);

    const std::map<std::string, PropertyEntry>& getProperties () const;
    const std::map<std::string, PropertyAnimation*>& getAnimations () const { return m_animations; }

protected:
    void registerProperty (const std::string& name, DynamicValue& value);
    void registerProperty (const std::string& name, const UserSetting& setting);

private:
    std::map<std::string, PropertyEntry> m_properties;
    std::map<std::string, PropertyAnimation*> m_animations;
};
}
