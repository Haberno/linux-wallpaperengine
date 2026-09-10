#include "VideoTextureAdapter.h"

#include "WallpaperEngine/Render/CTexture.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

#include <cmath>
#include <tuple>
#include <utility>

namespace WallpaperEngine::Scripting::Adapters {
namespace {
constexpr unsigned int VideoMagic = 0x76696465;
struct TextureHandle {
    unsigned int magic = VideoMagic;
    std::shared_ptr<const Render::CTexture> texture;
};

enum Command { Play, Pause, Stop, IsPlaying, GetTime, SetTime, Duration, Rate, SetRate, Loop, SetLoop };

void finalize (JSRuntime*, const JSValue value) {
    JSClassID id = 0;
    delete static_cast<TextureHandle*> (JS_GetAnyOpaque (value, &id));
}

JSValue command (JSContext* ctx, JSValueConst receiver, int argc, JSValueConst* argv, int operation) {
    JSClassID id = 0;
    const auto* texture = static_cast<TextureHandle*> (JS_GetAnyOpaque (receiver, &id));
    if (texture == nullptr || texture->magic != VideoMagic) return JS_ThrowTypeError (ctx, "Invalid video texture");
    auto* player = texture->texture->getVideoPlayer ();
    double value = 0.0;
    if (operation == SetTime || operation == SetRate) {
        if (argc == 0) return JS_ThrowTypeError (ctx, "Video playback setter expects a number");
        if (JS_ToFloat64 (ctx, &value, argv[0]) < 0) return JS_EXCEPTION;
	if (!std::isfinite (value) || value < 0.0 || (operation == SetRate && value == 0.0)) {
	    return JS_ThrowRangeError (ctx, "Invalid video playback time or rate");
	}
    }
    switch (operation) {
	case Play: player->resumePlayback (); break;
	case Pause: player->setPaused (); break;
	case Stop: player->stopPlayback (); break;
	case IsPlaying: return JS_NewBool (ctx, player->isPlaying ());
	case GetTime: return JS_NewFloat64 (ctx, player->getCurrentTime ());
	case SetTime: player->setCurrentTime (value); break;
	case Duration: return JS_NewFloat64 (ctx, player->getDuration ());
	case Rate: return JS_NewFloat64 (ctx, player->getRate ());
	case SetRate: player->setRate (value); break;
	case Loop: return JS_NewBool (ctx, player->getLoop ());
	case SetLoop: if (argc > 0) player->setLoop (JS_ToBool (ctx, argv[0]) != 0); break;
    }
    return JS_UNDEFINED;
}
}

VideoTextureAdapter::VideoTextureAdapter (ScriptEngine& engine) : ObjectAdapter (engine) {
    JSClassDef definition {};
    definition.class_name = "IVideoTexture";
    definition.finalizer = finalize;
    this->registerType (definition);
}

JSValue VideoTextureAdapter::instantiate (std::shared_ptr<const Render::TextureProvider> texture) {
    auto* ctx = this->m_engine.getContext ();
    auto video = std::dynamic_pointer_cast<const Render::CTexture> (texture);
    if (video == nullptr || video->getVideoPlayer () == nullptr) return JS_UNDEFINED;
    JSValue result = JS_NewObjectClass (ctx, this->m_classId);
    if (JS_IsException (result)) return result;
    video->getVideoPlayer ()->enableScriptControl ();
    // Keep the video asset alive independently of the layer whose handle was
    // requested. A script may retain this controller after removing that layer.
    JS_SetOpaque (result, new TextureHandle { .texture = std::move (video) });
    for (const auto& [name, operation] : {
	    std::pair { "play", Play }, { "pause", Pause }, { "stop", Stop },
	    { "isPlaying", IsPlaying }, { "getCurrentTime", GetTime }, { "setCurrentTime", SetTime } }) {
	JS_SetPropertyStr (ctx, result, name, JS_NewCFunctionMagic (ctx, command, name,
	    operation == SetTime ? 1 : 0, JS_CFUNC_generic_magic, operation));
    }
    for (const auto& [name, getter, setter] : {
	    std::tuple { "duration", Duration, -1 }, { "rate", Rate, int (SetRate) }, { "loop", Loop, int (SetLoop) } }) {
	const JSAtom atom = JS_NewAtom (ctx, name);
	JS_DefinePropertyGetSet (ctx, result, atom,
	    JS_NewCFunctionMagic (ctx, command, name, 0, JS_CFUNC_generic_magic, getter),
	    setter < 0 ? JS_UNDEFINED : JS_NewCFunctionMagic (ctx, command, name, 1, JS_CFUNC_generic_magic, setter),
	    JS_PROP_ENUMERABLE);
	JS_FreeAtom (ctx, atom);
    }
    return result;
}
}
