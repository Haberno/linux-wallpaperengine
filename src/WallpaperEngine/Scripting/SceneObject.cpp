#include "SceneObject.h"

#include "Adapters/ScriptableObjectAdapter.h"
#include "ScriptEngine.h"
#include "ScriptableObject.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Render/Camera.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include <cstdlib>

using namespace WallpaperEngine::Scripting;

SceneObject* get_opaque (JSValueConst this_val) {
    JSClassID classId;
    return static_cast<SceneObject*> (JS_GetAnyOpaque (this_val, &classId));
}

JSValue get_bloom (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewBool (ctx, container->getScene ().getScene ().camera.bloom.enabled->value->getBool ());
}

JSValue get_bloomstrength (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewInt32 (ctx, container->getScene ().getScene ().camera.bloom.strength->value->getInt ());
}

JSValue get_bloomthreshold (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewInt32 (ctx, container->getScene ().getScene ().camera.bloom.threshold->value->getInt ());
}

JSValue get_clearenabled (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewBool (ctx, container->getScene ().getScene ().camera.bloom.enabled->value->getBool ());
}

JSValue get_clearcolor (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return container->getEngine ().getAdapters ().vec3->instantiate (
	*container->getScene ().getScene ().colors.clear->value
    );
}

JSValue get_ambientcolor (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return container->getEngine ().getAdapters ().vec3->instantiate (
	*container->getScene ().getScene ().colors.ambient->value
    );
}

JSValue get_skylightcolor (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return container->getEngine ().getAdapters ().vec3->instantiate (
	*container->getScene ().getScene ().colors.ambient->value
    );
}

JSValue get_fov (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.projection.fov->value->getFloat ());
}

JSValue get_nearz (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.projection.nearz->value->getFloat ());
}

JSValue get_farz (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.projection.farz->value->getFloat ());
}

JSValue get_camerafade (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewBool (ctx, container->getScene ().getScene ().camera.fade->value->getBool ());
}

JSValue get_camerashake (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewBool (ctx, container->getScene ().getScene ().camera.shake.enabled->value->getBool ());
}

JSValue get_camerashakespeed (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.shake.speed->value->getFloat ());
}

JSValue get_camerashakeamplitude (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.shake.amplitude->value->getFloat ());
}

JSValue get_camerashakeroughness (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.shake.roughness->value->getFloat ());
}

JSValue get_cameraparallax (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewBool (ctx, container->getScene ().getScene ().camera.parallax.enabled->value->getBool ());
}

JSValue get_cameraparallaxamount (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.parallax.amount->value->getFloat ());
}

JSValue get_cameraparallaxdelay (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.parallax.delay->value->getFloat ());
}

JSValue get_cameraparallaxmouseinfluence (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.parallax.mouseInfluence->value->getFloat ());
}

JSValue get_layer (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc != 1) {
	return JS_EXCEPTION;
    }

    auto* container = get_opaque (this_val);

    JSValue layer = argv[0];

    if (JS_IsNumber (layer)) {
	int id = 0;

	JS_ToInt32 (ctx, &id, layer);

	auto* object = container->getScene ().getObject (id);

	if (object == nullptr) {
	    return JS_UNDEFINED;
	}

	if (!object->is<ScriptableObject> ()) {
	    return JS_UNDEFINED;
	}

	// TODO: REMOVE THIS CONST_CAST?
	return container->getEngine ().getAdapters ().object->instantiate (
	    const_cast<ScriptableObject&> (*object->as<ScriptableObject> ())
	);
    } else if (JS_IsString (layer)) {
	// find by name, this is harder
	const char* result = JS_ToCString (ctx, layer);

	if (result == nullptr) {
	    return JS_UNDEFINED;
	}

	ScopeGuard guard ([=] { JS_FreeCString (ctx, result); });

	for (auto object : container->getScene ().getObjectsByRenderOrder ()) {
	    if (object->getObject ().name != result) {
		continue;
	    }

	    if (!object->is<ScriptableObject> ()) {
		continue;
	    }

	    return container->getEngine ().getAdapters ().object->instantiate (*object->as<ScriptableObject> ());
	}
    }

    return JS_EXCEPTION;
}

