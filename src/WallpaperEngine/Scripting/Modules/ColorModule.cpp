#include "ColorModule.h"

#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

#include <algorithm>
#include <cmath>

using namespace WallpaperEngine::Scripting::Modules;
using WallpaperEngine::Data::Utils::ScopeGuard;

#define min_f(a, b, c) (fminf (a, fminf (b, c)))
#define max_f(a, b, c) (fmaxf (a, fmaxf (b, c)))

static uint32_t ColorModuleInstanceId = 0;
std::map<uint32_t, ColorModule&> colorModules;
// Maps each module definition to its instance id so the (static) init callback can stamp the right
// magic on the exported functions. Populated in the constructor, consumed in wecolor_init.
static std::map<JSModuleDef*, uint32_t> colorModuleDefs;

JSValue wecolor_rgb2hsv (JSContext*, JSValueConst, int, JSValueConst*, int);
JSValue wecolor_hsv2rgb (JSContext*, JSValueConst, int, JSValueConst*, int);
JSValue wecolor_normalizecolor (JSContext*, JSValueConst, int, JSValueConst*, int);
JSValue wecolor_expandcolor (JSContext*, JSValueConst, int, JSValueConst*, int);

// QuickJS contract: JS_AddModuleExport declares exports (done in the constructor, before the module is
// linked); JS_SetModuleExport must assign their values from inside the init callback (run at module
// instantiation). The original code had these swapped — Set ran in the constructor before the exports
// existed, so every WEColor.* stayed undefined ("not a function") and any importing script threw.
int wecolor_init (JSContext* ctx, JSModuleDef* m) {
    const auto it = colorModuleDefs.find (m);
    const uint32_t instanceId = it != colorModuleDefs.end () ? it->second : 0;

    JS_SetModuleExport (
	ctx, m, "rgb2hsv", JS_NewCFunctionMagic (ctx, wecolor_rgb2hsv, "rgb2hsv", 1, JS_CFUNC_generic_magic, instanceId)
    );
    JS_SetModuleExport (
	ctx, m, "hsv2rgb", JS_NewCFunctionMagic (ctx, wecolor_hsv2rgb, "hsv2rgb", 1, JS_CFUNC_generic_magic, instanceId)
    );
    JS_SetModuleExport (
	ctx, m, "normalizeColor",
	JS_NewCFunctionMagic (ctx, wecolor_normalizecolor, "normalizeColor", 1, JS_CFUNC_generic_magic, instanceId)
    );
    JS_SetModuleExport (
	ctx, m, "expandColor",
	JS_NewCFunctionMagic (ctx, wecolor_expandcolor, "expandColor", 1, JS_CFUNC_generic_magic, instanceId)
    );

    return 0;
}

JSValue wecolor_rgb2hsv (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc != 1) {
	return JS_ThrowTypeError (ctx, "rgb2hsv() expects 1 argument");
    }

    if (JS_VALUE_GET_TAG (argv[0]) != JS_TAG_OBJECT) {
	return JS_ThrowTypeError (ctx, "rgb2hsv() expects an object with x, y, z properties");
    }

    JSValue x = JS_GetPropertyStr (ctx, argv[0], "x");
    JSValue y = JS_GetPropertyStr (ctx, argv[0], "y");
    JSValue z = JS_GetPropertyStr (ctx, argv[0], "z");

    ScopeGuard guard ([ctx, x, y, z] () {
	JS_FreeValue (ctx, x);
	JS_FreeValue (ctx, y);
	JS_FreeValue (ctx, z);
    });

    double xVal = 0.0f, yVal = 0.0f, zVal = 0.0f;

    JS_ToFloat64 (ctx, &xVal, x);
    JS_ToFloat64 (ctx, &yVal, y);
    JS_ToFloat64 (ctx, &zVal, z);

    // conversion code from https://gist.github.com/yoggy/8999625
    float h, s, v; // h:0-360.0, s:0.0-1.0, v:0.0-1.0

    float max = max_f (xVal, yVal, zVal);
    float min = min_f (xVal, yVal, zVal);

    v = max;

    if (max == 0.0f) {
	s = 0;
	h = 0;
    } else if (max - min == 0.0f) {
	s = 0;
	h = 0;
    } else {
	s = (max - min) / max;

	if (max == xVal) {
	    h = 60 * ((yVal - zVal) / (max - min)) + 0;
	} else if (max == yVal) {
	    h = 60 * ((zVal - xVal) / (max - min)) + 120;
	} else {
	    h = 60 * ((xVal - yVal) / (max - min)) + 240;
	}
    }

    if (h < 0) {
	h += 360.0f;
    }

    // Wallpaper Engine's HSV convention is hue 0..1, not degrees
    h /= 360.0f;

    const auto it = colorModules.find (magic);

    if (it == colorModules.end ()) {
	return JS_UNDEFINED;
    }

    JSValue value = it->second.getEngine ().getAdapters ().vec3->instantiate ();

    JS_SetPropertyStr (ctx, value, "x", JS_NewFloat64 (ctx, h));
    JS_SetPropertyStr (ctx, value, "y", JS_NewFloat64 (ctx, s));
    JS_SetPropertyStr (ctx, value, "z", JS_NewFloat64 (ctx, v));

    return value;
}

