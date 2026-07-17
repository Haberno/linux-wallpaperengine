#include "WallpaperApplication.h"

#include "Steam/FileSystem/FileSystem.h"
#include "WallpaperEngine/Application/ApplicationState.h"
#include "WallpaperEngine/Assets/AssetLoadException.h"
#include "WallpaperEngine/Audio/Drivers/Detectors/PulseAudioPlayingDetector.h"
#include "WallpaperEngine/FileSystem/Container.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Drivers/VideoFactories.h"
#include "WallpaperEngine/Render/RenderContext.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include "WallpaperEngine/Data/Dumpers/StringPrinter.h"
#include "WallpaperEngine/Data/Parsers/ProjectParser.h"
#include "WallpaperEngine/Data/Parsers/TextureParser.h"
#include "WallpaperEngine/Data/Model/Effect.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Model.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Utils/BinaryReader.h"
#include "WallpaperEngine/Render/CTexture.h"

#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Debugging/CallStack.h"
#include "WallpaperEngine/FileSystem/Adapters/MediaCover.h"
#include "WallpaperEngine/Media/DBusMediaSource.h"

#if DEMOMODE
#include "recording.h"
#endif /* DEMOMODE */

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <deque>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <malloc.h>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <thread>

#define FULLSCREEN_CHECK_WAIT_TIME 250

float g_Time;
float g_TimeLast;
float g_Daytime;

using namespace WallpaperEngine::Assets;
using namespace WallpaperEngine::Application;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::FileSystem;

void CustomGLDebugCallback (
    GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam
) {
    if (severity != GL_DEBUG_SEVERITY_HIGH) {
	return;
    }

    sLog.error ("OpenGL error: ", message, ", type: ", type, ", id: ", id);

    std::vector<WallpaperEngine::Debugging::CallStack::CallInfo> callInfo;

    WallpaperEngine::Debugging::CallStack::GetCalls (callInfo);

    for (std::vector<WallpaperEngine::Debugging::CallStack::CallInfo>::size_type i = 0; i < callInfo.size (); ++i) {
	fprintf (
	    stderr, "[%3lu] %15lu: %s in %s\n", callInfo.size () - i, callInfo[i].offset, callInfo[i].function.c_str (),
	    callInfo[i].module.c_str ()
	);
    }
}

WallpaperApplication::WallpaperApplication (ApplicationContext& context) : m_context (context) {
    this->initializeSubsystems ();
    this->loadBackgrounds ();
    this->setupProperties ();
    this->setupBrowser ();
    this->initializePlaylists ();
}

WallpaperApplication::~WallpaperApplication () {
    // A prepared result can own shared-context GL textures. Stop the producer and
    // destroy queued results while the render context is still current and alive.
    this->stopSwitchWorker ();
    this->makeAnyViewportCurrent ();
    {
	std::lock_guard lock (this->m_switchMutex);
	this->m_switchRequests.clear ();
	this->m_switchResults.clear ();
    }

    // CEF-backed wallpapers must be destroyed while their browser and GL contexts still exist.
    if (this->m_renderContext) {
	this->m_renderContext.reset ();
    }
    this->m_backgrounds.clear ();
    this->m_browserContext.reset ();
}

void WallpaperApplication::initializeSubsystems () {
    // initialize player dbus (update every 2 seconds)
    m_mediaSource = std::make_unique<WallpaperEngine::Media::DBusMediaSource> (std::chrono::milliseconds (2000));
}

AssetLocatorUniquePtr WallpaperApplication::setupAssetLocator (const std::string& bg) const {
    auto container = std::make_unique<Container> ();

    const std::filesystem::path path = bg;

    container->registerAdapterFactory (std::make_unique<MediaCoverFactory> (*this->m_mediaSource));
    container->mount ("$mediaThumbnail", "$mediaThumbnail");
    container->mount (path, "/");

    try {
	container->mount (path / "scene.pkg", "/");
    } catch (std::runtime_error&) { }

    try {
	container->mount (path / "gifscene.pkg", "/");
    } catch (std::runtime_error&) { }

    try {
	container->mount (this->m_context.settings.general.assets, "/");
    } catch (std::runtime_error&) {
	sLog.exception ("Cannot find a valid assets folder, resolved to ", this->m_context.settings.general.assets);
    }

    // mount the current directory as root
    try {
	container->mount (std::filesystem::current_path (), "/");
    } catch (std::runtime_error&) { }

    auto& vfs = container->getVFS ();

    //
    // Had to get a little creative with the effects to achieve the same bloom effect without any custom code
    // these virtual files are loaded by an image in the scene that takes current _rt_FullFrameBuffer and
    // applies the bloom effect to render it out to the screen
    //

    // add the effect file for screen bloom

    // add some model for the image element even if it's going to waste rendering cycles
    vfs.add (
	"effects/wpenginelinux/bloomeffect.json",
	{ { "name", "camerabloom_wpengine_linux" },
	  { "group", "wpengine_linux_camera" },
	  { "dependencies", JSON::array () },
	  {
	      "passes",
	      JSON::array (
		  { { { "material", "materials/util/downsample_quarter_bloom.json" },
		      { "target", "_rt_4FrameBuffer" },
		      { "bind", JSON::array ({ { { "name", "_rt_FullFrameBuffer" }, { "index", 0 } } }) } },
		    { { "material", "materials/util/downsample_eighth_blur_v.json" },
		      { "target", "_rt_8FrameBuffer" },
		      { "bind", JSON::array ({ { { "name", "_rt_4FrameBuffer" }, { "index", 0 } } }) } },
		    { { "material", "materials/util/blur_h_bloom.json" },
		      { "target", "_rt_Bloom" },
		      { "bind", JSON::array ({ { { "name", "_rt_8FrameBuffer" }, { "index", 0 } } }) } },
		    { { "material", "materials/util/combine.json" },
		      { "target", "_rt_FullFrameBuffer" },
		      { "bind",
			JSON::array (
			    { { { "name", "_rt_imageLayerComposite_-1_a" }, { "index", 0 } },
			      { { "name", "_rt_Bloom" }, { "index", 1 } } }
			) } } }
	      ),
	  } }
    );

    vfs.add ("models/wpenginelinux.json", { { "material", "materials/wpenginelinux.json" } });

    vfs.add (
	"materials/wpenginelinux.json",
	{ { "passes",
	    JSON::array (
		{ { { "blending", "normal" },
		    { "cullmode", "nocull" },
		    { "depthtest", "disabled" },
		    { "depthwrite", "disabled" },
		    { "shader", "genericimage2" },
		    { "textures", JSON::array ({ "_rt_FullFrameBuffer" }) } } }
	    ) } }
    );

    vfs.add (
	"shaders/commands/copy.frag",
	"uniform sampler2D g_Texture0;\n"
	"in vec2 v_TexCoord;\n"
	"void main () {\n"
	"out_FragColor = texture (g_Texture0, v_TexCoord);\n"
	"}"
    );
    vfs.add (
	"shaders/commands/copy.vert",
	"in vec3 a_Position;\n"
	"in vec2 a_TexCoord;\n"
	"out vec2 v_TexCoord;\n"
	"void main () {\n"
	"gl_Position = vec4 (a_Position, 1.0);\n"
	"v_TexCoord = a_TexCoord;\n"
	"}"
    );

    return std::make_unique<AssetLocator> (std::move (container));
}