JSValue scene_set_value (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { return JS_EXCEPTION; }

static JSValue make_script_vec3 (JSContext* ctx, const glm::vec3& value) {
    JSValue result = JS_NewObject (ctx);
    JS_SetPropertyStr (ctx, result, "x", JS_NewFloat64 (ctx, value.x));
    JS_SetPropertyStr (ctx, result, "y", JS_NewFloat64 (ctx, value.y));
    JS_SetPropertyStr (ctx, result, "z", JS_NewFloat64 (ctx, value.z));
    return result;
}

JSValue scene_get_camera_transforms (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);
    const auto& camera = container->getScene ().getCamera ();

    JSValue result = JS_NewObject (ctx);
    JS_SetPropertyStr (ctx, result, "eye", make_script_vec3 (ctx, camera.getEye ()));
    JS_SetPropertyStr (ctx, result, "center", make_script_vec3 (ctx, camera.getCenter ()));
    JS_SetPropertyStr (ctx, result, "up", make_script_vec3 (ctx, camera.getUp ()));
    JS_SetPropertyStr (ctx, result, "fov", JS_NewFloat64 (ctx, camera.getFov ()));
    return result;
}

JSValue scene_set_camera_transforms (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_UNDEFINED;
}

// thisScene.enumerateLayers() -> array of every scriptable layer in render order.
JSValue scene_enumerate_layers (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);
    JSValue arr = JS_NewArray (ctx);
    uint32_t index = 0;

    for (auto* object : container->getScene ().getObjectsByRenderOrder ()) {
	if (object == nullptr || !object->is<ScriptableObject> ()) {
	    continue;
	}
	JS_SetPropertyUint32 (
	    ctx, arr, index++, container->getEngine ().getAdapters ().object->instantiate (*object->as<ScriptableObject> ())
	);
    }

    return arr;
}

// thisScene.getLayerByID(id) -> the layer whose object id matches (id given as a string), or undefined.
JSValue scene_get_layer_by_id (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc != 1) {
	return JS_EXCEPTION;
    }

    auto* container = get_opaque (this_val);
    const char* str = JS_ToCString (ctx, argv[0]);

    if (str == nullptr) {
	return JS_UNDEFINED;
    }

    const long id = std::strtol (str, nullptr, 10);
    JS_FreeCString (ctx, str);

    for (auto* object : container->getScene ().getObjectsByRenderOrder ()) {
	if (object == nullptr || !object->is<ScriptableObject> ()) {
	    continue;
	}
	if (object->getId () == id) {
	    return container->getEngine ().getAdapters ().object->instantiate (*object->as<ScriptableObject> ());
	}
    }

    return JS_UNDEFINED;
}

// thisScene.getLayerIndex(layer) -> index of the layer in the scriptable render order, or -1.
JSValue scene_get_layer_index (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc != 1) {
	return JS_EXCEPTION;
    }

    auto* container = get_opaque (this_val);
    auto* layer = WallpaperEngine::Scripting::Adapters::ScriptableObjectAdapter::fromJS (argv[0]);

    if (layer == nullptr) {
	return JS_NewInt32 (ctx, -1);
    }

    return JS_NewInt32 (ctx, container->getScene ().getScriptableLayerIndex (layer));
}

// thisScene.createLayer(modelPath) -> instantiate a new image layer at runtime and return its
// handle. Generative scripts (audio visualizers, particle-ish bar systems) build their layers here.
JSValue scene_create_layer (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString (argv[0])) {
	return JS_UNDEFINED;
    }

    auto* container = get_opaque (this_val);
    const char* path = JS_ToCString (ctx, argv[0]);

    if (path == nullptr) {
	return JS_UNDEFINED;
    }

    ScopeGuard guard ([=] { JS_FreeCString (ctx, path); });

    const std::string workshopId = container->getEngine ().getRunningModuleWorkshopId ();
    auto* layer = container->getScene ().createLayer (path, workshopId);

    if (layer == nullptr || !layer->is<ScriptableObject> ()) {
	return JS_UNDEFINED;
    }

    return container->getEngine ().getAdapters ().object->instantiate (*layer->as<ScriptableObject> ());
}

