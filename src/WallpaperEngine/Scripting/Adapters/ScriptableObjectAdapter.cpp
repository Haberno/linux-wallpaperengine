#include "ScriptableObjectAdapter.h"

#include <cstring>
#include <glm/glm.hpp>
#include <utility>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Utils;
using namespace WallpaperEngine::Scripting::Adapters;

#define SCRIPTABLE_OPAQUE_MAGIC 0xdeadbeef

struct OpaqueScriptableObjectAdapter {
    unsigned int magic;
    ScriptableObjectAdapter& adapter;
    WallpaperEngine::Scripting::ScriptableObject& object;
};

static JSValue scriptable_noop (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_UNDEFINED;
}

static JSValue scriptable_false (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_FALSE;
}

static JSValue scriptable_animation_stub (JSContext* ctx);

static JSValue scriptable_get_animation (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return scriptable_animation_stub (ctx);
}

static JSValue scriptable_animation_stub (JSContext* ctx) {
    JSValue result = JS_NewObject (ctx);
    JS_SetPropertyStr (ctx, result, "play", JS_NewCFunction (ctx, scriptable_noop, "play", 0));
    JS_SetPropertyStr (ctx, result, "pause", JS_NewCFunction (ctx, scriptable_noop, "pause", 0));
    JS_SetPropertyStr (ctx, result, "stop", JS_NewCFunction (ctx, scriptable_noop, "stop", 0));
    JS_SetPropertyStr (ctx, result, "isPlaying", JS_NewCFunction (ctx, scriptable_false, "isPlaying", 0));
    JS_SetPropertyStr (ctx, result, "getAnimation", JS_NewCFunction (ctx, scriptable_get_animation, "getAnimation", 1));
    return result;
}

JSValue scriptableobject_property_get (JSContext* ctx, JSValueConst obj_val, JSAtom atom, JSValueConst receiver) {
    JSClassID classId = 0;

    auto* container = static_cast<OpaqueScriptableObjectAdapter*> (JS_GetAnyOpaque (obj_val, &classId));

    if (!container || container->magic != SCRIPTABLE_OPAQUE_MAGIC) {
	return JS_ThrowTypeError (ctx, "invalid receiver for property access");
    }

    const char* name = JS_AtomToCString (ctx, atom);

    if (name == nullptr) {
	return JS_EXCEPTION;
    }

    ScopeGuard guard ([=] { JS_FreeCString (ctx, name); });

    if (std::strcmp (name, "name") == 0) {
	return JS_NewString (ctx, container->object.getObject ().name.c_str ());
    }

    if (std::strcmp (name, "play") == 0 || std::strcmp (name, "pause") == 0 || std::strcmp (name, "stop") == 0
	|| std::strcmp (name, "getParent") == 0) {
	return JS_NewCFunction (ctx, scriptable_noop, name, 0);
    }

    if (std::strcmp (name, "getAnimation") == 0 || std::strcmp (name, "getAnimationLayer") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_animation, name, 1);
    }

    try {
	// find the property inside, otherwise return undefined
	auto& property = container->object.getProperty (name);

	return container->adapter.getEngine ().dynamicToJs (property);
    } catch (const std::exception& e) {
	return JS_UNDEFINED;
    }
}

int scriptableobject_property_set (
    JSContext* ctx, JSValueConst obj_val, JSAtom atom, JSValueConst val, JSValueConst receiver, int flags
) {
    JSClassID classId = 0;

    auto* container = static_cast<OpaqueScriptableObjectAdapter*> (JS_GetAnyOpaque (obj_val, &classId));

    if (!container || container->magic != SCRIPTABLE_OPAQUE_MAGIC) {
	JS_ThrowTypeError (ctx, "invalid receiver for property assignment");
	return -1;
    }

    const char* name = JS_AtomToCString (ctx, atom);

    if (name == nullptr) {
	return -1;
    }

    ScopeGuard guard ([=] { JS_FreeCString (ctx, name); });

    try {
	auto& property = container->object.getProperty (name);

	if (JS_IsBool (val)) {
	    property.update (JS_ToBool (ctx, val) != 0, DynamicValue::UpdateSource::User);
	} else if (JS_IsNumber (val)) {
	    double number = 0.0;
	    JS_ToFloat64 (ctx, &number, val);
	    property.update (static_cast<float> (number), DynamicValue::UpdateSource::User);
	} else if (JS_IsObject (val)) {
	    const auto component = [ctx, &val] (const char* key) -> float {
		JSValue field = JS_GetPropertyStr (ctx, val, key);
		double number = 0.0;
		if (JS_IsNumber (field)) {
		    JS_ToFloat64 (ctx, &number, field);
		}
		JS_FreeValue (ctx, field);
		return static_cast<float> (number);
	    };
	    property.update (
		glm::vec3 (component ("x"), component ("y"), component ("z")), DynamicValue::UpdateSource::User
	    );
	}
    } catch (const std::exception&) {
	// Unknown script-visible fields are ignored by Wallpaper Engine.
    }

    return 1;
}

ScriptableObjectAdapter::ScriptableObjectAdapter (ScriptEngine& engine, std::string name) :
    ObjectAdapter (engine), m_exoticMethods (), m_name (std::move (name)) {
    this->m_exoticMethods.get_property = scriptableobject_property_get;
    this->m_exoticMethods.set_property = scriptableobject_property_set;

    this->registerType (
	{
	    .class_name = m_name.c_str (),
	    .exotic = &m_exoticMethods,
	}
    );
}

JSValue ScriptableObjectAdapter::instantiate (ScriptableObject& object) {
    JSValue result = this->ObjectAdapter::instantiate (object);
    JS_SetOpaque (
	result,
	new OpaqueScriptableObjectAdapter { .magic = SCRIPTABLE_OPAQUE_MAGIC, .adapter = *this, .object = object }
    );

    return result;
}

JSValue ScriptableObjectAdapter::instantiate (DynamicValue& value) {
    throw std::runtime_error ("Cannot create a ScriptableObject instance from a DynamicValue");
}

WallpaperEngine::Scripting::ScriptableObject* ScriptableObjectAdapter::fromJS (JSValue value) {
    JSClassID classId = 0;
    auto* container = static_cast<OpaqueScriptableObjectAdapter*> (JS_GetAnyOpaque (value, &classId));

    if (container == nullptr || container->magic != SCRIPTABLE_OPAQUE_MAGIC) {
	return nullptr;
    }

    return &container->object;
}
