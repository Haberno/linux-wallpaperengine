#include "quickjs.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>

namespace {
struct Runtime {
    JSRuntime* runtime = JS_NewRuntime ();
    JSContext* context = JS_NewContext (runtime);
    ~Runtime () {
	JS_FreeContext (context);
	JS_FreeRuntime (runtime);
    }
};
JSValue captured (JSContext* context, JSValueConst, int, JSValueConst*, int, JSValue* data) {
    return JS_DupValue (context, data[0]);
}
JSValue retainArgument (JSContext* context, JSValueConst, int argc, JSValueConst* args) {
    return argc ? JS_NewCFunctionData (context, captured, 0, 0, 1, args) : JS_UNDEFINED;
}
void finalized (JSRuntime*, JSValueConst value) {
    JSClassID id;
    auto* count = static_cast<int*> (JS_GetAnyOpaque (value, &id));
    if (count) {
	++*count;
    }
}
}

TEST_CASE ("Script runtime strings preserve embedded nulls and Unicode", "[script-runtime]") {
    Runtime vm;
    const std::string input ("before\0after \xF0\x9F\x8C\x8A", 17);
    JSValue value = JS_NewStringLen (vm.context, input.data (), input.size ());
    REQUIRE_FALSE (JS_IsException (value));
    size_t length = 0;
    const char* bytes = JS_ToCStringLen (vm.context, &length, value);
    REQUIRE (bytes != nullptr);
    CHECK (length == input.size ());
    CHECK (std::string (bytes, length) == input);
    JS_FreeCString (vm.context, bytes);
    JS_FreeValue (vm.context, value);
}

TEST_CASE ("Script runtime callbacks retain captured objects and release native wrappers", "[script-runtime]") {
    int finalizers = 0;
    for (int iteration = 0; iteration < 20; ++iteration) {
	Runtime vm;
	JSClassID id = 0;
	JS_NewClassID (vm.runtime, &id);
	const JSClassDef definition { .class_name = "LifetimeProbe", .finalizer = finalized };
	REQUIRE (JS_NewClass (vm.runtime, id, &definition) == 0);
	JSValue object = JS_NewObjectClass (vm.context, id);
	JS_SetOpaque (object, &finalizers);
	JSValue fn = JS_NewCFunctionData (vm.context, captured, 0, 0, 1, &object);
	JS_FreeValue (vm.context, object);
	JSValue result = JS_Call (vm.context, fn, JS_UNDEFINED, 0, nullptr);
	REQUIRE_FALSE (JS_IsException (result));
	CHECK (JS_GetOpaque (result, id) == &finalizers);
	JS_FreeValue (vm.context, result);
	JS_FreeValue (vm.context, fn);
    }
    CHECK (finalizers == 20);
}

TEST_CASE ("Script runtime promotes callback arguments retained beyond their call", "[script-runtime]") {
    Runtime vm;
    JSValue global = JS_GetGlobalObject (vm.context);
    JS_SetPropertyStr (vm.context, global, "retain", JS_NewCFunction (vm.context, retainArgument, "retain", 1));
    const char* source = "const later = retain({value: 17}); later().value";
    JSValue result = JS_Eval (vm.context, source, strlen (source), "retain.js", JS_EVAL_TYPE_GLOBAL);
    REQUIRE_FALSE (JS_IsException (result));
    int32_t number = 0;
    REQUIRE (JS_ToInt32 (vm.context, &number, result) == 0);
    CHECK (number == 17);
    JS_FreeValue (vm.context, result);
    JS_FreeValue (vm.context, global);
}

TEST_CASE ("Script runtime native functions preserve property descriptors", "[script-runtime]") {
    Runtime vm;
    JSValue global = JS_GetGlobalObject (vm.context);
    JS_SetPropertyStr (vm.context, global, "retain", JS_NewCFunction (vm.context, retainArgument, "retain", 1));
    const char* source = R"JS(
        const name = Object.getOwnPropertyDescriptor(retain, 'name');
        const length = Object.getOwnPropertyDescriptor(retain, 'length');
        name.value === 'retain' && length.value === 1 &&
        !name.writable && !name.enumerable && name.configurable &&
        !length.writable && !length.enumerable && length.configurable &&
        Object.getPrototypeOf(retain) === Function.prototype
    )JS";
    JSValue result = JS_Eval (vm.context, source, strlen (source), "function.js", JS_EVAL_TYPE_GLOBAL);
    REQUIRE_FALSE (JS_IsException (result));
    CHECK (JS_ToBool (vm.context, result) == 1);
    JS_FreeValue (vm.context, result);
    JS_FreeValue (vm.context, global);
}

TEST_CASE ("Script runtime releases many captured callbacks at shutdown", "[script-runtime]") {
    for (int iteration = 0; iteration < 3; ++iteration) {
	Runtime vm;
	JSValue global = JS_GetGlobalObject (vm.context);
	JS_SetPropertyStr (vm.context, global, "retain", JS_NewCFunction (vm.context, retainArgument, "retain", 1));
	const char* source = "for (let i = 0; i < 20000; ++i) retain({value: i}); 42";
	JSValue result = JS_Eval (vm.context, source, strlen (source), "cleanup.js", JS_EVAL_TYPE_GLOBAL);
	REQUIRE_FALSE (JS_IsException (result));
	int32_t number = 0;
	REQUIRE (JS_ToInt32 (vm.context, &number, result) == 0);
	CHECK (number == 42);
	JS_FreeValue (vm.context, result);
	JS_FreeValue (vm.context, global);
    }
}
