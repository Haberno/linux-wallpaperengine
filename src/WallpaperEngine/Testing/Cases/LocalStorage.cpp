#include "WallpaperEngine/Scripting/LocalStorage.h"
#include "WallpaperEngine/Scripting/Builtins.generated.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <future>
#include <string>

namespace {
struct TemporaryStorage {
    std::filesystem::path path;
    TemporaryStorage () {
	char directory[] = "/tmp/lwe-localstorage-test-XXXXXX";
	const char* created = mkdtemp (directory);
	REQUIRE (created != nullptr);
	path = created;
    }
    ~TemporaryStorage () {
	std::error_code ignored;
	std::filesystem::remove_all (path, ignored);
    }
};

class StorageContext {
public:
    StorageContext (const std::filesystem::path& directory, const std::string& project, const std::string& screen) {
	m_runtime = JS_NewRuntime ();
	m_context = JS_NewContext (m_runtime);
	JSValue global = JS_GetGlobalObject (m_context);
	WallpaperEngine::Scripting::installLocalStorage (m_context, global, project, screen, directory);
	JS_FreeValue (m_context, global);
	// Non-enumerable fields reproduce the C++ vector accessors for the JSON
	// bridge; installed Wallpaper Engine classes enumerate these components.
	m_error = run (R"JS(
            function assert(value) { if (!value) throw new Error('storage assertion failed'); }
            class Vec2 {
                constructor(x, y) {
                    Object.defineProperty(this, 'x', {value: x});
                    Object.defineProperty(this, 'y', {value: y});
                }
            }
            class Vec3 {
                constructor(x, y, z) {
                    Object.defineProperty(this, 'x', {value: x});
                    Object.defineProperty(this, 'y', {value: y});
                    Object.defineProperty(this, 'z', {value: z});
                }
            }
            class Vec4 {
                constructor(x, y, z, w) {
                    Object.defineProperty(this, 'x', {value: x});
                    Object.defineProperty(this, 'y', {value: y});
                    Object.defineProperty(this, 'z', {value: z});
                    Object.defineProperty(this, 'w', {value: w});
                }
            }
        )JS");
	if (m_error.empty ()) {
	    m_error = run (WallpaperEngine::Scripting::SCENE_SCRIPT_BUILTINS);
	}
    }

    ~StorageContext () {
	JS_FreeContext (m_context);
	JS_FreeRuntime (m_runtime);
    }

    std::string run (const std::string& source) {
	if (!m_error.empty ()) {
	    return m_error;
	}
	JSValue result = JS_Eval (m_context, source.data (), source.size (), "storage-test.js", JS_EVAL_TYPE_GLOBAL);
	std::string error;
	if (JS_IsException (result)) {
	    JSValue exception = JS_GetException (m_context);
	    const char* text = JS_ToCString (m_context, exception);
	    error = text ? text : "Unknown JS exception";
	    JS_FreeCString (m_context, text);
	    JS_FreeValue (m_context, exception);
	}
	JS_FreeValue (m_context, result);
	return error;
    }

private:
    JSRuntime* m_runtime;
    JSContext* m_context;
    std::string m_error;
};
}

