#include "SceneObject.h"

#include "Adapters/ScriptableObjectAdapter.h"
#include "ScriptEngine.h"
#include "ScriptableObject.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Render/Camera.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include <cmath>
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

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.bloom.strength->value->getFloat ());
}

JSValue get_bloomthreshold (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewFloat64 (ctx, container->getScene ().getScene ().camera.bloom.threshold->value->getFloat ());
}

JSValue get_clearenabled (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);

    return JS_NewBool (ctx, container->getScene ().getScene ().clearEnabled->value->getBool ());
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
	*container->getScene ().getScene ().colors.skylight->value
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
	return JS_ThrowTypeError (ctx, "getLayer() expects exactly one argument");
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
    } else if (!JS_IsNull (layer) && !JS_IsUndefined (layer)) {
	// Wallpaper Engine accepts string-coercible values here too (notably boxed
	// strings produced by some workshop script/transpiler combinations).
	const char* result = JS_ToCString (ctx, layer);

	if (result == nullptr) {
	    return JS_EXCEPTION;
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

    return JS_UNDEFINED;
}

// Instantiate a real engine Vec3 rather than a bare {x,y,z} bag: stock camera controllers chain
// vector math straight off getCameraTransforms() (`p2.subtract(p1).divide(len)`), so a plain
// object throws "not a function" on the first tick and kills the whole script.
static JSValue make_script_vec3 (JSContext* ctx, ScriptEngine& engine, const glm::vec3& value) {
    JSValue result = engine.getAdapters ().vec3->instantiate ();
    JS_SetPropertyStr (ctx, result, "x", JS_NewFloat64 (ctx, value.x));
    JS_SetPropertyStr (ctx, result, "y", JS_NewFloat64 (ctx, value.y));
    JS_SetPropertyStr (ctx, result, "z", JS_NewFloat64 (ctx, value.z));
    return result;
}

JSValue scene_get_camera_transforms (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* container = get_opaque (this_val);
    const auto& camera = container->getScene ().getCamera ();

    auto& engine = container->getEngine ();
    JSValue result = JS_NewObject (ctx);
    JS_SetPropertyStr (ctx, result, "eye", make_script_vec3 (ctx, engine, camera.getEye ()));
    JS_SetPropertyStr (ctx, result, "center", make_script_vec3 (ctx, engine, camera.getCenter ()));
    JS_SetPropertyStr (ctx, result, "up", make_script_vec3 (ctx, engine, camera.getUp ()));
    JS_SetPropertyStr (ctx, result, "zoom", JS_NewFloat64 (ctx, camera.getZoom ()));
    return result;
}

static bool read_script_vec3 (JSContext* ctx, JSValueConst value, glm::vec3& result) {
    if (!JS_IsObject (value)) {
	return false;
    }

    JSValue fields[] = {
	JS_GetPropertyStr (ctx, value, "x"),
	JS_GetPropertyStr (ctx, value, "y"),
	JS_GetPropertyStr (ctx, value, "z"),
    };
    const bool valid = JS_IsNumber (fields[0]) && JS_IsNumber (fields[1]) && JS_IsNumber (fields[2]);
    double components[] = {0.0, 0.0, 0.0};
    if (valid) {
	JS_ToFloat64 (ctx, &components[0], fields[0]);
	JS_ToFloat64 (ctx, &components[1], fields[1]);
	JS_ToFloat64 (ctx, &components[2], fields[2]);
    }
    for (const auto& field : fields) {
	JS_FreeValue (ctx, field);
    }

    result = glm::vec3 (components[0], components[1], components[2]);
    return valid;
}

enum class SceneField {
    Bloom, BloomStrength, BloomThreshold, ClearEnabled, ClearColor, AmbientColor, SkylightColor, Fov, NearZ, FarZ, CameraFade, CameraShake, CameraShakeSpeed, CameraShakeAmplitude, CameraShakeRoughness, CameraParallax, CameraParallaxAmount, CameraParallaxDelay, CameraParallaxMouseInfluence
};

// Native IScene installs both property callbacks (scenescript64.dll
// 181631d00/181632030). These fields are writable in lib.sceneScript.d.ts.
// Update the existing setting so its renderer consumers and bindings see writes.
JSValue set_scene_field (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc != 1) {
        return JS_ThrowTypeError (ctx, "Scene property setter expects one value");
    }
    const auto& scene = get_opaque (this_val)->getScene ().getScene ();
    DynamicValue* target = nullptr;
    const auto field = static_cast<SceneField> (magic);
    switch (field) {
        case SceneField::Bloom: target = scene.camera.bloom.enabled->value.get (); break;
        case SceneField::BloomStrength: target = scene.camera.bloom.strength->value.get (); break;
        case SceneField::BloomThreshold: target = scene.camera.bloom.threshold->value.get (); break;
        case SceneField::ClearEnabled: target = scene.clearEnabled->value.get (); break;
        case SceneField::ClearColor: target = scene.colors.clear->value.get (); break;
        case SceneField::AmbientColor: target = scene.colors.ambient->value.get (); break;
        case SceneField::SkylightColor: target = scene.colors.skylight->value.get (); break;
        case SceneField::Fov: target = scene.camera.projection.fov->value.get (); break;
        case SceneField::NearZ: target = scene.camera.projection.nearz->value.get (); break;
        case SceneField::FarZ: target = scene.camera.projection.farz->value.get (); break;
        case SceneField::CameraFade: target = scene.camera.fade->value.get (); break;
        case SceneField::CameraShake: target = scene.camera.shake.enabled->value.get (); break;
        case SceneField::CameraShakeSpeed: target = scene.camera.shake.speed->value.get (); break;
        case SceneField::CameraShakeAmplitude: target = scene.camera.shake.amplitude->value.get (); break;
        case SceneField::CameraShakeRoughness: target = scene.camera.shake.roughness->value.get (); break;
        case SceneField::CameraParallax: target = scene.camera.parallax.enabled->value.get (); break;
        case SceneField::CameraParallaxAmount: target = scene.camera.parallax.amount->value.get (); break;
        case SceneField::CameraParallaxDelay: target = scene.camera.parallax.delay->value.get (); break;
        case SceneField::CameraParallaxMouseInfluence: target = scene.camera.parallax.mouseInfluence->value.get (); break;
    }
    if (target == nullptr) {
        return JS_UNDEFINED;
    }
    if (field == SceneField::ClearColor || field == SceneField::AmbientColor || field == SceneField::SkylightColor) {
        glm::vec3 value;
        if (!read_script_vec3 (ctx, argv[0], value)) {
            return JS_ThrowTypeError (ctx, "Scene color expects a Vec3");
        }
        target->update (value, DynamicValue::UpdateSource::Script);
    } else if (field == SceneField::Bloom || field == SceneField::ClearEnabled || field == SceneField::CameraFade
               || field == SceneField::CameraShake || field == SceneField::CameraParallax) {
        if (!JS_IsBool (argv[0])) {
            return JS_ThrowTypeError (ctx, "Scene toggle expects a boolean");
        }
        target->update (JS_ToBool (ctx, argv[0]) != 0, DynamicValue::UpdateSource::Script);
    } else {
        if (!JS_IsNumber (argv[0])) {
            return JS_ThrowTypeError (ctx, "Scene numeric property expects a number");
        }
        double value = 0.0;
        if (JS_ToFloat64 (ctx, &value, argv[0]) < 0) {
            return JS_EXCEPTION;
        }
        target->update (static_cast<float> (value), DynamicValue::UpdateSource::Script);
    }
    return JS_UNDEFINED;
}

