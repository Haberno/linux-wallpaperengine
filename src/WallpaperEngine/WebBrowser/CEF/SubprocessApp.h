#pragma once

#include "WPSchemeHandlerFactory.h"
#include "WallpaperEngine/WebBrowser/WebBrowserContext.h"
#include "include/cef_app.h"
#include "include/cef_render_process_handler.h"

#include <string>
#include <vector>

namespace WallpaperEngine::Application {
class WallpaperApplication;
}

namespace WallpaperEngine::WebBrowser::CEF {
class SubprocessApp : public CefApp, public CefRenderProcessHandler {
public:
    explicit SubprocessApp (WallpaperEngine::Application::WallpaperApplication& application);
    /** Helper-process ctor: registers only the wp scheme, no file IO (would close the ICU data fd). */
    SubprocessApp () = default;

    void OnRegisterCustomSchemes (CefRawPtr<CefSchemeRegistrar> registrar) override;

    // Render-process side of the web API bridge: injects the window.wallpaper* shim into each frame
    // so web wallpapers can register audio/media/property listeners (driven by the browser via CWeb).
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler () override { return this; }
    void OnContextCreated (
	CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context
    ) override;

protected:
    const WallpaperEngine::Application::WallpaperApplication& getApplication () const;

private:
    WallpaperEngine::Application::WallpaperApplication* m_application = nullptr;
    IMPLEMENT_REFCOUNTING (SubprocessApp);
    DISALLOW_COPY_AND_ASSIGN (SubprocessApp);
};
}