TEST_CASE ("SceneScript localStorage preserves JSON values and native deletion semantics", "[local-storage]") {
    TemporaryStorage directory;
    StorageContext context (directory.path, "wallpaper-a", "DP-1");
    REQUIRE (context.run (R"JS(
        assert(localStorage.get('missing') === undefined);
        assert(localStorage.delete('missing') === false);
        localStorage.set('false', false);
        localStorage.set('number', 123.5);
        localStorage.set('null', null);
        assert(localStorage.get('false') === false);
        assert(localStorage.get('number') === 123.5);
        assert(localStorage.get('null') === null);
        var original = {nested: [false, {value: 9}], position: new Vec3(2, 4, 6)};
        localStorage.set('object', original);
        original.nested[1].value = 100;
        var loaded = localStorage.get('object');
        assert(loaded.nested[1].value === 9);
        assert(loaded.position.x === 2 && loaded.position.y === 4 && loaded.position.z === 6);
        assert(!(loaded.position instanceof Vec3));
        loaded.nested[1].value = 200;
        assert(localStorage.get('object').nested[1].value === 9);
        localStorage.set('v2', new Vec2(7, 8));
        localStorage.set('v4', new Vec4(1, 2, 3, 4));
        assert(JSON.stringify(localStorage.get('v2')) === '{"x":7,"y":8}');
        assert(JSON.stringify(localStorage.get('v4')) === '{"x":1,"y":2,"z":3,"w":4}');
        localStorage.set('__proto__', 'safe');
        localStorage.set('nul\u0000key', 'nul\u0000value');
        assert(localStorage.get('__proto__') === 'safe');
        assert(localStorage.get('nul\u0000key') === 'nul\u0000value');
        assert(localStorage.delete('number') === true);
        assert(localStorage.delete('number') === false);
        localStorage.set('false', undefined);
        assert(localStorage.get('false') === undefined);
        var invalidKey = false;
        try { localStorage.set(1, 'bad'); } catch (e) { invalidKey = e instanceof TypeError; }
        assert(invalidKey);
        var cyclic = {}; cyclic.self = cyclic;
        var rejectedCycle = false;
        try { localStorage.set('object', cyclic); } catch (e) { rejectedCycle = true; }
        assert(rejectedCycle && localStorage.get('object').nested[1].value === 9);
    )JS").empty ());
}

TEST_CASE ("SceneScript localStorage persists and isolates projects and outputs", "[local-storage]") {
    TemporaryStorage directory;
    {
	StorageContext first (directory.path, "wallpaper-a", "DP-1");
	REQUIRE (first.run (R"JS(
            localStorage.set('position', [10, 20]);
            localStorage.set('theme', 'dark', localStorage.LOCATION_GLOBAL);
        )JS").empty ());
    }
    StorageContext first (directory.path, "wallpaper-a", "DP-1");
    StorageContext second (directory.path, "wallpaper-a", "HDMI-A-1");
    StorageContext other (directory.path, "wallpaper-b", "DP-1");
    REQUIRE (first.run ("assert(localStorage.get('position')[0] === 10);").empty ());
    REQUIRE (second.run (R"JS(
        assert(localStorage.get('position') === undefined);
        assert(localStorage.get('theme', 'global') === 'dark');
        localStorage.set('position', [30, 40]);
        localStorage.set('theme', 'light', 'global');
    )JS").empty ());
    REQUIRE (first.run (R"JS(
        assert(localStorage.get('theme', 'global') === 'light');
        assert(localStorage.get('position')[0] === 10);
        assert(localStorage.get('position', 'screen')[0] === 10);
        assert(localStorage.get('position', null)[0] === 10);
        assert(localStorage.get('position', 'unknown')[0] === 10);
        localStorage.clear();
        assert(localStorage.get('position') === undefined);
        assert(localStorage.get('theme', 'global') === 'light');
    )JS").empty ());
    REQUIRE (second.run (R"JS(
        assert(localStorage.get('position')[0] === 30);
        localStorage.clear('global');
        assert(localStorage.get('position')[0] === 30);
    )JS").empty ());
    REQUIRE (first.run ("assert(localStorage.get('theme', 'global') === undefined);").empty ());
    REQUIRE (other.run (R"JS(
        assert(localStorage.get('position') === undefined);
        assert(localStorage.get('theme', 'global') === undefined);
        localStorage.set('position', [50, 60]);
    )JS").empty ());
    REQUIRE (second.run ("assert(localStorage.get('position')[0] === 30);").empty ());
}

TEST_CASE ("SceneScript localStorage serializes concurrent instance updates", "[local-storage]") {
    TemporaryStorage directory;
    auto writeValues = [&] (const std::string& screen) {
	StorageContext context (directory.path, "shared-wallpaper", screen);
	return context.run (
	    "for (var i = 0; i < 32; ++i) { localStorage.set('" + screen
	    + "-' + i, i, 'global'); localStorage.set('value', i); }"
	);
    };
    auto first = std::async (std::launch::async, writeValues, "DP-1");
    auto second = std::async (std::launch::async, writeValues, "HDMI-A-1");
    REQUIRE (first.get ().empty ());
    REQUIRE (second.get ().empty ());
    StorageContext reader (directory.path, "shared-wallpaper", "DP-1");
    REQUIRE (reader.run (R"JS(
        for (var i = 0; i < 32; ++i) {
            assert(localStorage.get('DP-1-' + i, 'global') === i);
            assert(localStorage.get('HDMI-A-1-' + i, 'global') === i);
        }
        assert(localStorage.get('value') === 31);
    )JS").empty ());
}
