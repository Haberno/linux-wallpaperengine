#include "EngineObject.h"
#include "ScriptEngine.h"
#include "WallpaperEngine/Audio/AudioContext.h"
#include "WallpaperEngine/Audio/Drivers/Recorders/PlaybackRecorder.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include <vector>

using namespace WallpaperEngine::Scripting;

extern float g_Time;
extern float g_TimeLast;
extern float g_Daytime;

static uint32_t EngineInstanceId = 0;
std::map<uint32_t, EngineObject&> engineInstances;

JSValue engine_set_value (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_ThrowTypeError (ctx, "Cannot assign to read-only property");
}

JSValue engine_open_user_shortcut (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_UNDEFINED;
}

JSValue engine_get_frametime (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewFloat64 (ctx, g_Time - g_TimeLast);
}

JSValue engine_get_runtime (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewFloat64 (ctx, g_Time);
}

JSValue engine_get_daytime (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewFloat64 (ctx, g_Daytime);
}

JSValue engine_get_screen_resolution (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSClassID classId;
    auto* engine = static_cast<EngineObject*> (JS_GetAnyOpaque (this_val, &classId));
    const auto& output = engine->getScene ().getContext ().getOutput ();

    JSValue result = engine->getEngine ().getAdapters ().vec2->instantiate ();

    JS_SetPropertyStr (ctx, result, "x", JS_NewFloat64 (ctx, output.getFullWidth ()));
    JS_SetPropertyStr (ctx, result, "y", JS_NewFloat64 (ctx, output.getFullHeight ()));

    return result;
}

JSValue engine_get_canvas_size (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSClassID classId;
    auto* engine = static_cast<EngineObject*> (JS_GetAnyOpaque (this_val, &classId));

    JSValue result = engine->getEngine ().getAdapters ().vec2->instantiate ();

    JS_SetPropertyStr (ctx, result, "x", JS_NewFloat64 (ctx, engine->getScene ().getWidth ()));
    JS_SetPropertyStr (ctx, result, "y", JS_NewFloat64 (ctx, engine->getScene ().getHeight ()));

    return result;
}

JSValue engine_get_user_properties (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSClassID classId;
    auto* engine = static_cast<EngineObject*> (JS_GetAnyOpaque (this_val, &classId));
    JSValue result = JS_NewObject (ctx);

    for (const auto& [name, property] : engine->getScene ().getScene ().project.properties) {
	JS_SetPropertyStr (ctx, result, name.c_str (), engine->getEngine ().dynamicToJs (*property));
    }

    return result;
}

// setTimeout/setInterval return this callable as their cancel closure (built via
// JS_NewCFunctionData below): the timer id is captured in func_data[0] and the engine
// instance id in the function's magic. Calling it -- real WE's documented `if (t) t();`
// idiom -- cancels the pending timer. Called with no arguments, so it must not read argv[].
JSValue engine_stop_interval (
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValueConst* func_data
) {
    int id = 0;

    JS_ToInt32 (ctx, &id, func_data[0]);

    const auto it = engineInstances.find (magic);

    if (it != engineInstances.end ()) {
	it->second.clearInterval (id);
    }

    return JS_UNDEFINED;
}

// mirror of engine_stop_interval for setTimeout's returned cancel closure
JSValue engine_stop_timeout (
    JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValueConst* func_data
) {
    int id = 0;

    JS_ToInt32 (ctx, &id, func_data[0]);

    const auto it = engineInstances.find (magic);

    if (it != engineInstances.end ()) {
	it->second.clearTimeout (id);
    }

    return JS_UNDEFINED;
}