void WallpaperApplication::loadBackgrounds () {
    if (this->m_context.settings.render.mode == ApplicationContext::NORMAL_WINDOW
	|| this->m_context.settings.render.mode == ApplicationContext::EXPLICIT_WINDOW) {
	auto path = this->m_context.settings.general.defaultBackground;

	if (this->m_context.settings.general.defaultPlaylist.has_value ()
	    && !this->m_context.settings.general.defaultPlaylist->items.empty ()) {
	    path = this->m_context.settings.general.defaultPlaylist->items.front ();
	}

	this->m_backgrounds["default"] = this->loadBackground (path);
	return;
    }

    for (const auto& [screen, path] : this->m_context.settings.general.screenBackgrounds) {
	// skip span group synthetic keys here, they're handled below
	if (screen.rfind ("span:", 0) == 0) {
	    continue;
	}
	// screens with no path should use the default
	if (path.empty ()) {
	    this->m_backgrounds[screen] = this->loadBackground (this->m_context.settings.general.defaultBackground);
	} else {
	    this->m_backgrounds[screen] = this->loadBackground (path);
	}
    }

    // Load one background per span group
    for (const auto& spanGroup : this->m_context.settings.general.spanGroups) {
	if (spanGroup.screens.empty ()) {
	    continue;
	}

	std::filesystem::path bgPath = spanGroup.background;
	if (bgPath.empty ()) {
	    bgPath = this->m_context.settings.general.defaultBackground;
	}

	// use the first screen's name as the group key for the loaded project
	const std::string groupKey = "span:" + spanGroup.screens.front ();
	this->m_backgrounds[groupKey] = this->loadBackground (bgPath);
    }
}

ProjectUniquePtr WallpaperApplication::loadProject (const std::string& bg) const {
    auto container = this->setupAssetLocator (bg);
    auto json = WallpaperEngine::Data::JSON::JSON::parse (container->readString ("project.json"));

    return WallpaperEngine::Data::Parsers::ProjectParser::parse (json, std::move (container));
}

void WallpaperApplication::resetScreenshotState () {
    // when a background is loaded, reset the screenshot variables
    // this allows taking screenshots after a background changes
    // useful for playlists
    if (!this->m_context.settings.screenshot.take) {
	return;
    }

    this->m_nextFrameScreenshot = this->m_context.settings.screenshot.delay;

    if (this->m_videoDriver != nullptr) {
	this->m_nextFrameScreenshot += this->m_videoDriver->getFrameCounter ();
    }

    this->m_screenShotTaken = false;
}

ProjectUniquePtr WallpaperApplication::loadBackground (const std::string& bg) {
    auto project = this->loadProject (bg);

    this->resetScreenshotState ();

    return project;
}

std::vector<std::size_t>
WallpaperApplication::buildPlaylistOrder (const ApplicationContext::PlaylistDefinition& definition) {
    std::vector<std::size_t> order (definition.items.size ());
    std::iota (order.begin (), order.end (), 0);

    if (definition.settings.order == "random") {
	std::shuffle (order.begin (), order.end (), this->m_playlistRng);
    }

    return order;
}

void WallpaperApplication::initializePlaylists () {
    const bool hasDefaultPlaylist = this->m_context.settings.general.defaultPlaylist.has_value ();
    const bool hasScreenPlaylists = !this->m_context.settings.general.screenPlaylists.empty ();

    if (!hasDefaultPlaylist && !hasScreenPlaylists) {
	return;
    }

    const auto now = std::chrono::steady_clock::now ();

    auto registerPlaylist = [this, now] (
				const std::string& key, const ApplicationContext::PlaylistDefinition& playlist,
				std::optional<std::filesystem::path> currentPath
			    ) {
	if (playlist.items.empty ()) {
	    return;
	}

	ActivePlaylist state;

	state.definition = playlist;
	state.order = this->buildPlaylistOrder (playlist);

	if (state.order.empty ()) {
	    return;
	}

	if (currentPath.has_value ()) {
	    state.orderIndex = 0;

	    for (std::size_t i = 0; i < state.order.size (); i++) {
		if (playlist.items[state.order[i]] == currentPath.value ()) {
		    state.orderIndex = i;
		    break;
		}
	    }
	}

	const uint32_t delayMinutes = std::max<uint32_t> (1, state.definition.settings.delayMinutes);
	state.nextSwitch = now + std::chrono::minutes (delayMinutes);
	state.lastUpdate = now;

	this->m_activePlaylists.insert_or_assign (key, std::move (state));
    };

    if (hasDefaultPlaylist
	&& (this->m_context.settings.render.mode == ApplicationContext::NORMAL_WINDOW
	    || this->m_context.settings.render.mode == ApplicationContext::EXPLICIT_WINDOW)) {
	const auto& playlist = this->m_context.settings.general.defaultPlaylist.value ();
	const auto currentPath = playlist.items.empty ()
	    ? std::optional<std::filesystem::path> { this->m_context.settings.general.defaultBackground }
	    : std::optional<std::filesystem::path> { playlist.items.front () };
	registerPlaylist ("default", playlist, currentPath);
    }

    for (const auto& [screen, playlist] : this->m_context.settings.general.screenPlaylists) {
	const auto current = this->m_context.settings.general.screenBackgrounds.find (screen);
	const auto currentPath = current != this->m_context.settings.general.screenBackgrounds.end ()
	    ? std::optional<std::filesystem::path> { current->second }
	    : std::nullopt;
	registerPlaylist (screen, playlist, currentPath);
    }
}

void WallpaperApplication::ensureBrowserForProject (const Project& project) {
    if (!project.wallpaper->is<Web> ()) {
	return;
    }

    if (!this->m_browserContext) {
	this->m_browserContext = std::make_unique<WebBrowser::WebBrowserContext> (*this);
    }
}

bool WallpaperApplication::makeAnyViewportCurrent () const {
    if (!this->m_renderContext) {
	return false;
    }

    const auto& viewports = this->m_renderContext->getOutput ().getViewports ();

    if (viewports.empty ()) {
	return false;
    }

    viewports.begin ()->second->makeCurrent ();
    return true;
}

bool WallpaperApplication::preflightWallpaper (const std::string& path) {
    try {
	// avoid mutating state, just ensure project.json parses
	auto container = this->setupAssetLocator (path);
	const auto json = WallpaperEngine::Data::JSON::JSON::parse (container->readString ("project.json"));
	if (!json.contains ("type") || !json.contains ("file")) {
	    sLog.error ("Preflight failed for ", path, ": missing required fields");
	    return false;
	}
	return true;
    } catch (const std::exception& e) {
	sLog.error ("Preflight failed for ", path, ": ", e.what ());
	return false;
    }
}

bool WallpaperApplication::selectNextCandidate (ActivePlaylist& playlist, std::size_t& outOrderIndex) {
    if (playlist.order.empty ()) {
	return false;
    }

    std::size_t attempts = 0;
    std::size_t candidateOrderIndex = outOrderIndex;

    while (attempts < playlist.order.size ()) {
	const auto candidateIndex = playlist.order[candidateOrderIndex];

	if (!playlist.failedIndices.contains (candidateIndex)) {
	    outOrderIndex = candidateOrderIndex;
	    return true;
	}

	attempts++;
	candidateOrderIndex = (candidateOrderIndex + 1) % playlist.order.size ();
    }

    return false;
}

