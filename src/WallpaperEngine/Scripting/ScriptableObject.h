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
	const UserSetting* setting = nullptr;
    };

    ScriptableObject (Wallpapers::CScene& scene, const Object& object);
    /** Drops this object's queued scripts, which hold a reference to it. */
    ~ScriptableObject () override;

    DynamicValue& getProperty (const std::string& name);
    [[nodiscard]] const UserSetting* getPropertySetting (const std::string& name) const;

    const std::map<std::string, PropertyEntry>& getProperties () const;
    const std::map<std::string, PropertyAnimation*>& getAnimations () const { return m_animations; }
    /** Synchronize playback and retire finished layers before event iteration starts. */
    virtual void prepareAnimationEvents () { }

protected:
    void registerProperty (const std::string& name, DynamicValue& value);
    void registerProperty (const std::string& name, const UserSetting& setting);
    void registerAnimation (const std::string& key, PropertyAnimation& animation) {
	m_animations.insert_or_assign (key, &animation);
    }
    void unregisterAnimation (const std::string& key) { m_animations.erase (key); }

private:
    std::map<std::string, PropertyEntry> m_properties;
    std::map<std::string, PropertyAnimation*> m_animations;
};
}
