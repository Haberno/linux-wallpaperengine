#pragma once

#include <cstdint>

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

namespace WallpaperEngine::WebBrowser {
class WebBrowserContext;
}

namespace WallpaperEngine::WebBrowser::CEF {
// *************************************************************************
//! \brief Provide access to browser-instance-specific callbacks. A single
//! CefClient instance can be shared among any number of browsers.
// *************************************************************************
class BrowserClient : public CefClient, public CefLifeSpanHandler, public CefLoadHandler {
public:
    BrowserClient (CefRefPtr<CefRenderHandler> ptr, WallpaperEngine::WebBrowser::WebBrowserContext& context);

    [[nodiscard]] CefRefPtr<CefRenderHandler> GetRenderHandler () override;
    [[nodiscard]] CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler () override;
    [[nodiscard]] CefRefPtr<CefLoadHandler> GetLoadHandler () override;
    void OnLoadEnd (CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override;
    [[nodiscard]] uint64_t getLoadGeneration () const { return m_loadGeneration; }
    void OnAfterCreated (CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose (CefRefPtr<CefBrowser> browser) override;

    CefRefPtr<CefRenderHandler> m_renderHandler = nullptr;

    IMPLEMENT_REFCOUNTING (BrowserClient);

private:
    WallpaperEngine::WebBrowser::WebBrowserContext& m_browserContext;
    uint64_t m_loadGeneration = 0;
};
} // namespace WallpaperEngine::WebBrowser::CEF
