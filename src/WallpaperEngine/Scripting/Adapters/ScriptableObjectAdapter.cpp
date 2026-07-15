#include "ScriptableObjectAdapter.h"

#include <cstring>
#include <glm/glm.hpp>
#include <utility>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Render/Objects/CImage.h"
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

enum AnimationCommand {
    AnimationCommand_Play,
    AnimationCommand_Pause,
    AnimationCommand_Stop,
    AnimationCommand_IsPlaying,
};

static OpaqueScriptableObjectAdapter* scriptable_container (JSValueConst value) {
    JSClassID classId = 0;
    auto* container = static_cast<OpaqueScriptableObjectAdapter*> (JS_GetAnyOpaque (value, &classId));
    return container != nullptr && container->magic == SCRIPTABLE_OPAQUE_MAGIC ? container : nullptr;
}

static WallpaperEngine::Render::Objects::CImage* scriptable_image (JSValueConst value) {
    OpaqueScriptableObjectAdapter* container = scriptable_container (value);
    return container != nullptr ? dynamic_cast<WallpaperEngine::Render::Objects::CImage*> (&container->object)
				: nullptr;
}

static JSValue scriptable_animation_command (
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValueConst* functionData
) {
    int64_t imagePointer = 0;
    int32_t layerIndex = -1;
    if (JS_ToBigInt64 (ctx, &imagePointer, functionData[0]) < 0 || JS_ToInt32 (ctx, &layerIndex, functionData[1]) < 0) {
	return JS_EXCEPTION;
    }

    auto* image = reinterpret_cast<WallpaperEngine::Render::Objects::CImage*> (imagePointer);
    if (image == nullptr) {
	return JS_UNDEFINED;
    }
    const std::optional<size_t> index
	= layerIndex >= 0 ? std::optional<size_t> (static_cast<size_t> (layerIndex)) : std::nullopt;

    switch (magic) {
	case AnimationCommand_Play:
	    image->playPuppetAnimationLayer (index);
	    return JS_UNDEFINED;
	case AnimationCommand_Pause:
	    image->pausePuppetAnimationLayer (index);
	    return JS_UNDEFINED;
	case AnimationCommand_Stop:
	    image->stopPuppetAnimationLayer (index);
	    return JS_UNDEFINED;
	case AnimationCommand_IsPlaying:
	    return JS_NewBool (ctx, image->isPuppetAnimationLayerPlaying (index));
	default:
	    return JS_UNDEFINED;
    }
}

static JSValue scriptable_animation_controller (
    JSContext* ctx, WallpaperEngine::Render::Objects::CImage& image, const std::optional<size_t> index
) {
    const int32_t layerIndex = index.has_value () ? static_cast<int32_t> (*index) : -1;
    JSValue functionData[] = {
	JS_NewBigInt64 (ctx, reinterpret_cast<int64_t> (&image)),
	JS_NewInt32 (ctx, layerIndex),
    };
    JSValue result = JS_NewObject (ctx);

    JS_SetPropertyStr (
	ctx, result, "play",
	JS_NewCFunctionData (ctx, scriptable_animation_command, 0, AnimationCommand_Play, 2, functionData)
    );
    JS_SetPropertyStr (
	ctx, result, "pause",
	JS_NewCFunctionData (ctx, scriptable_animation_command, 0, AnimationCommand_Pause, 2, functionData)
    );
    JS_SetPropertyStr (
	ctx, result, "stop",
	JS_NewCFunctionData (ctx, scriptable_animation_command, 0, AnimationCommand_Stop, 2, functionData)
    );
    JS_SetPropertyStr (
	ctx, result, "isPlaying",
	JS_NewCFunctionData (ctx, scriptable_animation_command, 0, AnimationCommand_IsPlaying, 2, functionData)
    );
    JS_FreeValue (ctx, functionData[0]);
    JS_FreeValue (ctx, functionData[1]);
    return result;
}

static JSValue scriptable_get_animation (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* image = scriptable_image (this_val);
    if (image == nullptr) {
	return JS_UNDEFINED;
    }
    if (argc < 1) {
	return scriptable_animation_controller (ctx, *image, std::nullopt);
    }

    std::optional<size_t> index;
    if (JS_IsNumber (argv[0])) {
	int32_t numericIndex = -1;
	if (JS_ToInt32 (ctx, &numericIndex, argv[0]) < 0) {
	    return JS_EXCEPTION;
	}
	index = image->findPuppetAnimationLayer (numericIndex);
    } else if (JS_IsString (argv[0])) {
	const char* name = JS_ToCString (ctx, argv[0]);
	if (name == nullptr) {
	    return JS_EXCEPTION;
	}
	index = image->findPuppetAnimationLayer (name);
	JS_FreeCString (ctx, name);
    }

    return index.has_value () ? scriptable_animation_controller (ctx, *image, index) : JS_UNDEFINED;
}

static JSValue
scriptable_object_animation_command (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    auto* image = scriptable_image (this_val);
    if (image == nullptr) {
	return magic == AnimationCommand_IsPlaying ? JS_FALSE : JS_UNDEFINED;
    }

    switch (magic) {
	case AnimationCommand_Play:
	    image->playPuppetAnimationLayer ();
	    return JS_UNDEFINED;
	case AnimationCommand_Pause:
	    image->pausePuppetAnimationLayer ();
	    return JS_UNDEFINED;
	case AnimationCommand_Stop:
	    image->stopPuppetAnimationLayer ();
	    return JS_UNDEFINED;
	case AnimationCommand_IsPlaying:
	    return JS_NewBool (ctx, image->isPuppetAnimationLayerPlaying ());
	default:
	    return JS_UNDEFINED;
    }
}

static JSValue
scriptable_get_animation_layer_count (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* image = scriptable_image (this_val);
    return JS_NewInt64 (ctx, image != nullptr ? static_cast<int64_t> (image->getPuppetAnimationLayerCount ()) : 0);
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

    if (std::strcmp (name, "play") == 0) {
	return JS_NewCFunctionMagic (
	    ctx, scriptable_object_animation_command, name, 0, JS_CFUNC_generic_magic, AnimationCommand_Play
	);
    }
    if (std::strcmp (name, "pause") == 0) {
	return JS_NewCFunctionMagic (
	    ctx, scriptable_object_animation_command, name, 0, JS_CFUNC_generic_magic, AnimationCommand_Pause
	);
    }
    if (std::strcmp (name, "stop") == 0) {
	return JS_NewCFunctionMagic (
	    ctx, scriptable_object_animation_command, name, 0, JS_CFUNC_generic_magic, AnimationCommand_Stop
	);
    }
    if (std::strcmp (name, "isPlaying") == 0) {
	return JS_NewCFunctionMagic (
	    ctx, scriptable_object_animation_command, name, 0, JS_CFUNC_generic_magic, AnimationCommand_IsPlaying
	);
    }
    if (std::strcmp (name, "getParent") == 0) {
	return JS_NewCFunction (ctx, scriptable_noop, name, 0);
    }

    if (std::strcmp (name, "getAnimation") == 0 || std::strcmp (name, "getAnimationLayer") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_animation, name, 1);
    }
    if (std::strcmp (name, "getAnimationLayerCount") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_animation_layer_count, name, 0);
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