JSValue engine_set_interval (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc < 1) {
	return JS_ThrowTypeError (ctx, "setInterval() requires at least 1 argument (callback)");
    }

    int delay = 0;

    if (argc > 1) {
	JS_ToInt32 (ctx, &delay, argv[1]);
    }

    JSValue function = argv[0];

    if (!JS_IsFunction (ctx, function)) {
	return JS_ThrowTypeError (ctx, "setInterval() argument 1 must be a function");
    }

    const auto it = engineInstances.find (magic);

    if (it == engineInstances.end ()) {
	return JS_ThrowReferenceError (ctx, "Could not find engine instance '%d' for setInterval", magic);
    }

    // the callback is stored past this call, so it needs its own reference
    int id = it->second.reserveNextIntervalId (JS_DupValue (ctx, function), delay);

    JSValue args[] = { JS_NewInt32 (ctx, id) };

    return JS_NewCFunctionData (ctx, engine_stop_interval, 2, magic, 1, args);
}

JSValue engine_register_audio_buffers (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    const auto it = engineInstances.find (magic);

    if (it == engineInstances.end ()) {
	return JS_ThrowReferenceError (ctx, "Could not find engine instance '%d' for registerAudioBuffers", magic);
    }

    int resolution = 16;

    if (argc > 0) {
	if (JS_ToInt32 (ctx, &resolution, argv[0]) < 0) {
	    return JS_EXCEPTION;
	}
    }

    auto& recorder = it->second.getScene ().getAudioContext ().getRecorder ();
    float* left;
    float* right;
    float* average;

    switch (resolution) {
	case 16:
	    left = recorder.audio16Left;
	    right = recorder.audio16Right;
	    average = recorder.audio16Average;
	    break;
	case 32:
	    left = recorder.audio32Left;
	    right = recorder.audio32Right;
	    average = recorder.audio32Average;
	    break;
	case 64:
	    left = recorder.audio64Left;
	    right = recorder.audio64Right;
	    average = recorder.audio64Average;
	    break;
	default:
	    return JS_ThrowRangeError (ctx, "Audio buffer resolution must be 16, 32, or 64");
    }

    // Live Float32Array views match the native DLL's three independent buffers. The
    // recorder outlives the script engine, so their backing memory remains valid.
    const auto makeView = [ctx, resolution] (float* data) {
	JSValue buffer = JS_NewArrayBuffer (
	    ctx, reinterpret_cast<uint8_t*> (data), resolution * sizeof (float), nullptr, nullptr, false
	);
	JSValue view = JS_NewTypedArray (ctx, 1, &buffer, JS_TYPED_ARRAY_FLOAT32);
	JS_FreeValue (ctx, buffer);
	return view;
    };

    JSValue leftView = makeView (left);
    if (JS_IsException (leftView)) {
	return leftView;
    }
    JSValue rightView = makeView (right);
    if (JS_IsException (rightView)) {
	JS_FreeValue (ctx, leftView);
	return rightView;
    }
    JSValue averageView = makeView (average);
    if (JS_IsException (averageView)) {
	JS_FreeValue (ctx, leftView);
	JS_FreeValue (ctx, rightView);
	return averageView;
    }

    JSValue result = JS_NewObject (ctx);
    JS_SetPropertyStr (ctx, result, "left", leftView);
    JS_SetPropertyStr (ctx, result, "right", rightView);
    JS_SetPropertyStr (ctx, result, "average", averageView);

    return result;
}

JSValue engine_set_timeout (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc < 1) {
	return JS_ThrowTypeError (ctx, "setTimeout() requires at least 1 argument (callback)");
    }

    int delay = 0;

    if (argc > 1) {
	JS_ToInt32 (ctx, &delay, argv[1]);
    }

    JSValue function = argv[0];

    if (!JS_IsFunction (ctx, function)) {
	return JS_ThrowTypeError (ctx, "setTimeout() argument 1 must be a function");
    }

    const auto it = engineInstances.find (magic);

    if (it == engineInstances.end ()) {
	return JS_ThrowReferenceError (ctx, "Could not find engine instance '%d' for setTimeout", magic);
    }

    // the callback is stored past this call, so it needs its own reference
    int id = it->second.reserveNextTimeoutId (JS_DupValue (ctx, function), delay);

    JSValue args[] = { JS_NewInt32 (ctx, id) };

    return JS_NewCFunctionData (ctx, engine_stop_timeout, 2, magic, 1, args);
}