namespace {
/**
 * Maps a transition name from the control socket to its mode. "random" maps to
 * TransitionMode_None as a sentinel the caller resolves to a random pick.
 */
std::optional<WallpaperEngine::Render::TransitionMode> parseTransitionName (const std::string& name) {
    using namespace WallpaperEngine::Render;
    static const std::map<std::string, TransitionMode> NAMES = {
	{ "random", TransitionMode_None },	{ "fade", TransitionMode_Fade },
	{ "wipeleft", TransitionMode_WipeLeft }, { "wiperight", TransitionMode_WipeRight },
	{ "wipeup", TransitionMode_WipeUp },	{ "wipedown", TransitionMode_WipeDown },
	{ "disc", TransitionMode_Disc },	{ "stripes", TransitionMode_Stripes },
	{ "pixelate", TransitionMode_Pixelate }, { "honeycomb", TransitionMode_Honeycomb },
	{ "wipediag", TransitionMode_WipeDiag }, { "clock", TransitionMode_Clock },
	{ "iris", TransitionMode_Iris },	{ "checkerboard", TransitionMode_Checkerboard },
	{ "blinds", TransitionMode_Blinds },	{ "split", TransitionMode_Split },
	{ "voronoi", TransitionMode_Voronoi },	{ "noise", TransitionMode_Noise },
	{ "dots", TransitionMode_Dots },	{ "inksplash", TransitionMode_InkSplash },
    };

    const auto it = NAMES.find (name);
    return it != NAMES.end () ? std::optional (it->second) : std::nullopt;
}
} // namespace