JSValue wecolor_hsv2rgb (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc != 1) {
	return JS_ThrowTypeError (ctx, "hsv2rgb() expects 1 argument");
    }

    if (JS_VALUE_GET_TAG (argv[0]) != JS_TAG_OBJECT) {
	return JS_ThrowTypeError (ctx, "hsv2rgb() expects an object with x, y, z properties");
    }

    JSValue x = JS_GetPropertyStr (ctx, argv[0], "x");
    JSValue y = JS_GetPropertyStr (ctx, argv[0], "y");
    JSValue z = JS_GetPropertyStr (ctx, argv[0], "z");

    ScopeGuard guard ([ctx, x, y, z] () {
	JS_FreeValue (ctx, x);
	JS_FreeValue (ctx, y);
	JS_FreeValue (ctx, z);
    });

    double xVal = 0.0f, yVal = 0.0f, zVal = 0.0f;

    JS_ToFloat64 (ctx, &xVal, x);
    JS_ToFloat64 (ctx, &yVal, y);
    JS_ToFloat64 (ctx, &zVal, z);

    // conversion code from https://gist.github.com/yoggy/8999625, adapted to Wallpaper Engine's
    // convention: hue is 0..1 (not degrees) and wraps fractionally — WE's own color-cycle script
    // feeds hsv2rgb an ever-growing `engine.runtime * speed` and relies on the wrap for the rainbow.
    float r, g, b; // 0.0-1.0

    const double hue = (xVal - std::floor (xVal)) * 6.0;
    yVal = std::clamp (yVal, 0.0, 1.0);
    zVal = std::clamp (zVal, 0.0, 1.0);

    const int hi = static_cast<int> (hue) % 6;
    const float f = static_cast<float> (hue) - static_cast<float> (hi);
    const float p = zVal * (1.0f - yVal);
    const float q = zVal * (1.0f - yVal * f);
    const float t = zVal * (1.0f - yVal * (1.0f - f));

    switch (hi) {
	case 0:
	    r = zVal, g = t, b = p;
	    break;
	case 1:
	    r = q, g = zVal, b = p;
	    break;
	case 2:
	    r = p, g = zVal, b = t;
	    break;
	case 3:
	    r = p, g = q, b = zVal;
	    break;
	case 4:
	    r = t, g = p, b = zVal;
	    break;
	case 5:
	    r = zVal, g = p, b = q;
	    break;
	default:
	    r = 0, g = 0, b = 0;
	    break;
    }

    const auto it = colorModules.find (magic);

    if (it == colorModules.end ()) {
	return JS_UNDEFINED;
    }

    JSValue value = it->second.getEngine ().getAdapters ().vec3->instantiate ();

    JS_SetPropertyStr (ctx, value, "x", JS_NewFloat64 (ctx, r));
    JS_SetPropertyStr (ctx, value, "y", JS_NewFloat64 (ctx, g));
    JS_SetPropertyStr (ctx, value, "z", JS_NewFloat64 (ctx, b));

    return value;
}

