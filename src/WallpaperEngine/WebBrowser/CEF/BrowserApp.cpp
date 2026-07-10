#include "BrowserApp.h"
#include "WallpaperEngine/Logging/Log.h"
#include <sstream>

#include <cstdlib>
#include <string>

using namespace WallpaperEngine::WebBrowser::CEF;

BrowserApp::BrowserApp (WallpaperEngine::Application::WallpaperApplication& application) :
    SubprocessApp (application) { }

CefRefPtr<CefBrowserProcessHandler> BrowserApp::GetBrowserProcessHandler () { return this; }

void BrowserApp::OnContextInitialized () {
    // one factory for the fixed wp scheme; it resolves the wallpaper from the URL
    // host per request, so live-swapped backgrounds are served without re-registering
    CefRegisterSchemeHandlerFactory (
	WPENGINE_SCHEME, static_cast<const char*> (nullptr), new WPSchemeHandlerFactory (this->getApplication ())
    );
}

void BrowserApp::OnBeforeCommandLineProcessing (const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
    command_line->AppendSwitchWithValue (
	"--disable-features",
	"IsolateOrigins,HardwareMediaKeyHandling,WebContentsOcclusion,RendererCodeIntegrityEnabled,site-per-process"
    );
    command_line->AppendSwitch ("--disable-gpu-shader-disk-cache");
    command_line->AppendSwitch ("--disable-site-isolation-trials");
    command_line->AppendSwitch ("--disable-web-security");
    command_line->AppendSwitchWithValue ("--remote-allow-origins", "*");
    command_line->AppendSwitchWithValue ("--autoplay-policy", "no-user-gesture-required");
    command_line->AppendSwitch ("--disable-background-timer-throttling");
    command_line->AppendSwitch ("--disable-backgrounding-occluded-windows");
    command_line->AppendSwitch ("--disable-background-media-suspend");
    command_line->AppendSwitch ("--disable-renderer-backgrounding");
    command_line->AppendSwitch ("--disable-test-root-certs");
    command_line->AppendSwitch ("--disable-bundled-ppapi-flash");
    command_line->AppendSwitch ("--disable-breakpad");
    command_line->AppendSwitch ("--disable-field-trial-config");
    command_line->AppendSwitch ("--no-experiments");
    // CEF Wayland GPU-stability knobs (optional env overrides: WPE_CEF_NO_IPG/OZONE/ANGLE/EXTRA).
    // In-process GPU avoids the standalone GPU process crashing on Wayland offscreen rendering.
    if (std::getenv ("WPE_CEF_NO_IPG") == nullptr) {
	command_line->AppendSwitch ("--in-process-gpu");
    }

    // On Wayland, the Wayland Ozone platform + EGL ANGLE backend keep offscreen rendering stable.
    const char* sessionType = std::getenv ("XDG_SESSION_TYPE");
    const char* waylandDisplay = std::getenv ("WAYLAND_DISPLAY");
    const bool sessionIsWayland = sessionType != nullptr && std::string (sessionType) == "wayland";
    const bool hasWaylandDisplay = waylandDisplay != nullptr && waylandDisplay[0] != '\0';

    if (sessionIsWayland || hasWaylandDisplay) {
	// Overridable for testing which backend gives stable GPU compositing
	// (needed for CSS backdrop-filter). Defaults match the previous behaviour.
	const char* ozEnv = std::getenv ("WPE_CEF_OZONE");
	const char* anEnv = std::getenv ("WPE_CEF_ANGLE");
	const std::string ozone = ozEnv != nullptr ? ozEnv : "wayland";
	const std::string angle = anEnv != nullptr ? anEnv : "gl-egl";
	command_line->AppendSwitchWithValue ("--ozone-platform", ozone);
	if (angle != "skip") {
	    command_line->AppendSwitchWithValue ("--use-angle", angle);
	}
	command_line->AppendSwitchWithValue ("--enable-features", "UseOzonePlatform");
    }

    // Extra CEF flags for testing GPU-stability configurations, space separated,
    // e.g. WPE_CEF_EXTRA="--in-process-gpu --disable-gpu-sandbox".
    if (const char* extra = std::getenv ("WPE_CEF_EXTRA")) {
	std::string s = extra, tok;
	std::stringstream ss (s);
	while (ss >> tok) {
	    std::string flag = tok;
	    while (!flag.empty () && flag[0] == '-') {
		flag.erase (flag.begin ());
	    }
	    const auto eq = flag.find ('=');
	    if (eq == std::string::npos) {
		command_line->AppendSwitch (flag);
	    } else {
		command_line->AppendSwitchWithValue (flag.substr (0, eq), flag.substr (eq + 1));
	    }
	}
    }

    // TODO: ACTIVATE THIS IF WE EVER SUPPORT MACOS OFFICIALLY
    /*
if (process_type.empty()) {
#if defined(OS_MACOSX)
  // Disable the macOS keychain prompt. Cookies will not be encrypted.
  command_line->AppendSwitch("use-mock-keychain");
#endif
}*/
}

void BrowserApp::OnBeforeChildProcessLaunch (CefRefPtr<CefCommandLine> command_line) {
    // add back any parameters we had before so the new process can load up everything needed
    for (int i = 1; i < this->getApplication ().getContext ().getArgc (); i++) {
	command_line->AppendArgument (this->getApplication ().getContext ().getArgv ()[i]);
    }
}