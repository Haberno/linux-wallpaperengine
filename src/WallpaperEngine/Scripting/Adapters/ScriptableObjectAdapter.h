#pragma once

#include "ObjectAdapter.h"

namespace WallpaperEngine::Scripting::Adapters {
class ScriptableObjectAdapter : public ObjectAdapter {
public:
    explicit ScriptableObjectAdapter (ScriptEngine& engine, std::string name);

    JSValue instantiate (ScriptableObject& object) override;
    JSValue instantiate (Data::Model::DynamicValue& value) override;

    // Recover the underlying ScriptableObject from a JS layer value produced by instantiate(),
    // or nullptr if the value isn't one of ours. Used by scene-script layer APIs that take a
    // layer handle (getLayerIndex/sortLayer).
    static ScriptableObject* fromJS (JSValue value);

private:
    JSClassExoticMethods m_exoticMethods;
    std::string m_name;
};
}