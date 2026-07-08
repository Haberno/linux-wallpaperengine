#include "MathModule.h"

#include "WallpaperEngine/Scripting/ScriptEngine.h"

using namespace WallpaperEngine::Scripting::Modules;

#define min_f(a, b, c) (fminf (a, fminf (b, c)))
#define max_f(a, b, c) (fmaxf (a, fmaxf (b, c)))

static uint32_t MathModuleInstanceId = 0;
std::map<uint32_t, MathModule&> mathModules;
// Module-def -> instance id, so the (static) init callback can stamp the right magic on the exports.
static std::map<JSModuleDef*, uint32_t> mathModuleDefs;

JSValue wemath_smoothstep (JSContext*, JSValueConst, int, JSValueConst*, int);
JSValue wemath_mix (JSContext*, JSValueConst, int, JSValueConst*, int);

// QuickJS contract: declare exports with JS_AddModuleExport in the constructor (before linking), then
// assign their values with JS_SetModuleExport from inside the init callback (run at instantiation). The
// original code had these swapped, leaving every WEMath.* undefined ("not a function").
int wemath_init (JSContext* ctx, JSModuleDef* m) {
    const auto it = mathModuleDefs.find (m);
    const uint32_t instanceId = it != mathModuleDefs.end () ? it->second : 0;

    JS_SetModuleExport (
	ctx, m, "smoothStep",
	JS_NewCFunctionMagic (ctx, wemath_smoothstep, "smoothStep", 3, JS_CFUNC_generic_magic, instanceId)
    );
    JS_SetModuleExport (
	ctx, m, "mix", JS_NewCFunctionMagic (ctx, wemath_mix, "mix", 1, JS_CFUNC_generic_magic, instanceId)
    );
    JS_SetModuleExport (ctx, m, "deg2rad", JS_NewFloat64 (ctx, 0.01745329251994329576923690768489));
    JS_SetModuleExport (ctx, m, "rad2deg", JS_NewFloat64 (ctx, 57.295779513082320876798154814105));

    return 0;
}

JSValue wemath_smoothstep (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc != 3) {
	return JS_EXCEPTION;
    }

    if (!JS_IsNumber (argv[0]) || !JS_IsNumber (argv[1]) || !JS_IsNumber (argv[2])) {
	return JS_EXCEPTION;
    }

    double edge0 = 0.0f;
    double edge1 = 1.0f;
    double x = 0.0f;

    JS_ToFloat64 (ctx, &edge0, argv[0]);
    JS_ToFloat64 (ctx, &edge1, argv[1]);
    JS_ToFloat64 (ctx, &x, argv[2]);

    return JS_NewFloat64 (ctx, glm::smoothstep (edge0, edge1, x));
}

JSValue wemath_mix (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc != 3) {
	return JS_EXCEPTION;
    }

    if (!JS_IsNumber (argv[0]) || !JS_IsNumber (argv[1]) || !JS_IsNumber (argv[2])) {
	return JS_EXCEPTION;
    }

    double a = 0.0f;
    double b = 1.0f;
    double value = 0.0f;

    JS_ToFloat64 (ctx, &a, argv[0]);
    JS_ToFloat64 (ctx, &b, argv[1]);
    JS_ToFloat64 (ctx, &value, argv[2]);

    return JS_NewFloat64 (ctx, glm::mix (a, b, value));
}

MathModule::MathModule (ScriptEngine& engine) : ScriptModule (engine, "WEMath", wemath_init) {
    this->m_instanceId = ++MathModuleInstanceId;
    mathModules.emplace (this->m_instanceId, *this);
    mathModuleDefs.emplace (this->getDefinition (), this->m_instanceId);

    JSContext* ctx = this->getEngine ().getContext ();
    JS_AddModuleExport (ctx, this->getDefinition (), "smoothStep");
    JS_AddModuleExport (ctx, this->getDefinition (), "mix");
    JS_AddModuleExport (ctx, this->getDefinition (), "deg2rad");
    JS_AddModuleExport (ctx, this->getDefinition (), "rad2deg");
}

MathModule::~MathModule () {
    mathModules.erase (this->m_instanceId);
    mathModuleDefs.erase (this->getDefinition ());
}