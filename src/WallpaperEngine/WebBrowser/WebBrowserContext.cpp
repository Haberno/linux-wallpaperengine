#include "WebBrowserContext.h"
#include "CEF/BrowserApp.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/WebBrowser/CEF/SubprocessApp.h"
#include "include/cef_app.h"
#include "include/cef_render_handler.h"
#include <chrono>
#include <filesystem>
#include <random>
#include <ranges>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace WallpaperEngine::WebBrowser;

int WebBrowserContext::executeSubprocess (int argc, char* argv[]) {
    CefMainArgs main_args (argc, argv);

    // Subprocess only registers the fixed wp scheme; avoid any file IO here — it would close the
    // inherited ICU data descriptor before CefExecuteProcess reads it.
    const CefRefPtr<CefApp> app = new CEF::SubprocessApp ();
    const int exitCode = CefExecuteProcess (main_args, app, nullptr);
    // A helper process always terminates here; CefExecuteProcess returns its exit
    // code (>= 0). Guard against -1 just in case so we still exit cleanly.
    return exitCode < 0 ? 0 : exitCode;
}

// TODO: THIS IS USED TO GENERATE A RANDOM FOLDER FOR THE CHROME PROFILE, MAYBE A DIFFERENT APPROACH WOULD BE BETTER?
namespace uuid {
static std::random_device rd;
static std::mt19937 gen (rd ());
static std::uniform_int_distribution<> dis (0, 15);
static std::uniform_int_distribution<> dis2 (8, 11);

std::string generate_uuid_v4 () {
    std::stringstream ss;
    int i;
    ss << std::hex;
    for (i = 0; i < 8; i++) {
	ss << dis (gen);
    }
    ss << "-";
    for (i = 0; i < 4; i++) {
	ss << dis (gen);
    }
    ss << "-4";
    for (i = 0; i < 3; i++) {
	ss << dis (gen);
    }
    ss << "-";
    ss << dis2 (gen);
    for (i = 0; i < 3; i++) {
	ss << dis (gen);
    }
    ss << "-";
    for (i = 0; i < 12; i++) {
	ss << dis (gen);
    };
    return ss.str ();
}
}

WebBrowserContext::WebBrowserContext (WallpaperEngine::Application::WallpaperApplication& wallpaperApplication) :
    m_browserApplication (nullptr), m_wallpaperApplication (wallpaperApplication) {
    CefMainArgs main_args (
	this->m_wallpaperApplication.getContext ().getArgc (), this->m_wallpaperApplication.getContext ().getArgv ()
    );

    // Only the browser process reaches here (helpers are handled in executeSubprocess), so never call
    // CefExecuteProcess here: it puts the ICU loader into "child" mode and fails to find its data fd.
    this->m_browserApplication = new CEF::BrowserApp (wallpaperApplication);

    // Configurate Chromium
    CefSettings settings;
    std::string cache_path = (std::filesystem::temp_directory_path () / uuid::generate_uuid_v4 ()).string ();

    // Point CEF at its resources dir (icudtl.dat, *.pak, locales/) next to the executable;
    // without it subprocesses fail to load ICU and web wallpapers crash.
    char proc_path[4096];
    const ssize_t proc_len = readlink ("/proc/self/exe", proc_path, sizeof (proc_path) - 1);
    if (proc_len > 0) {
	proc_path[proc_len] = '\0';
	const std::string exe_dir = std::filesystem::path (proc_path).parent_path ().string ();
	CefString (&settings.resources_dir_path) = exe_dir;
	CefString (&settings.locales_dir_path) = exe_dir + "/locales";
    }

    cef_string_utf8_to_utf16 (cache_path.c_str (), cache_path.length (), &settings.root_cache_path);
    settings.windowless_rendering_enabled = true;
    // No sandbox: content is local/trusted, and the sandbox's fd remapping breaks ICU-data loading,
    // so disabling it lets every process read icudtl.dat from resources_dir_path directly.
    settings.no_sandbox = true;

    // spawns two new processess

    this->m_initialized = CefInitialize (main_args, settings, this->m_browserApplication, nullptr);
    if (!this->m_initialized) {
	sLog.exception ("CefInitialize: failed");
    }
}

void WebBrowserContext::doMessageLoopWork () {
    if (!this->m_initialized) {
	return;
    }

    CEF_REQUIRE_UI_THREAD ();
    CefDoMessageLoopWork ();
}

void WebBrowserContext::onBrowserCreated (CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD ();
    if (browser != nullptr) {
	this->m_browsers.insert_or_assign (browser->GetIdentifier (), browser);
    }
}

void WebBrowserContext::onBrowserClosed (CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD ();
    if (browser != nullptr) {
	this->m_browsers.erase (browser->GetIdentifier ());
    }
}

WebBrowserContext::~WebBrowserContext () {
    if (!this->m_initialized) {
	return;
    }

    CEF_REQUIRE_UI_THREAD ();
    std::vector<CefRefPtr<CefBrowser>> browsers;
    browsers.reserve (this->m_browsers.size ());
    for (const auto& browser : this->m_browsers | std::views::values) {
	browsers.push_back (browser);
    }
    for (const auto& browser : browsers) {
	if (browser != nullptr && browser->GetHost () != nullptr) {
	    browser->GetHost ()->CloseBrowser (true);
	}
    }

    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (!this->m_browsers.empty () && std::chrono::steady_clock::now () < deadline) {
	CefDoMessageLoopWork ();
	std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    if (!this->m_browsers.empty ()) {
	sLog.error ("CEF browser shutdown timed out with ", this->m_browsers.size (), " browser(s) still open");
    }

    sLog.out ("Shutting down CEF");
    CefShutdown ();
    this->m_initialized = false;
}