JSValue wecolor_normalizecolor (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc != 1) {
	return JS_ThrowTypeError (ctx, "normalizeColor() expects 1 argument");
    }

    if (JS_VALUE_GET_TAG (argv[0]) != JS_TAG_OBJECT) {
	return JS_ThrowTypeError (ctx, "normalizeColor() expects an object with x, y, z properties");
    }

    const auto it = colorModules.find (magic);

    if (it == colorModules.end ()) {
	return JS_UNDEFINED;
    }

    JSValue x = JS_GetPropertyStr (ctx, argv[0], "x");
    JSValue y = JS_GetPropertyStr (ctx, argv[0], "y");
    JSValue z = JS_GetPropertyStr (ctx, argv[0], "z");

    ScopeGuard guard ([ctx, x, y, z] () {
	JS_FreeValue (ctx, x);
	JS_FreeValue (ctx, y);
	JS_FreeValue (ctx, z);
    });

    if (!JS_IsNumber (x) || !JS_IsNumber (y) || !JS_IsNumber (z)) {
	return JS_ThrowTypeError (ctx, "normalizeColor() argument must have numeric x, y, z properties");
    }

    double xVal = 0.0f, yVal = 0.0f, zVal = 0.0f;

    JS_ToFloat64 (ctx, &xVal, x);
    JS_ToFloat64 (ctx, &yVal, y);
    JS_ToFloat64 (ctx, &zVal, z);

    JSValue value = it->second.getEngine ().getAdapters ().vec3->instantiate ();

    JS_SetPropertyStr (ctx, value, "x", JS_NewFloat64 (ctx, xVal / 255.0f));
    JS_SetPropertyStr (ctx, value, "y", JS_NewFloat64 (ctx, yVal / 255.0f));
    JS_SetPropertyStr (ctx, value, "z", JS_NewFloat64 (ctx, zVal / 255.0f));

    return value;
}

JSValue wecolor_expandcolor (JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic) {
    if (argc != 1) {
	return JS_ThrowTypeError (ctx, "expandColor() expects 1 argument");
    }

    if (JS_VALUE_GET_TAG (argv[0]) != JS_TAG_OBJECT) {
	return JS_ThrowTypeError (ctx, "expandColor() expects an object with x, y, z properties");
    }

    const auto it = colorModules.find (magic);

    if (it == colorModules.end ()) {
	return JS_UNDEFINED;
    }

    JSValue x = JS_GetPropertyStr (ctx, argv[0], "x");
    JSValue y = JS_GetPropertyStr (ctx, argv[0], "y");
    JSValue z = JS_GetPropertyStr (ctx, argv[0], "z");

    ScopeGuard guard ([ctx, x, y, z] () {
	JS_FreeValue (ctx, x);
	JS_FreeValue (ctx, y);
	JS_FreeValue (ctx, z);
    });

    if (!JS_IsNumber (x) || !JS_IsNumber (y) || !JS_IsNumber (z)) {
	return JS_ThrowTypeError (ctx, "expandColor() argument must have numeric x, y, z properties");
    }

    double xVal = 0.0f, yVal = 0.0f, zVal = 0.0f;

    JS_ToFloat64 (ctx, &xVal, x);
    JS_ToFloat64 (ctx, &yVal, y);
    JS_ToFloat64 (ctx, &zVal, z);

    JSValue value = it->second.getEngine ().getAdapters ().vec3->instantiate ();

    JS_SetPropertyStr (ctx, value, "x", JS_NewFloat64 (ctx, xVal * 255.0f));
    JS_SetPropertyStr (ctx, value, "y", JS_NewFloat64 (ctx, yVal * 255.0f));
    JS_SetPropertyStr (ctx, value, "z", JS_NewFloat64 (ctx, zVal * 255.0f));

    return value;
}

ColorModule::ColorModule (ScriptEngine& engine) : ScriptModule (engine, "WEColor", wecolor_init) {
    this->m_instanceId = ++ColorModuleInstanceId;
    colorModules.emplace (this->m_instanceId, *this);
    colorModuleDefs.emplace (this->getDefinition (), this->m_instanceId);

    JSContext* ctx = this->getEngine ().getContext ();
    JS_AddModuleExport (ctx, this->getDefinition (), "rgb2hsv");
    JS_AddModuleExport (ctx, this->getDefinition (), "hsv2rgb");
    JS_AddModuleExport (ctx, this->getDefinition (), "normalizeColor");
    JS_AddModuleExport (ctx, this->getDefinition (), "expandColor");
}

ColorModule::~ColorModule () {
    colorModules.erase (this->m_instanceId);
    colorModuleDefs.erase (this->getDefinition ());
}