// thisScene.setCameraTransforms({eye, center, up, zoom}) -> hands the camera to the script for
// this frame. This is how stock 3D scenes implement mouse-drag orbiting: a controller layer
// reads input.cursorScreenPosition/cursorLeftDown, integrates it, and pushes the result here.
// Fields are optional; anything omitted keeps the value getCameraTransforms would have returned.
JSValue scene_set_camera_transforms (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject (argv[0])) {
	return JS_ThrowTypeError (ctx, "setCameraTransforms() expects a transform object");
    }

    auto* container = get_opaque (this_val);
    auto& scene = container->getScene ();
    const auto& camera = scene.getCamera ();

    CameraTransform transform = {
	.center = camera.getCenter (),
	.eye = camera.getEye (),
	.up = camera.getUp (),
	.fov = camera.getFov (),
	.zoom = camera.getZoom (),
    };

    const auto readInto = [ctx, &argv] (const char* name, glm::vec3& target) {
	JSValue value = JS_GetPropertyStr (ctx, argv[0], name);
	glm::vec3 candidate;
	if (read_script_vec3 (ctx, value, candidate)) {
	    target = candidate;
	}
	JS_FreeValue (ctx, value);
    };
    readInto ("eye", transform.eye);
    readInto ("center", transform.center);
    readInto ("up", transform.up);

    JSValue zoom = JS_GetPropertyStr (ctx, argv[0], "zoom");
    if (JS_IsNumber (zoom)) {
	double value = 0.0;
	JS_ToFloat64 (ctx, &value, zoom);
	transform.zoom = static_cast<float> (value);
    }
    JS_FreeValue (ctx, zoom);

    // Drop a pose that is not finite instead of latching it. Controllers integrate from whatever
    // getCameraTransforms() hands back, so a single inf/NaN frame — these scripts divide by a vector
    // length that is zero until the scene settles — would otherwise feed itself forever. Ignoring
    // the call leaves the last good pose in place, which is what the script reads next tick.
    const auto finite = [] (const glm::vec3& value) {
	return std::isfinite (value.x) && std::isfinite (value.y) && std::isfinite (value.z);
    };

    if (!finite (transform.eye) || !finite (transform.center) || !finite (transform.up)
	|| !std::isfinite (transform.zoom)) {
	return JS_UNDEFINED;
    }

    scene.setScriptCameraTransform (transform);
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
	return JS_ThrowTypeError (ctx, "getLayerByID() expects exactly one argument");
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
	return JS_ThrowTypeError (ctx, "getLayerIndex() expects one argument");
    }

    auto* container = get_opaque (this_val);
    auto* layer = WallpaperEngine::Scripting::Adapters::ScriptableObjectAdapter::fromJS (argv[0]);

    if (layer == nullptr) {
	return JS_NewInt32 (ctx, -1);
    }

    return JS_NewInt32 (ctx, container->getScene ().getScriptableLayerIndex (layer));
}