EngineObject::EngineObject (ScriptEngine& engine, Render::Wallpapers::CScene& scene) :
    m_scene (scene), m_engine (engine), m_instanceId (++EngineInstanceId), m_classId (0) {
    engineInstances.emplace (this->m_instanceId, *this);
    this->m_definition = { .class_name = "IEngine" };
    JS_NewClassID (this->m_engine.getRuntime (), &this->m_classId);
    JS_NewClass (this->m_engine.getRuntime (), this->m_classId, &this->m_definition);
    this->m_instance = JS_NewObjectClass (this->m_engine.getContext (), this->m_classId);

    JS_DupValue (this->m_engine.getContext (), this->m_instance);

    // set properties
    JS_SetOpaque (this->m_instance, this);
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "frametime"),
	JS_NewCFunction (this->m_engine.getContext (), engine_get_frametime, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), engine_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "runtime"),
	JS_NewCFunction (this->m_engine.getContext (), engine_get_runtime, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), engine_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "timeOfDay"),
	JS_NewCFunction (this->m_engine.getContext (), engine_get_daytime, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), engine_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "screenResolution"),
	JS_NewCFunction (this->m_engine.getContext (), engine_get_screen_resolution, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), engine_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "canvasSize"),
	JS_NewCFunction (this->m_engine.getContext (), engine_get_canvas_size, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), engine_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyGetSet (
	this->m_engine.getContext (), this->m_instance, JS_NewAtom (this->m_engine.getContext (), "userProperties"),
	JS_NewCFunction (this->m_engine.getContext (), engine_get_user_properties, "get", 0),
	JS_NewCFunction (this->m_engine.getContext (), engine_set_value, "set", 1), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "AUDIO_RESOLUTION_16",
	JS_NewInt32 (this->m_engine.getContext (), 16), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "AUDIO_RESOLUTION_32",
	JS_NewInt32 (this->m_engine.getContext (), 32), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "AUDIO_RESOLUTION_64",
	JS_NewInt32 (this->m_engine.getContext (), 64), JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "setInterval",
	JS_NewCFunctionMagic (
	    this->m_engine.getContext (), engine_set_interval, "setInterval", 2, JS_CFUNC_generic_magic,
	    this->m_instanceId
	),
	JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "setTimeout",
	JS_NewCFunctionMagic (
	    this->m_engine.getContext (), engine_set_timeout, "setTimeout", 2, JS_CFUNC_generic_magic,
	    this->m_instanceId
	),
	JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "registerAudioBuffers",
	JS_NewCFunctionMagic (
	    this->m_engine.getContext (), engine_register_audio_buffers, "registerAudioBuffers", 1,
	    JS_CFUNC_generic_magic, this->m_instanceId
	),
	JS_PROP_ENUMERABLE
    );
    JS_DefinePropertyValueStr (
	this->m_engine.getContext (), this->m_instance, "openUserShortcut",
	JS_NewCFunction (this->m_engine.getContext (), engine_open_user_shortcut, "openUserShortcut", 0),
	JS_PROP_ENUMERABLE
    );
    // TODO: ADD THE REST OF THE DEFINITION!
}

EngineObject::~EngineObject () {
    // clear all the timeouts and intervals
    for (const auto& [id, timeout] : this->m_timeouts) {
	JS_FreeValue (this->m_engine.getContext (), timeout.callback);
    }
    for (const auto& [id, interval] : this->m_intervals) {
	JS_FreeValue (this->m_engine.getContext (), interval.callback);
    }

    engineInstances.erase (this->m_instanceId);
    this->m_intervals.clear ();
    this->m_timeouts.clear ();

    JS_FreeValue (this->m_engine.getContext (), this->m_instance);
}

