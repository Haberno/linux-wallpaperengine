#include "VectorModule.h"

#include "WallpaperEngine/Scripting/ScriptEngine.h"

#include <cmath>
#include <map>

using namespace WallpaperEngine::Scripting::Modules;

static constexpr double RAD2DEG = 57.295779513082320876798154814105;

static uint32_t VectorModuleInstanceId = 0;
// Module-def -> instance id, so the (static) init callback can stamp the right magic on the exports.
static std::map<JSModuleDef*, uint32_t> vectorModuleDefs;

JSValue wevector_angle2 (JSContext*, JSValueConst, int, JSValueConst*, int);
JSValue wevector_anglevector2 (JSContext*, JSValueConst, int, JSValueConst*, int);

// QuickJS contract: JS_AddModuleExport declares exports (constructor); JS_SetModuleExport assigns
// their values from the init callback. These were swapped before, leaving WEVector.* undefined.
int wevector_init (JSContext* ctx, JSModuleDef* m) {
    const auto it = vectorModuleDefs.find (m);
    const uint32_t instanceId = it != vectorModuleDefs.end () ? it->second : 0;

    JS_SetModuleExport (
	ctx, m, "vectorAngle2",
	JS_NewCFunctionMagic (ctx, wevector_angle2, "vectorAngle2", 1, JS_CFUNC_generic_magic, instanceId)
    );
    JS_SetModuleExport (
	ctx, m, "angleVector2",
	JS_NewCFunctionMagic (ctx, wevector_anglevector2, "angleVector2", 1, JS_CFUNC_generic_magic, instanceId)
    );
    return 0;
}

// vectorAngle2(Vec2) -> heading in degrees. Reads .x/.y off the vector object (a VectorAdapter
// Vec2 or any {x,y}). Wallpaper Engine uses this for the camera's heading angle.
JSValue wevector_angle2 (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc != 1) {
	return JS_EXCEPTION;
    }

    double x = 0.0;
    double y = 0.0;
    JSValue jx = JS_GetPropertyStr (ctx, argv[0], "x");
    JSValue jy = JS_GetPropertyStr (ctx, argv[0], "y");
    JS_ToFloat64 (ctx, &x, jx);
    JS_ToFloat64 (ctx, &y, jy);
    JS_FreeValue (ctx, jx);
    JS_FreeValue (ctx, jy);

    return JS_NewFloat64 (ctx, std::atan2 (y, x) * RAD2DEG);
}

// angleVector2(angleDegrees) -> unit Vec2 pointing along that heading. Inverse of vectorAngle2.
JSValue wevector_anglevector2 (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc != 1) {
	return JS_EXCEPTION;
    }

    double angle = 0.0;
    JS_ToFloat64 (ctx, &angle, argv[0]);
    const double rad = angle / RAD2DEG;

    // Build a Vec2 through the global constructor so it's a real engine Vec2 (with all its methods).
    JSValue global = JS_GetGlobalObject (ctx);
    JSValue ctor = JS_GetPropertyStr (ctx, global, "Vec2");
    JSValue args[2] = { JS_NewFloat64 (ctx, std::cos (rad)), JS_NewFloat64 (ctx, std::sin (rad)) };
    JSValue result = JS_CallConstructor (ctx, ctor, 2, args);
    JS_FreeValue (ctx, args[0]);
    JS_FreeValue (ctx, args[1]);
    JS_FreeValue (ctx, ctor);
    JS_FreeValue (ctx, global);
    return result;
}

VectorModule::VectorModule (ScriptEngine& engine) : ScriptModule (engine, "WEVector", wevector_init) {
    this->m_instanceId = ++VectorModuleInstanceId;
    vectorModuleDefs.emplace (this->getDefinition (), this->m_instanceId);

    JS_AddModuleExport (this->getEngine ().getContext (), this->getDefinition (), "vectorAngle2");
    JS_AddModuleExport (this->getEngine ().getContext (), this->getDefinition (), "angleVector2");
}

VectorModule::~VectorModule () { vectorModuleDefs.erase (this->getDefinition ()); }