// Assets and full layer configurations share the native object parser.
JSValue scene_create_layer (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
	return JS_UNDEFINED;
    }

    auto* container = get_opaque (this_val);
    // createLayer takes "String|Object|IAssetHandle" - the handle engine.registerAsset() hands back
    // carries the path on .file, and a plain configuration object spells it the same way.
    JSValue source = JS_DupValue (ctx, argv[0]);

    if (JS_IsObject (source)) {
	JSValue file = JS_GetPropertyStr (ctx, source, "file");
	if (!JS_IsString (file)) {
	    JS_FreeValue (ctx, file);
	    JSValue json = JS_JSONStringify (ctx, source, JS_UNDEFINED, JS_UNDEFINED);
	    JS_FreeValue (ctx, source);
	    if (JS_IsException (json)) return json;
	    const char* data = JS_ToCString (ctx, json);
	    JS_FreeValue (ctx, json);
	    if (data == nullptr) return JS_EXCEPTION;
	    ScopeGuard guard ([=] { JS_FreeCString (ctx, data); });
	    try {
		auto config = WallpaperEngine::Data::JSON::JSON::parse (data);
		auto* layer = container->getScene ().createLayer (
		    std::move (config), container->getEngine ().getRunningModuleWorkshopId ());
		return layer != nullptr && layer->is<ScriptableObject> ()
		    ? container->getEngine ().getAdapters ().object->instantiate (*layer->as<ScriptableObject> ())
		    : JS_UNDEFINED;
	    } catch (const std::exception& e) {
		return JS_ThrowTypeError (ctx, "Invalid layer configuration: %s", e.what ());
	    }
	}
	JS_FreeValue (ctx, source);
	source = file;
    }

    if (!JS_IsString (source)) {
	JS_FreeValue (ctx, source);
	return JS_UNDEFINED;
    }

    const char* path = JS_ToCString (ctx, source);
    JS_FreeValue (ctx, source);

    if (path == nullptr) {
	return JS_UNDEFINED;
    }

    ScopeGuard guard ([=] { JS_FreeCString (ctx, path); });

    const std::string workshopId = container->getEngine ().getRunningModuleWorkshopId ();
    auto* layer = container->getScene ().createLayer (std::string (path), workshopId);

    if (layer == nullptr || !layer->is<ScriptableObject> ()) {
	return JS_UNDEFINED;
    }

    return container->getEngine ().getAdapters ().object->instantiate (*layer->as<ScriptableObject> ());
}

