#pragma once

#include "ObjectAdapter.h"
#include "WallpaperEngine/Data/Model/Types.h"
#include <array>

namespace WallpaperEngine::Scripting::Adapters {
template <int components> class VectorAdapter : public ObjectAdapter {
public:
    explicit VectorAdapter (ScriptEngine& engine);
    ~VectorAdapter () override;

    int length () { return components; }
    bool isInstance (JSValueConst value) const { return JS_GetClassID (value) == m_classId; }
    int componentIndex (JSAtom atom) const {
        for (int index = 0; index < components; ++index) {
            if (m_componentAtoms[index] == atom) return index;
        }
        return -1;
    }
    JSValue instantiate (Data::Model::DynamicValue& value) override;
    JSValue instantiate (ScriptableObject& object) override;
    /**
     * @return A new, anonymous JSValue representing the vector
     */
    JSValue instantiate (Data::Model::DynamicValue& source, bool temporal);
    JSValue instantiate ();

private:
    JSValue m_prototype;
    uint32_t m_instanceId;
    std::string m_name;
    JSClassExoticMethods m_exoticMethods;
    std::array<JSAtom, components> m_componentAtoms;
};

extern template class VectorAdapter<2>;
extern template class VectorAdapter<3>;
extern template class VectorAdapter<4>;
}