void WallpaperApplication::setupControlSocket () {
    const char* runtimeDir = getenv ("XDG_RUNTIME_DIR");
    this->m_controlSocketPath = std::string (runtimeDir != nullptr ? runtimeDir : "/tmp") + "/linux-wallpaperengine.sock";

    this->m_controlSocket = socket (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (this->m_controlSocket < 0) {
	sLog.error ("Cannot create control socket: ", strerror (errno));
	return;
    }

    // a previous instance (or a crashed one) may have left the socket file behind
    unlink (this->m_controlSocketPath.c_str ());

    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    strncpy (address.sun_path, this->m_controlSocketPath.c_str (), sizeof (address.sun_path) - 1);

    if (bind (this->m_controlSocket, reinterpret_cast<sockaddr*> (&address), sizeof (address)) < 0
	|| listen (this->m_controlSocket, 4) < 0) {
	sLog.error ("Cannot bind control socket ", this->m_controlSocketPath, ": ", strerror (errno));
	close (this->m_controlSocket);
	this->m_controlSocket = -1;
	return;
    }

    // remember which filesystem entry we created; a newer instance may replace
    // it (unlink + rebind), in which case the exit path must leave it alone
    struct stat info {};
    if (stat (this->m_controlSocketPath.c_str (), &info) == 0) {
	this->m_controlSocketInode = info.st_ino;
    }

    sLog.out ("Control socket listening on ", this->m_controlSocketPath);
}

void WallpaperApplication::processControlSocket () {
    if (this->m_controlSocket < 0) {
	return;
    }

    // handle every pending connection; each carries a single command line
    while (true) {
	const int client = accept4 (this->m_controlSocket, nullptr, nullptr, SOCK_CLOEXEC);

	if (client < 0) {
	    return;
	}

	// the client writes right after connecting, but give it a small grace period
	pollfd pfd { .fd = client, .events = POLLIN, .revents = 0 };
	std::string command;

	while (poll (&pfd, 1, 100) > 0 && (pfd.revents & POLLIN) != 0) {
	    char buffer [4096];
	    const ssize_t bytes = read (client, buffer, sizeof (buffer));

	    if (bytes <= 0) {
		break;
	    }

	    command.append (buffer, bytes);

	    if (command.find ('\n') != std::string::npos) {
		break;
	    }
	}

	if (const auto newline = command.find ('\n'); newline != std::string::npos) {
	    command.resize (newline);
	}

	std::string reply
	    = "err invalid command, expected: switch <screen> [transition] <path> | prop <screen> <key> <value>\n";

	// prop <screen> <key> <value> - live user-property change: updates the property value and
	// fires the scripts' applyUserProperties handler. Properties that gate scene structure
	// (visibility conditions, effect toggles) need a rebuild, which this does NOT do yet.
	if (command.rfind ("prop ", 0) == 0) {
	    const auto body = command.substr (5);
	    const auto screenEnd = body.find (' ');
	    const auto keyEnd = body.find (' ', screenEnd + 1);

	    if (screenEnd == std::string::npos || keyEnd == std::string::npos) {
		reply = "err expected: prop <screen> <key> <value>\n";
	    } else {
		const auto screen = body.substr (0, screenEnd);
		const auto key = body.substr (screenEnd + 1, keyEnd - screenEnd - 1);
		const auto value = body.substr (keyEnd + 1);

		const auto& wallpapers
		    = this->m_renderContext != nullptr ? this->m_renderContext->getWallpapers ()
						       : std::map<std::string, std::shared_ptr<Render::CWallpaper>> {};
		const auto wpIt = wallpapers.find (screen);

		if (wpIt == wallpapers.end ()) {
		    reply = "err unknown screen " + screen + "\n";
		} else if (auto* scene = dynamic_cast<Render::Wallpapers::CScene*> (wpIt->second.get ());
			   scene == nullptr) {
		    reply = "err not a scene wallpaper on " + screen + "\n";
		} else {
		    // Scene::project is a reference member, so it stays mutable through the const getter
		    auto& project = scene->getScene ().project;
		    const auto propertyIt = project.properties.find (key);

		    if (propertyIt == project.properties.end ()) {
			reply = "err unknown property " + key + "\n";
		    } else {
			propertyIt->second->update (value, DynamicValue::UpdateSource::User);
			scene->getScriptEngine ().dispatchUserProperty (key, *propertyIt->second);
			reply = "ok\n";
		    }
		}
	    }
	}

	// switch <screen> [transition] <path> - path may contain spaces, so the optional
	// transition sits before it where it can be recognized unambiguously
	if (command.rfind ("switch ", 0) == 0) {
	    auto body = command.substr (7);

	    if (const auto separator = body.find (' '); separator != std::string::npos) {
		const auto screen = body.substr (0, separator);
		auto path = body.substr (separator + 1);
		auto transition = Render::TransitionMode_Fade;

		if (const auto transitionEnd = path.find (' '); transitionEnd != std::string::npos) {
		    if (const auto parsed = parseTransitionName (path.substr (0, transitionEnd));
			parsed.has_value ()) {
			transition = parsed.value ();
			path = path.substr (transitionEnd + 1);
		    }
		}

		if (transition == Render::TransitionMode_None) {
		    // "random" resolves here so the whole animation set stays in one place
		    transition = static_cast<Render::TransitionMode> (std::uniform_int_distribution<int> (
			Render::TransitionMode_Fade, Render::TransitionMode_Last
		    ) (this->m_playlistRng));
		}

		if (this->m_renderContext == nullptr
		    || !this->m_renderContext->getWallpapers ().contains (screen)) {
		    reply = "err unknown screen " + screen + "\n";
		} else {
		    // the load happens on the loader thread; failures only show up in the
		    // log once the prepared switch is applied
		    this->requestBackgroundSwitch (screen, path, transition);
		    reply = "ok\n";
		}
	    }
	}

	// memstats - glibc heap counters over the socket so leak hunts can run without ptrace
	if (command == "memstats") {
	    const auto info = mallinfo2 ();
	    reply = "ok inuse=" + std::to_string (info.uordblks) + " free=" + std::to_string (info.fordblks)
		+ " arena=" + std::to_string (info.arena) + " mmap=" + std::to_string (info.hblkhd) + "\n";
	}

	if (write (client, reply.c_str (), reply.size ()) < 0) {
	    sLog.error ("Cannot write control socket reply: ", strerror (errno));
	}

	close (client);
    }
}

namespace {
/** Engine-generated or media-bound texture names the switch worker must not pre-parse */
bool isSpecialTexture (const std::string& name) {
    return name.empty () || name.starts_with ("_rt_") || name.starts_with ("$");
}

void collectTextureNames (const TextureMap& textures, std::set<std::string>& out) {
    for (const auto& [slot, name] : textures) {
	if (!isSpecialTexture (name)) {
	    out.insert (name);
	}
    }
}

void collectMaterialTextures (const Material& material, std::set<std::string>& out) {
    for (const auto& pass : material.passes) {
	collectTextureNames (pass->textures, out);
	collectTextureNames (pass->usertextures, out);
    }
}

void collectImageEffectTextures (const std::vector<ImageEffectUniquePtr>& effects, std::set<std::string>& out) {
    for (const auto& imageEffect : effects) {
	for (const auto& passOverride : imageEffect->passOverrides) {
	    collectTextureNames (passOverride->textures, out);
	    collectTextureNames (passOverride->usertextures, out);
	}

	if (imageEffect->effect == nullptr) {
	    continue;
	}

	for (const auto& pass : imageEffect->effect->passes) {
	    if (pass->material.has_value ()) {
		collectMaterialTextures (**pass->material, out);
	    }

	    collectTextureNames (pass->binds, out);
	}
    }
}

/**
 * Walks a scene's data model gathering every texture file its materials and effects
 * reference so the switch worker can parse them off the render thread. Non-scene
 * wallpapers (video, web) load no textures through the texture cache.
 */
std::set<std::string> collectProjectTextures (const Project& project) {
    std::set<std::string> result {};

    if (!project.wallpaper->is<Scene> ()) {
	return result;
    }

    for (const auto& object : project.wallpaper->as<Scene> ()->objects) {
	if (object->is<Image> ()) {
	    const auto* image = object->as<Image> ();

	    if (image->model != nullptr && image->model->material != nullptr) {
		collectMaterialTextures (*image->model->material, result);
	    }

	    collectImageEffectTextures (image->effects, result);
	} else if (object->is<Particle> ()) {
	    const auto* particle = object->as<Particle> ();

	    if (particle->material != nullptr && particle->material->material != nullptr) {
		collectMaterialTextures (*particle->material->material, result);
	    }
	} else if (object->is<Text> ()) {
	    collectImageEffectTextures (object->as<Text> ()->effects, result);
	}
    }

    return result;
}

/** Finishes queued uploads and releases the worker's shared GL context on every exit path. */
class BuildContextGuard {
public:
    explicit BuildContextGuard (WallpaperEngine::Render::Drivers::VideoDriver& driver) : m_driver (driver) { }
    ~BuildContextGuard () {
	// The render context must never observe a partially uploaded shared texture.
	// Waiting here blocks only the loader thread, not frame dispatch.
	glFinish ();
	m_driver.releaseBuildContext ();
    }

    BuildContextGuard (const BuildContextGuard&) = delete;
    BuildContextGuard& operator= (const BuildContextGuard&) = delete;

private:
    WallpaperEngine::Render::Drivers::VideoDriver& m_driver;
};
} // namespace

void WallpaperApplication::requestBackgroundSwitch (
    const std::string& screen, const std::string& path, const Render::TransitionMode transition
) {
    {
	std::lock_guard lock (this->m_switchMutex);

	if (!this->m_switchWorker.joinable ()) {
	    this->m_switchWorkerStop = false;
	    this->m_switchWorker = std::thread (&WallpaperApplication::switchWorkerMain, this);
	}

	PreparedSwitch job {};
	job.id = ++this->m_switchIdCounter;
	job.screen = screen;
	job.path = path;
	job.transition = transition;
	this->m_latestSwitchIds[screen] = job.id;
	this->m_switchRequests.emplace_back (std::move (job));
    }

    this->m_switchCv.notify_one ();
}

void WallpaperApplication::switchWorkerMain () {
    while (true) {
	PreparedSwitch job {};

	{
	    std::unique_lock lock (this->m_switchMutex);
	    this->m_switchCv.wait (lock, [this] {
		return this->m_switchWorkerStop || !this->m_switchRequests.empty ();
	    });

	    if (this->m_switchWorkerStop) {
		return;
	    }

	    job = std::move (this->m_switchRequests.front ());
	    this->m_switchRequests.pop_front ();

	    // a newer request for the same screen already superseded this one
	    if (this->m_latestSwitchIds[job.screen] != job.id) {
		continue;
	    }
	}

	try {
	    job.project = this->loadProject (job.path);

	    // Read and decompress textures away from the render loop first.
	    for (const auto& name : collectProjectTextures (*job.project)) {
		try {
		    const auto contents = job.project->assetLocator->texture (name);
		    auto stream = Data::Utils::BinaryReader (contents);
		    auto metadataLoader = [&job] (const std::string& metaFilename) -> std::string {
			const auto fullPath = std::filesystem::path ("materials") / metaFilename;
			return job.project->assetLocator->readString (fullPath);
		    };

		    auto texture = Data::Parsers::TextureParser::parse (stream, name, metadataLoader);
		    // decode image-format textures here as well so the render thread
		    // skips the stbi work entirely and only does the GL upload
		    Data::Parsers::TextureParser::decodeMipmaps (*texture);

		    job.textures.emplace_back (name, std::move (texture));
		} catch (const std::exception&) {
		    // ignored, the render thread falls back to loading it synchronously
		}
	    }

	    {
		std::lock_guard lock (this->m_switchMutex);
		// Avoid spending GPU time on a selection the user has already replaced.
		if (this->m_latestSwitchIds[job.screen] != job.id) {
		    continue;
		}
	    }

	    // Static GL textures are shareable between EGL contexts, so upload them here.
	    // Video textures initialize mpv and stay in the main-thread fallback list.
	    if (!job.textures.empty () && this->m_videoDriver != nullptr && this->m_renderContext != nullptr
		&& this->m_videoDriver->makeBuildContextCurrent ()) {
		BuildContextGuard buildContext (*this->m_videoDriver);
		std::vector<std::pair<std::string, Data::Assets::TextureUniquePtr>> fallbackTextures {};
		fallbackTextures.reserve (job.textures.size ());

		for (auto& [name, texture] : job.textures) {
		    const bool mediaBacked = texture->isVideoMp4 || (texture->flags & TextureFlags_Video);

		    if (mediaBacked) {
			fallbackTextures.emplace_back (std::move (name), std::move (texture));
			continue;
		    }

		    try {
			job.readyTextures.emplace_back (
			    std::move (name),
			    std::make_shared<Render::CTexture> (*this->m_renderContext, std::move (texture))
			);
		    } catch (const std::exception& e) {
			// resolveTexture will load this asset normally during the scene build.
			sLog.error ("Async texture upload failed for ", name, ": ", e.what ());
		    }
		}

		job.textures = std::move (fallbackTextures);
	    }
	} catch (const std::exception& e) {
	    job.error = e.what ();
	}

	{
	    std::lock_guard lock (this->m_switchMutex);
	    this->m_switchResults.emplace_back (std::move (job));
	}
    }
}

void WallpaperApplication::processPreparedSwitches () {
#if defined(__GLIBC__)
    // a switch's peak allocations (decode staging, old+new project alive through the
    // crossfade) leave hundreds of MB of freed-but-retained chunks in the glibc arena;
    // once the fade is done and teardown has run, hand those pages back to the OS
    if (this->m_pendingMallocTrim != std::chrono::steady_clock::time_point {}
	&& std::chrono::steady_clock::now () >= this->m_pendingMallocTrim) {
	this->m_pendingMallocTrim = {};
	malloc_trim (0);
    }
#endif

    while (true) {
	PreparedSwitch job {};

	{
	    std::lock_guard lock (this->m_switchMutex);

	    if (this->m_switchResults.empty ()) {
		return;
	    }

	    job = std::move (this->m_switchResults.front ());
	    this->m_switchResults.pop_front ();

	    // a newer request for the same screen is already on the way, drop this one
	    if (this->m_latestSwitchIds[job.screen] != job.id) {
		// readyTextures owns GL objects. Ensure its destructor runs with a context.
		this->makeAnyViewportCurrent ();
		continue;
	    }
	}

	if (!job.error.empty ()) {
	    if (!job.readyTextures.empty ()) {
		this->makeAnyViewportCurrent ();
	    }
	    sLog.error ("Failed to load wallpaper ", job.path, " for ", job.screen, ": ", job.error);
	    this->markPlaylistItemFailed (job.screen, job.path);
	    continue;
	}

	if (!this->applyPreparedSwitch (job)) {
	    this->markPlaylistItemFailed (job.screen, job.path);
	}

	// schedule an allocator trim for after the crossfade ends and the previous
	// project has been torn down (transitions run well under this)
	this->m_pendingMallocTrim = std::chrono::steady_clock::now () + std::chrono::seconds (5);
    }
}

bool WallpaperApplication::applyPreparedSwitch (PreparedSwitch& job) {
    try {
	if (!this->makeAnyViewportCurrent ()) {
	    sLog.error ("Cannot switch wallpaper on ", job.screen, ": no active viewport");
	    throw std::runtime_error ("No viewport available");
	}

	this->setupPropertiesForProject (*job.project);
	this->ensureBrowserForProject (*job.project);

	// the outgoing CWallpaper references this Project's data; the render context
	// keeps it alive until the crossfade is over
	std::shared_ptr<void> previousProject;
	if (const auto it = this->m_backgrounds.find (job.screen); it != this->m_backgrounds.end ()) {
	    previousProject = std::shared_ptr<Project> (std::move (it->second));
	}

	this->m_backgrounds[job.screen] = std::move (job.project);

	const auto scalingIt = this->m_context.settings.general.screenScalings.find (job.screen);
	const auto clampIt = this->m_context.settings.general.screenClamps.find (job.screen);
	const auto scaling = scalingIt != this->m_context.settings.general.screenScalings.end ()
	    ? scalingIt->second
	    : this->m_context.settings.render.window.scalingMode;
	const auto clamp = clampIt != this->m_context.settings.general.screenClamps.end ()
	    ? clampIt->second
	    : this->m_context.settings.render.window.clamp;

	if (this->m_renderContext) {
	    const auto& assetLocator = *this->m_backgrounds[job.screen]->assetLocator;
	    // Shared-context uploads are already complete; cache insertion is only a pointer move.
	    for (auto& [name, texture] : job.readyTextures) {
		this->m_renderContext->storeTexture (name, assetLocator, std::move (texture));
	    }

	    // Media textures and non-Wayland fallbacks still need main-context construction.
	    for (auto& [name, texture] : job.textures) {
		this->m_renderContext->storeTexture (
		    name, assetLocator,
		    std::make_shared<Render::CTexture> (*this->m_renderContext, std::move (texture))
		);
	    }

	    auto renderWallpaper = WallpaperEngine::Render::CWallpaper::fromWallpaper (
		*this->m_backgrounds[job.screen]->wallpaper, *this->m_renderContext, *this->m_audioContext,
		this->m_browserContext.get (), scaling, clamp
	    );

	    this->m_renderContext->setWallpaper (
		job.screen, std::move (renderWallpaper), std::move (previousProject), job.transition
	    );
	}

	this->m_context.settings.general.screenBackgrounds[job.screen] = job.path;
	return true;
    } catch (const std::exception& e) {
	sLog.error ("Failed to switch wallpaper on ", job.screen, ": ", e.what ());
	return false;
    }
}

void WallpaperApplication::markPlaylistItemFailed (const std::string& screen, const std::string& path) {
    const auto it = this->m_activePlaylists.find (screen);

    if (it == this->m_activePlaylists.end ()) {
	return;
    }

    auto& playlist = it->second;
    const auto& items = playlist.definition.items;

    for (std::size_t index = 0; index < items.size (); index++) {
	if (items[index] == path) {
	    playlist.failedIndices.insert (index);
	    sLog.error ("Failed to load wallpaper for ", screen, ", will retry on next cycle");
	    break;
	}
    }
}

void WallpaperApplication::stopSwitchWorker () {
    {
	std::lock_guard lock (this->m_switchMutex);

	if (!this->m_switchWorker.joinable ()) {
	    return;
	}

	this->m_switchWorkerStop = true;
    }

    this->m_switchCv.notify_all ();
    this->m_switchWorker.join ();
}

void WallpaperApplication::advancePlaylist (
    const std::string& screen, ActivePlaylist& playlist, const std::chrono::steady_clock::time_point& now
) {
    if (playlist.order.empty ()) {
	return;
    }

    playlist.orderIndex = (playlist.orderIndex + 1) % playlist.order.size ();

    if (playlist.orderIndex == 0 && playlist.definition.settings.order == "random") {
	std::shuffle (playlist.order.begin (), playlist.order.end (), this->m_playlistRng);
    }

    std::size_t candidateOrderIndex = playlist.orderIndex;

    if (!this->selectNextCandidate (playlist, candidateOrderIndex)) {
	sLog.error ("All playlist items failed for ", screen, ", keeping current wallpaper");
	const uint32_t delayMinutes = std::max<uint32_t> (1, playlist.definition.settings.delayMinutes);
	playlist.nextSwitch = now + std::chrono::minutes (delayMinutes);
	return;
    }

    const auto candidateIndex = playlist.order[candidateOrderIndex];
    const auto& candidatePath = playlist.definition.items[candidateIndex];

    if (!this->preflightWallpaper (candidatePath.string ())) {
	playlist.failedIndices.insert (candidateIndex);

	if (!this->selectNextCandidate (playlist, candidateOrderIndex)) {
	    sLog.error ("All playlist items failed for ", screen, ", keeping current wallpaper");
	    const uint32_t delayMinutes = std::max<uint32_t> (1, playlist.definition.settings.delayMinutes);
	    playlist.nextSwitch = now + std::chrono::minutes (delayMinutes);
	    return;
	}
    }

    playlist.orderIndex = candidateOrderIndex;
    const auto& nextPath = playlist.definition.items[playlist.order[playlist.orderIndex]];

    // the load happens on the loader thread; failures are reported back through
    // markPlaylistItemFailed so the next advancement skips the item
    this->requestBackgroundSwitch (screen, nextPath.string ());

    const uint32_t delayMinutes = std::max<uint32_t> (1, playlist.definition.settings.delayMinutes);
    playlist.nextSwitch = now + std::chrono::minutes (delayMinutes);
}

void WallpaperApplication::updatePlaylists () {
    if (this->m_activePlaylists.empty ()) {
	return;
    }

    const auto now = std::chrono::steady_clock::now ();

    for (auto& [screen, playlist] : this->m_activePlaylists) {
	playlist.lastUpdate = now;

	if (playlist.definition.settings.mode != "timer") {
	    continue;
	}

	if (playlist.definition.items.size () <= 1) {
	    continue;
	}

	if (now < playlist.nextSwitch) {
	    continue;
	}

	this->advancePlaylist (screen, playlist, now);
    }
}

void WallpaperApplication::setupPropertiesForProject (const Project& project) {
    // show properties if required
    for (const auto& [key, cur] : project.properties) {
	// update the value of the property
	auto override = this->m_context.settings.general.properties.find (key);

	if (override != this->m_context.settings.general.properties.end ()) {
	    sLog.out ("Applying override value for ", key);

	    cur->update (override->second, DynamicValue::UpdateSource::User);
	}

	if (this->m_context.settings.general.onlyListProperties) {
	    sLog.out (cur->dump ());
	}
    }
}

void WallpaperApplication::setupProperties () {
    for (const auto& [background, info] : this->m_backgrounds) {
	this->setupPropertiesForProject (*info);
    }
}

void WallpaperApplication::setupBrowser () {
    // Runtime switching can introduce a web wallpaper after startup, and CEF
    // must initialize before the GL/EGL context exists.
    if (this->m_browserContext) {
	return;
    }

    this->m_browserContext = std::make_unique<WebBrowser::WebBrowserContext> (*this);
}

void WallpaperApplication::takeScreenshot (const std::filesystem::path& filename) const {
    const int width = this->m_renderContext->getOutput ().getFullWidth ();
    const int height = this->m_renderContext->getOutput ().getFullHeight ();
    const bool vflip = this->m_renderContext->getOutput ().renderVFlip ();
    const auto& wallpapers = this->m_renderContext->getWallpapers ();

    struct ViewportCapture {
	uint8_t* buffer;
	int readWidth;
	int readHeight;
	int vpWidth;
	int vpHeight;
	int xoffset;
	float ustart, uend, vstart, vend;
    };

    std::vector<ViewportCapture> captures;
    int currentXOffset = 0;

    for (const auto& [screen, viewport] : this->m_renderContext->getOutput ().getViewports ()) {
	// activate opengl context so we can read from the framebuffer
	viewport->makeCurrent ();

	// find the wallpaper for this screen to read from its FBO
	const auto wallpaperIt = wallpapers.find (screen);
	if (wallpaperIt == wallpapers.end ()) {
	    sLog.error ("Cannot find wallpaper for screen ", screen);
	    continue;
	}

	const auto& wallpaper = wallpaperIt->second;
	const int vpWidth = viewport->viewport.z - viewport->viewport.x;
	const int vpHeight = viewport->viewport.w - viewport->viewport.y;

	// bind the wallpaper's FBO to read from it directly
	// this is more reliable than the default framebuffer on some drivers (NVIDIA/Wayland)
	glBindFramebuffer (GL_FRAMEBUFFER, wallpaper->getWallpaperFramebuffer ());

	// ensure rendering is complete before reading
	glFinish ();

	// make room for storing the pixel of this viewport
	const int readWidth = wallpaper->getWidth ();
	const int readHeight = wallpaper->getHeight ();
	const auto bufferSize = readWidth * readHeight * 3;
	auto* buffer = new uint8_t[bufferSize];

	// drain any errors accumulated during rendering: glGetError reports anything raised
	// since the last check, so a stale error from a draw call earlier in the frame would
	// otherwise be misattributed to the readback below and silently discard the capture
	for (int i = 0; i < 8; i++) {
	    const GLenum pending = glGetError ();
	    if (pending == GL_NO_ERROR) {
		break;
	    }
	    sLog.error ("Pending OpenGL error raised during rendering (not readback): ", pending);
	}

	// read the FBO data into the pixel buffer
	glPixelStorei (GL_PACK_ALIGNMENT, 1);
	if (GLEW_VERSION_4_5) {
	    glReadnPixels (0, 0, readWidth, readHeight, GL_RGB, GL_UNSIGNED_BYTE, bufferSize, buffer);
	} else {
	    glReadPixels (0, 0, readWidth, readHeight, GL_RGB, GL_UNSIGNED_BYTE, buffer);
	}

	// restore default framebuffer
	glBindFramebuffer (GL_FRAMEBUFFER, 0);

	if (const GLenum error = glGetError (); error != GL_NO_ERROR) {
	    sLog.error ("Cannot obtain pixel data for screen ", screen, ". OpenGL error: ", error);
	    delete[] buffer;
	    continue;
	}

	// Get the UV coordinates which define the visible portion based on scaling mode
	const auto [ustart, uend, vstart, vend] = wallpaper->getState ().getTextureUVs ();

	captures.push_back (
	    { buffer, readWidth, readHeight, vpWidth, vpHeight, currentXOffset, ustart, uend, vstart, vend }
	);

	if (viewport->single) {
	    currentXOffset += vpWidth;
	}
    }

    const auto extension = filename.extension ();
    const std::string extStr = extension.string ();

    // Offload pixel processing and saving to a background thread to avoid hitches
    std::thread ([captures, width, height, vflip, extStr, filename] () {
	auto* bitmap = new uint8_t[width * height * 3] { 0 };

	for (const auto& capture : captures) {
	    // copy pixels to bitmap, sampling from the UV-defined region
	    for (int y = 0; y < capture.vpHeight; y++) {
		for (int x = 0; x < capture.vpWidth; x++) {
		    // interpolate within the UV range to get source coordinates
		    const float u
			= capture.ustart + (static_cast<float> (x) / capture.vpWidth) * (capture.uend - capture.ustart);
		    const float v = capture.vstart
			+ (static_cast<float> (y) / capture.vpHeight) * (capture.vend - capture.vstart);

		    // convert UV to pixel coordinates in the source buffer
		    const int srcX = std::clamp (static_cast<int> (u * capture.readWidth), 0, capture.readWidth - 1);
		    const int srcY = std::clamp (static_cast<int> (v * capture.readHeight), 0, capture.readHeight - 1);
		    const int srcIdx = (srcY * capture.readWidth + srcX) * 3;

		    const int xfinal = x + capture.xoffset;
		    // FBO content is not flipped like default framebuffer, so invert vflip logic
		    const int yfinal = vflip ? y : (capture.vpHeight - y - 1);

		    if (yfinal >= 0 && yfinal < height && xfinal >= 0 && xfinal < width) {
			bitmap[yfinal * width * 3 + xfinal * 3] = capture.buffer[srcIdx];
			bitmap[yfinal * width * 3 + xfinal * 3 + 1] = capture.buffer[srcIdx + 1];
			bitmap[yfinal * width * 3 + xfinal * 3 + 2] = capture.buffer[srcIdx + 2];
		    }
		}
	    }
	    delete[] capture.buffer;
	}

	if (extStr == ".bmp") {
	    stbi_write_bmp (filename.c_str (), width, height, 3, bitmap);
	} else if (extStr == ".png") {
	    stbi_write_png (filename.c_str (), width, height, 3, bitmap, width * 3);
	} else if (extStr == ".jpg" || extStr == ".jpeg") {
	    stbi_write_jpg (filename.c_str (), width, height, 3, bitmap, 100);
	}

	delete[] bitmap;
    }).detach ();
}

void WallpaperApplication::setupOutput () {
    const char* XDG_SESSION_TYPE = getenv ("XDG_SESSION_TYPE");

    if (!XDG_SESSION_TYPE) {
	sLog.exception (
	    "Cannot read environment variable XDG_SESSION_TYPE, window server detection failed. Please ensure proper "
	    "values are set"
	);
    }

    sLog.debug ("Checking for window servers: ");

    for (const auto& windowServer : sVideoFactories.getRegisteredDrivers ()) {
	sLog.debug ("\t", windowServer);
    }

    this->m_videoDriver = sVideoFactories.createVideoDriver (
	this->m_context.settings.render.mode, XDG_SESSION_TYPE, this->m_context, *this
    );
    this->m_fullScreenDetector
	= sVideoFactories.createFullscreenDetector (XDG_SESSION_TYPE, this->m_context, *this->m_videoDriver);
}

void WallpaperApplication::setupAudio () {
    // ensure audioprocessing is required by any background, and we have it enabled
    const bool audioProcessingRequired = std::ranges::any_of (
	this->m_backgrounds, [] (const std::pair<const std::string, ProjectUniquePtr>& pair) -> bool {
	    return pair.second->supportsAudioProcessing;
	}
    );

    if (audioProcessingRequired && this->m_context.settings.audio.audioprocessing) {
	this->m_audioRecorder
	    = std::make_unique<WallpaperEngine::Audio::Drivers::Recorders::PulseAudioPlaybackRecorder> ();
    } else {
	this->m_audioRecorder = std::make_unique<WallpaperEngine::Audio::Drivers::Recorders::PlaybackRecorder> ();
    }

    if (this->m_context.settings.audio.automute) {
	m_audioDetector = std::make_unique<WallpaperEngine::Audio::Drivers::Detectors::PulseAudioPlayingDetector> (
	    this->m_context, *this->m_fullScreenDetector
	);
    } else {
	m_audioDetector = std::make_unique<WallpaperEngine::Audio::Drivers::Detectors::AudioPlayingDetector> (
	    this->m_context, *this->m_fullScreenDetector
	);
    }

    // initialize sdl audio driver
    m_audioDriver = std::make_unique<WallpaperEngine::Audio::Drivers::SDLAudioDriver> (
	this->m_context, *this->m_audioDetector, *this->m_audioRecorder
    );
    // initialize audio context
    m_audioContext = std::make_unique<WallpaperEngine::Audio::AudioContext> (*m_audioDriver);
}

void WallpaperApplication::prepareOutputs () {
    // initialize render context
    m_renderContext
	= std::make_unique<WallpaperEngine::Render::RenderContext> (*m_videoDriver, *this, *this->m_mediaSource);
    // create a new background for each screen

    // set all the specific wallpapers required (skip span group synthetic keys)
    for (const auto& [background, info] : this->m_backgrounds) {
	if (background.rfind ("span:", 0) == 0) {
	    continue;
	}
	const auto scalingIt = this->m_context.settings.general.screenScalings.find (background);
	const auto clampIt = this->m_context.settings.general.screenClamps.find (background);
	const auto scaling = scalingIt != this->m_context.settings.general.screenScalings.end ()
	    ? scalingIt->second
	    : this->m_context.settings.render.window.scalingMode;
	const auto clamp = clampIt != this->m_context.settings.general.screenClamps.end ()
	    ? clampIt->second
	    : this->m_context.settings.render.window.clamp;

	m_renderContext->setWallpaper (
	    background,
	    WallpaperEngine::Render::CWallpaper::fromWallpaper (
		*info->wallpaper, *m_renderContext, *m_audioContext, m_browserContext.get (), scaling, clamp
	    )
	);
    }

    // Set up span groups: one shared wallpaper per group, registered for each viewport
    for (const auto& spanGroup : this->m_context.settings.general.spanGroups) {
	if (spanGroup.screens.empty ()) {
	    continue;
	}

	const std::string groupKey = "span:" + spanGroup.screens.front ();
	const auto bgIt = this->m_backgrounds.find (groupKey);
	if (bgIt == this->m_backgrounds.end ()) {
	    continue;
	}

	// Compute the bounding box of all viewports in this span group
	const auto& viewports = m_renderContext->getOutput ().getViewports ();
	int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
	bool anyFound = false;

	for (const auto& screenName : spanGroup.screens) {
	    const auto vpIt = viewports.find (screenName);
	    if (vpIt == viewports.end ()) {
		sLog.error ("Span group screen not found: ", screenName);
		continue;
	    }
	    anyFound = true;
	    const auto& vp = vpIt->second;
	    const int x = vp->globalPosition.x;
	    const int y = vp->globalPosition.y;
	    const int w = vp->logicalSize.x;
	    const int h = vp->logicalSize.y;
	    sLog.debug (
		"SPAN DEBUG prepareOutputs: screen '", screenName, "' globalPos=(", x, ",", y, ") logicalSize=", w, "x",
		h
	    );
	    minX = std::min (minX, x);
	    minY = std::min (minY, y);
	    maxX = std::max (maxX, x + w);
	    maxY = std::max (maxY, y + h);
	}

	if (!anyFound) {
	    sLog.error ("No viewports found for span group, skipping");
	    continue;
	}

	sLog.debug (
	    "SPAN DEBUG prepareOutputs: bounding box=(", minX, ",", minY, ",", maxX - minX, ",", maxY - minY, ")"
	);

	WallpaperEngine::Render::CWallpaper::SpanInfo spanInfo;
	spanInfo.totalBounds = { minX, minY, maxX - minX, maxY - minY };

	// Create one shared wallpaper with the span group's scaling mode
	auto sharedWallpaper = WallpaperEngine::Render::CWallpaper::fromWallpaper (
	    *bgIt->second->wallpaper, *m_renderContext, *m_audioContext, m_browserContext.get (), spanGroup.scaling,
	    spanGroup.clamp
	);

	// Convert to shared_ptr so it can be registered for multiple viewports
	std::shared_ptr<WallpaperEngine::Render::CWallpaper> shared (std::move (sharedWallpaper));
	shared->setSpanInfo (spanInfo);

	// Register the same wallpaper for each screen in the span group
	for (const auto& screenName : spanGroup.screens) {
	    m_renderContext->setWallpaper (screenName, shared);
	}
    }
}

void WallpaperApplication::setupOpenGLDebugging () {
#if !NDEBUG
    glDebugMessageCallback (CustomGLDebugCallback, nullptr);
    // GL_DEBUG_OUTPUT defaults to disabled in non-debug contexts (and our EGL context is not
    // created with the debug flag), so the callback never fires without enabling it explicitly
    glEnable (GL_DEBUG_OUTPUT);
    glEnable (GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif
}

void WallpaperApplication::setup () {
    this->setupOutput ();
    this->setupAudio ();
    this->prepareOutputs ();
    this->setupOpenGLDebugging ();
    this->setupControlSocket ();

    if (this->m_context.settings.general.dumpStructure) {
	auto prettyPrinter = Data::Dumpers::StringPrinter ();

	for (const auto& [background, info] : this->m_renderContext->getWallpapers ()) {
	    prettyPrinter.printWallpaper (info->getWallpaperData ());
	}

	std::cout << prettyPrinter.str () << std::endl;
    }

#if DEMOMODE
    // ensure only one background is running so everything can be properly caught
    if (this->m_renderContext->getWallpapers ().size () > 1) {
	sLog.exception ("Demo mode only supports one background");
    }

    int width = this->m_renderContext->getWallpapers ().begin ()->second->getWidth ();
    int height = this->m_renderContext->getWallpapers ().begin ()->second->getHeight ();
    std::vector<uint8_t> pixels (width * height * 3);
    bool initialized = false;
    int frame = 0;
#endif /* DEMOMODE */
}

void WallpaperApplication::render () {
    static time_t seconds;
    static struct tm* timeinfo;

    if (this->m_isPaused) {
	usleep (FULLSCREEN_CHECK_WAIT_TIME);
	if (this->m_fullScreenDetector->anythingFullscreen () && this->m_context.state.general.keepRunning) {
	    return;
	}
	m_renderContext->setPause (false);

	// account for paused duration in playlist timers
	const auto pausedNow = std::chrono::steady_clock::now ();
	const auto pausedDuration = pausedNow - this->m_pauseStart;

	for (auto& [_, playlist] : this->m_activePlaylists) {
	    if (!playlist.definition.settings.updateOnPause) {
		playlist.nextSwitch += pausedDuration;
		playlist.lastUpdate += pausedDuration;
	    }
	}

	this->m_isPaused = false;
    } else {
	// update g_Daytime
	time (&seconds);
	timeinfo = localtime (&seconds);
	g_Daytime = static_cast<float> ((timeinfo->tm_hour * 60) + timeinfo->tm_min) / (24.0f * 60.0f);

	// keep track of the previous frame's time
	g_TimeLast = g_Time;
	// calculate the current time value
	g_Time = m_videoDriver->getRenderTime ();
	// update audio recorder
	m_audioDriver->update ();
	// update the media source
	m_mediaSource->update ();
	// update input information
	m_videoDriver->getInputContext ().update ();
	// process driver events
	m_videoDriver->dispatchEventQueue ();
	// Keep CEF alive while a scene wallpaper is active so browser shutdown and
	// late callbacks from a previous web wallpaper can complete safely.
	if (this->m_browserContext && this->makeAnyViewportCurrent ()) {
	    this->m_browserContext->doMessageLoopWork ();
	}

	if (m_videoDriver->closeRequested ()) {
	    sLog.out ("Stop requested by driver");
	    this->m_context.state.general.keepRunning = false;
	}

#if DEMOMODE
	// wait for a full render cycle before actually starting
	// this gives some extra time for video and web decoders to set themselves up
	// because of size changes
	if (m_videoDriver->getFrameCounter () > (uint32_t)this->m_context.settings.render.maximumFPS) {
	    if (!initialized) {
		width = this->m_renderContext->getWallpapers ().begin ()->second->getWidth ();
		height = this->m_renderContext->getWallpapers ().begin ()->second->getHeight ();
		pixels.reserve (width * height * 3);
		init_encoder ("output.webm", width, height);
		initialized = true;
	    }

	    glBindFramebuffer (
		GL_FRAMEBUFFER, this->m_renderContext->getWallpapers ().begin ()->second->getWallpaperFramebuffer ()
	    );

	    glPixelStorei (GL_PACK_ALIGNMENT, 1);
	    glReadPixels (0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data ());
	    write_video_frame (pixels.data ());
	    frame++;

	    // stop after the given framecount
	    if (frame >= FRAME_COUNT) {
		this->m_context.state.general.keepRunning = false;
	    }
	}
#endif /* DEMOMODE */
	// check for fullscreen windows and wait until there's none fullscreen
	if (this->m_fullScreenDetector->anythingFullscreen () && this->m_context.state.general.keepRunning) {
	    this->m_isPaused = true;
	    this->m_pauseStart = std::chrono::steady_clock::now ();

	    m_renderContext->setPause (true);
	    return;
	}
    }

    this->processControlSocket ();
    this->updatePlaylists ();
    // apply any switch the loader thread finished preparing; runs on the render
    // thread as it uploads textures and rebuilds the wallpaper's GL state
    this->processPreparedSwitches ();

    if (!this->m_context.settings.screenshot.take || this->m_screenShotTaken == true) {
	return;
    }

    if (this->m_videoDriver->getFrameCounter () < this->m_nextFrameScreenshot) {
	return;
    }

    this->takeScreenshot (this->m_context.settings.screenshot.path);
    this->m_screenShotTaken = true;
}

void WallpaperApplication::cleanup () {
    sLog.out ("Stopping");

#if DEMOMODE
    close_encoder ();
#endif /* DEMOMODE */

    SDL_Quit ();
}

void WallpaperApplication::show () {
    setup ();
    while (this->m_context.state.general.keepRunning) {
	render ();
    }

    if (this->m_controlSocket >= 0) {
	close (this->m_controlSocket);

	// only remove the socket file if it is still the one this instance
	// bound - a newer engine may own the path by now
	struct stat info {};
	if (stat (this->m_controlSocketPath.c_str (), &info) == 0 && info.st_ino == this->m_controlSocketInode) {
	    unlink (this->m_controlSocketPath.c_str ());
	}
    }

    this->stopSwitchWorker ();

    cleanup ();
}

bool WallpaperApplication::abnormalTermination () const {
    return this->m_videoDriver->abnormalTermination ();
}

void WallpaperApplication::update (Render::Drivers::Output::OutputViewport* viewport) {
    // render the scene
    m_renderContext->render (viewport);
}

void WallpaperApplication::signal (int signal) {
    sLog.out ("Stop requested by signal ", signal);
    this->m_context.state.general.keepRunning = false;
}

const std::map<std::string, ProjectUniquePtr>& WallpaperApplication::getBackgrounds () const {
    return this->m_backgrounds;
}

ApplicationContext& WallpaperApplication::getContext () const { return this->m_context; }

const WallpaperEngine::Render::Drivers::Output::Output& WallpaperApplication::getOutput () const {
    return this->m_renderContext->getOutput ();
}

void WallpaperApplication::setDestinationFramebuffer (GLuint framebuffer) {
    this->m_destinationFramebuffer = framebuffer;
    // Update all wallpapers with the new destination framebuffer
    for (const auto& [screen, wallpaper] : this->m_renderContext->getWallpapers ()) {
	wallpaper->setDestinationFramebuffer (framebuffer);
    };
}

GLuint WallpaperApplication::getDestinationFramebuffer () const { return this->m_destinationFramebuffer; }