static ScriptableObject* resolve_layer_argument (
    JSContext* ctx, JSValueConst scene, JSValueConst argument
) {
    if (auto* layer = WallpaperEngine::Scripting::Adapters::ScriptableObjectAdapter::fromJS (argument)) return layer;
    JSValue value = get_layer (ctx, scene, 1, &argument);
    auto* layer = WallpaperEngine::Scripting::Adapters::ScriptableObjectAdapter::fromJS (value);
    JS_FreeValue (ctx, value);
    return layer;
}

JSValue scene_get_initial_layer_config (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* layer = argc > 0 ? resolve_layer_argument (ctx, this_val, argv[0]) : nullptr;
    if (layer == nullptr) return JS_UNDEFINED;
    const auto& config = layer->getObject ().initialConfiguration;
    return JS_ParseJSON (ctx, config.data (), config.size (), "<initial-layer-config>");
}

JSValue scene_destroy_layer (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    auto* layer = argc > 0 ? resolve_layer_argument (ctx, this_val, argv[0]) : nullptr;
    return JS_NewBool (ctx, get_opaque (this_val)->getScene ().destroyLayer (layer));
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
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::Bloom)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "bloomstrength"),
	JS_NewCFunction (this->m_engine.getContext (), get_bloomstrength, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::BloomStrength)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "bloomthreshold"),
	JS_NewCFunction (this->m_engine.getContext (), get_bloomthreshold, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::BloomThreshold)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "clearenabled"),
	JS_NewCFunction (this->m_engine.getContext (), get_clearenabled, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::ClearEnabled)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "clearcolor"),
	JS_NewCFunction (this->m_engine.getContext (), get_clearcolor, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::ClearColor)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "ambientcolor"),
	JS_NewCFunction (this->m_engine.getContext (), get_ambientcolor, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::AmbientColor)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "skylightcolor"),
	JS_NewCFunction (this->m_engine.getContext (), get_skylightcolor, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::SkylightColor)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "fov"),
	JS_NewCFunction (this->m_engine.getContext (), get_fov, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::Fov)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "nearz"),
	JS_NewCFunction (this->m_engine.getContext (), get_nearz, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::NearZ)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "farz"),
	JS_NewCFunction (this->m_engine.getContext (), get_farz, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::FarZ)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "camerafade"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerafade, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::CameraFade)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "camerashake"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashake, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::CameraShake)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "camerashakespeed"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashakespeed, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::CameraShakeSpeed)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "camerashakeamplitude"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashakeamplitude, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::CameraShakeAmplitude)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "camerashakeroughness"),
	JS_NewCFunction (this->m_engine.getContext (), get_camerashakeroughness, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::CameraShakeRoughness)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "cameraparallax"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallax, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::CameraParallax)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "cameraparallaxamount"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallaxamount, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::CameraParallaxAmount)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "cameraparallaxdelay"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallaxdelay, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::CameraParallaxDelay)), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance,
	JS_NewAtom (this->m_engine.getContext (), "cameraparallaxmouseinfluence"),
	JS_NewCFunction (this->m_engine.getContext (), get_cameraparallaxmouseinfluence, "get", 0),
	JS_NewCFunctionMagic (this->m_engine.getContext (), set_scene_field, "set", 1, JS_CFUNC_generic_magic,
	    static_cast<int> (SceneField::CameraParallaxMouseInfluence)), JS_PROP_ENUMERABLE
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
	this->m_engine.getContext (), this->m_instance, "getInitialLayerConfig",
	JS_NewCFunction (this->m_engine.getContext (), scene_get_initial_layer_config, "getInitialLayerConfig", 1),
	JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "destroyLayer",
	JS_NewCFunction (this->m_engine.getContext (), scene_destroy_layer, "destroyLayer", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "sortLayer",
	JS_NewCFunction (this->m_engine.getContext (), scene_sort_layer, "sortLayer", 2), JS_PROP_ENUMERABLE
    );
    // TODO: ADD REST OF THE METHODS
}

SceneObject::~SceneObject () { JS_FreeValue (this->m_engine.getContext (), this->m_instance); }
