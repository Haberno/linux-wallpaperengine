#pragma once
#include "quickjs.h"

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}
namespace WallpaperEngine::Scripting {
class ScriptEngine;
class SceneObject {
public:
    SceneObject (ScriptEngine& engine, Render::Wallpapers::CScene& scene);
    ~SceneObject ();

    const Render::Wallpapers::CScene& getScene () const { return m_scene; }
    Render::Wallpapers::CScene& getScene () { return m_scene; }
    JSValue getInstance () const { return m_instance; }
    ScriptEngine& getEngine () const { return m_engine; }
    JSValue makeModelDataHandle (uint32_t token);

private:
    Render::Wallpapers::CScene& m_scene;
    ScriptEngine& m_engine;

    JSClassID m_classId;
    JSClassID m_modelDataClassId = 0;
    JSClassDef m_definition;
    JSValue m_instance;
};
}