uint32_t EngineObject::reserveNextTimeoutId (JSValue function, uint64_t duration) {
    const auto id = ++this->m_nextTimeoutId;

    this->m_timeouts[id] = Timeout { .callback = function,
				     .duration = std::chrono::milliseconds (duration),
				     .next = std::chrono::steady_clock::now () + std::chrono::milliseconds (duration) };

    return id;
}

uint32_t EngineObject::reserveNextIntervalId (JSValue function, uint64_t duration) {
    const auto id = ++this->m_nextIntervalId;

    this->m_intervals[id]
	= Timeout { .callback = function,
		    .duration = std::chrono::milliseconds (duration),
		    .next = std::chrono::steady_clock::now () + std::chrono::milliseconds (duration) };

    return id;
}

void EngineObject::clearInterval (uint32_t id) {
    const auto it = this->m_intervals.find (id);

    if (it == this->m_intervals.end ()) {
	return;
    }

    JS_FreeValue (this->getEngine ().getContext (), it->second.callback);

    this->m_intervals.erase (id);
}

void EngineObject::clearTimeout (uint32_t id) {
    const auto it = this->m_timeouts.find (id);

    if (it == this->m_timeouts.end ()) {
	return;
    }

    JS_FreeValue (this->getEngine ().getContext (), it->second.callback);

    this->m_timeouts.erase (id);
}

void EngineObject::tick () {
    const auto now = std::chrono::steady_clock::now ();

    JSContext* ctx = this->m_engine.getContext ();

    // Snapshot which intervals/timeouts are due *before* calling into any JS, since a callback
    // can reentrantly call engine.setInterval/setTimeout/clearInterval/clearTimeout on this
    // same EngineObject (a common self-rescheduling pattern), mutating m_intervals/m_timeouts
    // while we'd otherwise still be iterating them -- erasing the entry currently being visited
    // (e.g. a timeout clearing itself) leaves a dangling reference and crashes on next access.
    std::vector<uint32_t> dueIntervals;
    for (const auto& [id, timeout] : this->m_intervals) {
	if (timeout.next <= now) {
	    dueIntervals.push_back (id);
	}
    }

    // check any interval and run them if needed
    for (auto id : dueIntervals) {
	const auto it = this->m_intervals.find (id);

	if (it == this->m_intervals.end ()) {
	    continue; // cleared reentrantly by an earlier callback this tick
	}

	it->second.next = now + it->second.duration;

	// keep the callback alive across its own call: a reentrant clearInterval from within
	// the callback would otherwise free the JSValue we're still executing
	const JSValue callback = JS_DupValue (ctx, it->second.callback);
	JS_FreeValue (ctx, JS_Call (ctx, callback, JS_NULL, 0, nullptr));
	JS_FreeValue (ctx, callback);
    }

    std::vector<uint32_t> dueTimeouts;
    for (const auto& [id, timeout] : this->m_timeouts) {
	if (timeout.next <= now) {
	    dueTimeouts.push_back (id);
	}
    }

    // check any timeout and run them if needed
    for (auto id : dueTimeouts) {
	const auto it = this->m_timeouts.find (id);

	if (it == this->m_timeouts.end ()) {
	    continue; // cleared reentrantly by an earlier callback this tick
	}

	const JSValue callback = JS_DupValue (ctx, it->second.callback);
	JS_FreeValue (ctx, JS_Call (ctx, callback, JS_NULL, 0, nullptr));
	JS_FreeValue (ctx, callback);

	// the callback may have already cleared this exact timeout (self-clearing pattern);
	// re-lookup rather than reusing `it`, which erase() may have invalidated, and only
	// free/erase what's still actually there
	const auto current = this->m_timeouts.find (id);

	if (current != this->m_timeouts.end ()) {
	    JS_FreeValue (ctx, current->second.callback);
	    this->m_timeouts.erase (current);
	}
    }
}
