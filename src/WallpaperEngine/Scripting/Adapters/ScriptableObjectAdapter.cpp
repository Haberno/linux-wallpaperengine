#include "ScriptableObjectAdapter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#undef GLM_ENABLE_EXPERIMENTAL
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

static void scriptableobject_finalizer (JSRuntime* runtime, JSValueConst value) {
    JSClassID classId = 0;
    delete static_cast<OpaqueScriptableObjectAdapter*> (JS_GetAnyOpaque (value, &classId));
}

enum AnimationCommand {
    AnimationCommand_Play,
    AnimationCommand_Pause,
    AnimationCommand_Stop,
    AnimationCommand_IsPlaying,
};

enum TextureAnimationCommand {
    TextureAnimationCommand_Play,
    TextureAnimationCommand_Pause,
    TextureAnimationCommand_Stop,
    TextureAnimationCommand_IsPlaying,
    TextureAnimationCommand_GetFrame,
    TextureAnimationCommand_SetFrame,
    TextureAnimationCommand_Join,
};

enum TextureAnimationProperty {
    TextureAnimationProperty_FrameCount,
    TextureAnimationProperty_Duration,
    TextureAnimationProperty_Rate,
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

static WallpaperEngine::Render::Objects::CImage*
scriptable_animation_image (JSContext* ctx, JSValueConst* functionData) {
    int64_t imagePointer = 0;
    if (JS_ToBigInt64 (ctx, &imagePointer, functionData[0]) < 0) {
	return nullptr;
    }
    return reinterpret_cast<WallpaperEngine::Render::Objects::CImage*> (imagePointer);
}

static JSValue texture_animation_command (
    JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv, int magic, JSValueConst* functionData
) {
    auto* image = scriptable_animation_image (ctx, functionData);
    if (image == nullptr) {
	return JS_UNDEFINED;
    }

    switch (magic) {
	case TextureAnimationCommand_Play:
	    image->playTextureAnimation ();
	    return JS_UNDEFINED;
	case TextureAnimationCommand_Pause:
	    image->pauseTextureAnimation ();
	    return JS_UNDEFINED;
	case TextureAnimationCommand_Stop:
	    image->stopTextureAnimation ();
	    return JS_UNDEFINED;
	case TextureAnimationCommand_IsPlaying:
	    return JS_NewBool (ctx, image->isTextureAnimationPlaying ());
	case TextureAnimationCommand_GetFrame:
	    return JS_NewInt64 (ctx, static_cast<int64_t> (image->getTextureAnimationFrame ()));
	case TextureAnimationCommand_SetFrame:
	    {
		int64_t frame = 0;
		if (argc < 1 || JS_ToInt64 (ctx, &frame, argv[0]) < 0) {
		    return JS_ThrowTypeError (ctx, "setFrame() expects a frame number");
		}
		image->setTextureAnimationFrame (static_cast<size_t> (std::max<int64_t> (0, frame)));
		return JS_UNDEFINED;
	    }
	case TextureAnimationCommand_Join:
	    image->joinTextureAnimation ();
	    return JS_UNDEFINED;
	default:
	    return JS_UNDEFINED;
    }
}

static JSValue texture_animation_property_get (
    JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv, int magic, JSValueConst* functionData
) {
    auto* image = scriptable_animation_image (ctx, functionData);
    if (image == nullptr) {
	return JS_UNDEFINED;
    }

    switch (magic) {
	case TextureAnimationProperty_FrameCount:
	    return JS_NewInt64 (ctx, static_cast<int64_t> (image->getTextureAnimationFrameCount ()));
	case TextureAnimationProperty_Duration:
	    return JS_NewFloat64 (ctx, image->getTextureAnimationDuration ());
	case TextureAnimationProperty_Rate:
	    return JS_NewFloat64 (ctx, image->getTextureAnimationRate ());
	default:
	    return JS_UNDEFINED;
    }
}

static JSValue texture_animation_rate_set (
    JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv, int magic, JSValueConst* functionData
) {
    auto* image = scriptable_animation_image (ctx, functionData);
    double rate = 1.0;
    if (image == nullptr || argc < 1 || JS_ToFloat64 (ctx, &rate, argv[0]) < 0) {
	return JS_ThrowTypeError (ctx, "rate expects a number");
    }

    image->setTextureAnimationRate (static_cast<float> (rate));
    return JS_UNDEFINED;
}

static JSValue
scriptable_texture_animation_controller (JSContext* ctx, WallpaperEngine::Render::Objects::CImage& image) {
    JSValue functionData[] = { JS_NewBigInt64 (ctx, reinterpret_cast<int64_t> (&image)) };
    JSValue result = JS_NewObject (ctx);

    const auto defineReadOnly = [&] (const char* name, const int property) {
	JS_DefinePropertyGetSet (
	    ctx, result, JS_NewAtom (ctx, name),
	    JS_NewCFunctionData (ctx, texture_animation_property_get, 0, property, 1, functionData), JS_UNDEFINED,
	    JS_PROP_ENUMERABLE
	);
    };
    defineReadOnly ("frameCount", TextureAnimationProperty_FrameCount);
    defineReadOnly ("duration", TextureAnimationProperty_Duration);
    JS_DefinePropertyGetSet (
	ctx, result, JS_NewAtom (ctx, "rate"),
	JS_NewCFunctionData (ctx, texture_animation_property_get, 0, TextureAnimationProperty_Rate, 1, functionData),
	JS_NewCFunctionData (ctx, texture_animation_rate_set, 1, TextureAnimationProperty_Rate, 1, functionData),
	JS_PROP_ENUMERABLE
    );

    const auto defineMethod = [&] (const char* name, const int command, const int length) {
	JS_SetPropertyStr (
	    ctx, result, name, JS_NewCFunctionData (ctx, texture_animation_command, length, command, 1, functionData)
	);
    };
    defineMethod ("play", TextureAnimationCommand_Play, 0);
    defineMethod ("pause", TextureAnimationCommand_Pause, 0);
    defineMethod ("stop", TextureAnimationCommand_Stop, 0);
    defineMethod ("isPlaying", TextureAnimationCommand_IsPlaying, 0);
    defineMethod ("getFrame", TextureAnimationCommand_GetFrame, 0);
    defineMethod ("setFrame", TextureAnimationCommand_SetFrame, 1);
    defineMethod ("join", TextureAnimationCommand_Join, 0);

    JS_FreeValue (ctx, functionData[0]);
    return result;
}

static JSValue scriptable_get_texture_animation (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    auto* image = scriptable_image (thisVal);
    return image != nullptr && image->hasTextureAnimation () ? scriptable_texture_animation_controller (ctx, *image)
							     : JS_UNDEFINED;
}

static JSValue make_script_mat4 (JSContext* ctx, const glm::mat4& matrix) {
    JSValue values = JS_NewArray (ctx);
    uint32_t index = 0;
    for (int column = 0; column < 4; column++) {
	for (int row = 0; row < 4; row++) {
	    JS_SetPropertyUint32 (ctx, values, index++, JS_NewFloat64 (ctx, matrix[column][row]));
	}
    }

    JSValue global = JS_GetGlobalObject (ctx);
    JSValue constructor = JS_GetPropertyStr (ctx, global, "Mat4");
    JSValue result = JS_CallConstructor (ctx, constructor, 1, &values);
    JS_FreeValue (ctx, constructor);
    JS_FreeValue (ctx, global);
    JS_FreeValue (ctx, values);
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
    const bool valid = JS_IsNumber (fields[0]) && JS_IsNumber (fields[1]);
    double components[] = { 0.0, 0.0, 0.0 };
    if (valid) {
	JS_ToFloat64 (ctx, &components[0], fields[0]);
	JS_ToFloat64 (ctx, &components[1], fields[1]);
	if (JS_IsNumber (fields[2])) {
	    JS_ToFloat64 (ctx, &components[2], fields[2]);
	}
    }
    for (const auto& field : fields) {
	JS_FreeValue (ctx, field);
    }

    result = glm::vec3 (components[0], components[1], components[2]);
    return valid;
}

static JSValue scriptable_get_transform_matrix (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    const auto* container = scriptable_container (thisVal);
    return container != nullptr ? make_script_mat4 (ctx, container->object.resolveWorldMatrix ()) : JS_UNDEFINED;
}

static JSValue scriptable_get_parent (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    auto* container = scriptable_container (thisVal);
    if (container == nullptr || !container->object.getObject ().parent.has_value ()) {
	return JS_UNDEFINED;
    }

    const auto* parent = container->object.getScene ().getObject (*container->object.getObject ().parent);
    const auto* scriptable = dynamic_cast<const WallpaperEngine::Scripting::ScriptableObject*> (parent);
    return scriptable != nullptr ? container->adapter.getEngine ().getAdapters ().object->instantiate (
				       const_cast<WallpaperEngine::Scripting::ScriptableObject&> (*scriptable)
				   )
				 : JS_UNDEFINED;
}

static JSValue scriptable_get_children (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    auto* container = scriptable_container (thisVal);
    JSValue result = JS_NewArray (ctx);
    if (container == nullptr) {
	return result;
    }

    uint32_t index = 0;
    for (auto* child : container->object.getScene ().getObjectsByRenderOrder ()) {
	if (child == nullptr || !child->getObject ().parent.has_value ()
	    || *child->getObject ().parent != container->object.getId ()) {
	    continue;
	}
	if (auto* scriptable = dynamic_cast<WallpaperEngine::Scripting::ScriptableObject*> (child);
	    scriptable != nullptr) {
	    JS_SetPropertyUint32 (
		ctx, result, index++, container->adapter.getEngine ().getAdapters ().object->instantiate (*scriptable)
	    );
	}
    }
    return result;
}

static std::optional<std::string>
resolve_attachment_name (JSContext* ctx, const WallpaperEngine::Render::CObject& object, JSValueConst value) {
    if (JS_IsNumber (value)) {
	int32_t index = -1;
	if (JS_ToInt32 (ctx, &index, value) < 0 || index < 0) {
	    return std::nullopt;
	}
	return object.getAttachmentName (static_cast<size_t> (index));
    }

    const char* name = JS_ToCString (ctx, value);
    if (name == nullptr) {
	return std::nullopt;
    }
    std::string result (name);
    JS_FreeCString (ctx, name);
    return result;
}

static glm::mat4 attachment_world_matrix (
    JSContext* ctx, const WallpaperEngine::Scripting::ScriptableObject& object, int argc, JSValueConst* argv
) {
    const glm::mat4 world = object.resolveWorldMatrix ();
    if (argc < 1 || JS_IsNull (argv[0]) || JS_IsUndefined (argv[0])) {
	return world;
    }

    const auto name = resolve_attachment_name (ctx, object, argv[0]);
    if (!name.has_value ()) {
	return world;
    }
    const auto local = object.getAttachmentTransform (*name);
    return local.has_value () ? world * *local : world;
}

static JSValue scriptable_get_attachment_index (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    auto* container = scriptable_container (thisVal);
    if (container == nullptr || argc < 1) {
	return JS_NewInt32 (ctx, -1);
    }

    const char* name = JS_ToCString (ctx, argv[0]);
    if (name == nullptr) {
	return JS_NewInt32 (ctx, -1);
    }
    const auto index = container->object.getAttachmentIndex (name);
    JS_FreeCString (ctx, name);
    return JS_NewInt64 (ctx, index.has_value () ? static_cast<int64_t> (*index) : -1);
}

static JSValue scriptable_get_attachment_matrix (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    auto* container = scriptable_container (thisVal);
    return container != nullptr ? make_script_mat4 (ctx, attachment_world_matrix (ctx, container->object, argc, argv))
				: JS_UNDEFINED;
}

static JSValue scriptable_get_attachment_origin (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    auto* container = scriptable_container (thisVal);
    if (container == nullptr) {
	return JS_UNDEFINED;
    }
    DynamicValue origin (glm::vec3 (attachment_world_matrix (ctx, container->object, argc, argv)[3]));
    return container->adapter.getEngine ().getAdapters ().vec3->instantiate (origin, true);
}

static JSValue scriptable_get_attachment_angles (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    auto* container = scriptable_container (thisVal);
    if (container == nullptr) {
	return JS_UNDEFINED;
    }

    glm::vec3 scale, translation, skew;
    glm::vec4 perspective;
    glm::quat orientation;
    glm::decompose (
	attachment_world_matrix (ctx, container->object, argc, argv), scale, orientation, translation, skew,
	perspective
    );
    DynamicValue angles (glm::degrees (glm::eulerAngles (glm::normalize (orientation))));
    return container->adapter.getEngine ().getAdapters ().vec3->instantiate (angles, true);
}

static WallpaperEngine::Scripting::ScriptableObject*
resolve_scriptable_parent (JSContext* ctx, OpaqueScriptableObjectAdapter& container, JSValueConst value) {
    if (JS_IsNull (value) || JS_IsUndefined (value)) {
	return nullptr;
    }
    if (auto* scriptable = ScriptableObjectAdapter::fromJS (value); scriptable != nullptr) {
	return scriptable;
    }

    auto& scene = container.object.getScene ();
    if (JS_IsNumber (value)) {
	int32_t id = -1;
	if (JS_ToInt32 (ctx, &id, value) >= 0) {
	    if (auto* object = const_cast<WallpaperEngine::Render::CObject*> (scene.getObject (id));
		object != nullptr) {
		return dynamic_cast<WallpaperEngine::Scripting::ScriptableObject*> (object);
	    }
	}
    }

    const char* requestedName = JS_ToCString (ctx, value);
    if (requestedName == nullptr) {
	return nullptr;
    }
    WallpaperEngine::Scripting::ScriptableObject* result = nullptr;
    for (auto* object : scene.getObjectsByRenderOrder ()) {
	if (object != nullptr && object->getObject ().name == requestedName) {
	    result = dynamic_cast<WallpaperEngine::Scripting::ScriptableObject*> (object);
	    break;
	}
    }
    JS_FreeCString (ctx, requestedName);
    return result;
}

static bool would_create_parent_cycle (
    const WallpaperEngine::Scripting::ScriptableObject& child,
    const WallpaperEngine::Scripting::ScriptableObject& parent
) {
    constexpr int kMaxParentDepth = 64;
    const auto* current = &parent;
    for (int depth = 0; current != nullptr && depth < kMaxParentDepth; depth++) {
	if (current->getId () == child.getId ()) {
	    return true;
	}
	if (!current->getObject ().parent.has_value ()) {
	    return false;
	}
	const auto* next = current->getScene ().getObject (*current->getObject ().parent);
	current = dynamic_cast<const WallpaperEngine::Scripting::ScriptableObject*> (next);
    }
    return current != nullptr;
}

static void apply_local_transform (WallpaperEngine::Scripting::ScriptableObject& object, const glm::mat4& local) {
    glm::vec3 scale (1.0f);
    glm::quat orientation (1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 translation (0.0f);
    glm::vec3 skew (0.0f);
    glm::vec4 perspective (0.0f);
    if (!glm::decompose (local, scale, orientation, translation, skew, perspective)) {
	return;
    }

    object.getProperty ("origin").update (translation, DynamicValue::UpdateSource::User);
    object.getProperty ("scale").update (scale, DynamicValue::UpdateSource::User);
    object.getProperty ("angles").update (
	glm::eulerAngles (glm::normalize (orientation)), DynamicValue::UpdateSource::User
    );
}

static JSValue scriptable_set_parent (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    auto* container = scriptable_container (thisVal);
    if (container == nullptr || argc < 1) {
	return JS_ThrowTypeError (ctx, "setParent() expects a parent layer or undefined");
    }

    const bool clearParent = JS_IsNull (argv[0]) || JS_IsUndefined (argv[0]);
    auto* parent = resolve_scriptable_parent (ctx, *container, argv[0]);
    if (!clearParent && parent == nullptr) {
	return JS_ThrowTypeError (ctx, "setParent() could not resolve the parent layer");
    }
    if (parent != nullptr && would_create_parent_cycle (container->object, *parent)) {
	return JS_ThrowTypeError (ctx, "setParent() would create a parent cycle");
    }

    std::optional<std::string> attachment;
    bool adjustTransforms = false;
    if (argc >= 2 && JS_IsBool (argv[1])) {
	adjustTransforms = JS_ToBool (ctx, argv[1]) != 0;
    } else if (argc >= 2 && !JS_IsNull (argv[1]) && !JS_IsUndefined (argv[1])) {
	if (parent != nullptr) {
	    attachment = resolve_attachment_name (ctx, *parent, argv[1]);
	}
	if (argc >= 3) {
	    adjustTransforms = JS_ToBool (ctx, argv[2]) != 0;
	}
    }

    const glm::mat4 oldWorld = container->object.resolveWorldMatrix ();
    auto& object = const_cast<Object&> (container->object.getObject ());
    object.parent = parent != nullptr ? std::optional<int> (parent->getId ()) : std::nullopt;
    object.attachment = parent != nullptr ? attachment : std::nullopt;

    if (adjustTransforms) {
	glm::mat4 parentWorld (1.0f);
	if (parent != nullptr) {
	    parentWorld = parent->resolveWorldMatrix ();
	    if (attachment.has_value ()) {
		if (const auto attachmentTransform = parent->getAttachmentTransform (*attachment);
		    attachmentTransform.has_value ()) {
		    parentWorld *= *attachmentTransform;
		}
	    }
	}
	apply_local_transform (container->object, glm::inverse (parentWorld) * oldWorld);
    }
    return JS_UNDEFINED;
}

static JSValue scriptable_rotate_object_space (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    auto* container = scriptable_container (thisVal);
    glm::vec3 degrees (0.0f);
    if (container == nullptr || argc < 1 || !read_script_vec3 (ctx, argv[0], degrees)) {
	return JS_ThrowTypeError (ctx, "rotateObjectSpace() expects a Vec3");
    }

    auto& angles = container->object.getProperty ("angles");
    const glm::quat current = glm::quat (angles.getVec3 ());
    const glm::quat delta = glm::quat (glm::radians (degrees));
    angles.update (glm::eulerAngles (glm::normalize (current * delta)), DynamicValue::UpdateSource::User);
    return JS_UNDEFINED;
}

static void
set_look_at_orientation (WallpaperEngine::Scripting::ScriptableObject& object, glm::vec3 direction, glm::vec3 up) {
    if (glm::dot (direction, direction) < 1e-8f || glm::dot (up, up) < 1e-8f) {
	return;
    }
    direction = glm::normalize (direction);
    up = glm::normalize (up);
    if (glm::dot (glm::cross (direction, up), glm::cross (direction, up)) < 1e-8f) {
	up = std::abs (direction.y) < 0.999f ? glm::vec3 (0.0f, 1.0f, 0.0f) : glm::vec3 (1.0f, 0.0f, 0.0f);
    }

    glm::quat worldOrientation = glm::quatLookAt (direction, up);
    if (object.getObject ().parent.has_value ()) {
	if (const auto* parent = object.getScene ().getObject (*object.getObject ().parent); parent != nullptr) {
	    glm::vec3 scale, translation, skew;
	    glm::vec4 perspective;
	    glm::quat parentOrientation;
	    if (glm::decompose (
		    parent->resolveWorldMatrix (), scale, parentOrientation, translation, skew, perspective
		)) {
		worldOrientation = glm::inverse (glm::normalize (parentOrientation)) * worldOrientation;
	    }
	}
    }
    object.getProperty ("angles").update (
	glm::eulerAngles (glm::normalize (worldOrientation)), DynamicValue::UpdateSource::User
    );
}

static JSValue
scriptable_look_at_common (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv, const bool yawOnly) {
    auto* container = scriptable_container (thisVal);
    glm::vec3 center (0.0f);
    glm::vec3 up (0.0f, 1.0f, 0.0f);
    if (container == nullptr || argc < 1 || !read_script_vec3 (ctx, argv[0], center)
	|| (argc >= 2 && !read_script_vec3 (ctx, argv[1], up))) {
	return JS_ThrowTypeError (
	    ctx, yawOnly ? "lookAtYaw() expects a center Vec3" : "lookAt() expects a center Vec3"
	);
    }

    const glm::vec3 origin = glm::vec3 (container->object.resolveWorldMatrix ()[3]);
    glm::vec3 direction = center - origin;
    if (yawOnly && glm::dot (up, up) > 1e-8f) {
	const glm::vec3 normalizedUp = glm::normalize (up);
	direction -= normalizedUp * glm::dot (direction, normalizedUp);
    }
    set_look_at_orientation (container->object, direction, up);
    return JS_UNDEFINED;
}

static JSValue scriptable_look_at (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    return scriptable_look_at_common (ctx, thisVal, argc, argv, false);
}

static JSValue scriptable_look_at_yaw (JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
    return scriptable_look_at_common (ctx, thisVal, argc, argv, true);
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
    if (std::strcmp (name, "getTransformMatrix") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_transform_matrix, name, 0);
    }
    if (std::strcmp (name, "rotateObjectSpace") == 0) {
	return JS_NewCFunction (ctx, scriptable_rotate_object_space, name, 1);
    }
    if (std::strcmp (name, "lookAt") == 0) {
	return JS_NewCFunction (ctx, scriptable_look_at, name, 2);
    }
    if (std::strcmp (name, "lookAtYaw") == 0) {
	return JS_NewCFunction (ctx, scriptable_look_at_yaw, name, 2);
    }
    if (std::strcmp (name, "setParent") == 0) {
	return JS_NewCFunction (ctx, scriptable_set_parent, name, 3);
    }
    if (std::strcmp (name, "getParent") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_parent, name, 0);
    }
    if (std::strcmp (name, "getChildren") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_children, name, 0);
    }
    if (std::strcmp (name, "getAttachmentIndex") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_attachment_index, name, 1);
    }
    if (std::strcmp (name, "getAttachmentMatrix") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_attachment_matrix, name, 1);
    }
    if (std::strcmp (name, "getAttachmentOrigin") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_attachment_origin, name, 1);
    }
    if (std::strcmp (name, "getAttachmentAngles") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_attachment_angles, name, 1);
    }

    if (std::strcmp (name, "getAnimation") == 0 || std::strcmp (name, "getAnimationLayer") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_animation, name, 1);
    }
    if (std::strcmp (name, "getTextureAnimation") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_texture_animation, name, 0);
    }
    if (std::strcmp (name, "getAnimationLayerCount") == 0) {
	return JS_NewCFunction (ctx, scriptable_get_animation_layer_count, name, 0);
    }

    try {
	// find the property inside, otherwise return undefined
	auto& property = container->object.getProperty (name);

	// SceneScript exposes layer Euler angles in degrees even though scene.json
	// and the renderer store them in radians.
	if (std::strcmp (name, "angles") == 0 && property.getType () == DynamicValue::Vec3) {
	    DynamicValue degrees (glm::degrees (property.getVec3 ()));
	    return container->adapter.getEngine ().getAdapters ().vec3->instantiate (degrees, true);
	}

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
	    glm::vec3 vector (component ("x"), component ("y"), component ("z"));
	    if (std::strcmp (name, "angles") == 0) {
		vector = glm::radians (vector);
	    }
	    property.update (vector, DynamicValue::UpdateSource::User);
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
	    .finalizer = scriptableobject_finalizer,
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
