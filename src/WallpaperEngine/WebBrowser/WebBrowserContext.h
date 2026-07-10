#pragma once

#include "WallpaperEngine/Application/ApplicationContext.h"
#include "WallpaperEngine/Application/WallpaperApplication.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_browser_process_handler.h"
#include "include/wrapper/cef_helpers.h"

#include <map>

#define WPENGINE_SCHEME "wp"

namespace WallpaperEngine::WebBrowser::CEF {
class BrowserApp;
}

namespace WallpaperEngine::WebBrowser {
class WebBrowserContext {
public:
    explicit WebBrowserContext (WallpaperEngine::Application::WallpaperApplication& wallpaperApplication);
    ~WebBrowserContext ();

    /**
     * Entry point for CEF helper (subprocess) processes. Must be called at the very
     * top of main(), before any other initialisation, so the inherited ICU-data
     * file descriptor is still valid when CEF reads it. Registers the wp<workshopId>
     * schemes parsed from argv's --bg paths and hands off to CefExecuteProcess.
     * Returns the process exit code (the caller should return it from main()).
     */
    static int executeSubprocess (int argc, char* argv[]);

    /** Pumps CEF on the browser/UI thread from the application's main loop. */
    void doMessageLoopWork ();
    /** BrowserClient lifecycle callbacks used to make shutdown deterministic. */
    void onBrowserCreated (CefRefPtr<CefBrowser> browser);
    void onBrowserClosed (CefRefPtr<CefBrowser> browser);

private:
    CefRefPtr<CefApp> m_browserApplication = nullptr;
    CefRefPtr<CefCommandLine> m_commandLine = nullptr;
    WallpaperEngine::Application::WallpaperApplication& m_wallpaperApplication;
    std::map<int, CefRefPtr<CefBrowser>> m_browsers;
    bool m_initialized = false;
};
} // namespace WallpaperEngine::WebBrowser