// thisScene.sortLayer(layer, index) -> move a layer to a z-position in the render order.
JSValue scene_sort_layer (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
	return JS_UNDEFINED;
    }

    auto* container = get_opaque (this_val);
    auto* layer = WallpaperEngine::Scripting::Adapters::ScriptableObjectAdapter::fromJS (argv[0]);

    if (layer == nullptr) {
	return JS_UNDEFINED;
    }

    int index = 0;
    JS_ToInt32 (ctx, &index, argv[1]);
    container->getScene ().moveLayerToScriptableIndex (layer, index);

    return JS_UNDEFINED;
}

SceneObject::SceneObject (ScriptEngine& engine, Render::Wallpapers::CScene& scene) :
    m_scene (scene), m_engine (engine), m_classId (0) {
    this->m_definition = { .class_name = "IScene" };
    JS_NewClassID (this->m_engine.getRuntime (), &this->m_classId);
    JS_NewClass (this->m_engine.getRuntime (), this->m_classId, &this->m_definition);
    this->m_instance = JS_NewObjectClass (this->m_engine.getContext (), this->m_classId);

    JS_DupValue (this->m_engine.getContext (), this->m_instance);

    // set properties
    JS_SetOpaque (this->m_instance, this);
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "bloom"),
	JS_NewCFunction (this->m_engine.getContext (), get_bloom, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "bloomstrength"),
	JS_NewCFunction (this->m_engine.getContext (), get_bloomstrength, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "bloomthreshold"),
	JS_NewCFunction (this->m_engine.getContext (), get_bloomthreshold, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "clearenabled"),
	JS_NewCFunction (this->m_engine.getContext (), get_clearenabled, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "clearcolor"),
	JS_NewCFunction (this->m_engine.getContext (), get_clearcolor, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "ambientcolor"),
	JS_NewCFunction (this->m_engine.getContext (), get_ambientcolor, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "skylightcolor"),
	JS_NewCFunction (this->m_engine.getContext (), get_skylightcolor, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "fov"),
	JS_NewCFunction (this->m_engine.getContext (), get_fov, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "nearz"),
	JS_NewCFunction (this->m_engine.getContext (), get_nearz, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "farz"),
	JS_NewCFunction (this->m_engine.getContext (), get_farz, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "camerafade"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerafade, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "camerashake"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashake, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "camerashakespeed"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashakespeed, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "camerashakeamplitude"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashakeamplitude, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "camerashakeroughness"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashakeroughness, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "cameraparallax"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallax, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "cameraparallaxamount"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallaxamount, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "cameraparallaxdelay"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallaxdelay, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "cameraparallaxmouseinfluence"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallaxmouseinfluence, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), scene_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "getLayer",
	JS_NewCFunction (this->m_engine.getContext (), get_layer, "getLayer", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "enumerateLayers",
	JS_NewCFunction (this->m_engine.getContext (), scene_enumerate_layers, "enumerateLayers", 0), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "getLayerByID",
	JS_NewCFunction (this->m_engine.getContext (), scene_get_layer_by_id, "getLayerByID", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "getCameraTransforms",
	JS_NewCFunction (this->m_engine.getContext (), scene_get_camera_transforms, "getCameraTransforms", 0),
	JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "setCameraTransforms",
	JS_NewCFunction (this->m_engine.getContext (), scene_set_camera_transforms, "setCameraTransforms", 1),
	JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "getLayerIndex",
	JS_NewCFunction (this->m_engine.getContext (), scene_get_layer_index, "getLayerIndex", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "createLayer",
	JS_NewCFunction (this->m_engine.getContext (), scene_create_layer, "createLayer", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "sortLayer",
	JS_NewCFunction (this->m_engine.getContext (), scene_sort_layer, "sortLayer", 2), JS_PROP_ENUMERABLE
    );
    // TODO: ADD REST OF THE METHODS
}

SceneObject::~SceneObject () { JS_FreeValue (this->m_engine.getContext (), this->m_instance); }
