#include "BrowserClient.h"
#include "WallpaperEngine/WebBrowser/WebBrowserContext.h"
#include "include/wrapper/cef_helpers.h"

using namespace WallpaperEngine::WebBrowser::CEF;

BrowserClient::BrowserClient (
    CefRefPtr<CefRenderHandler> ptr, WallpaperEngine::WebBrowser::WebBrowserContext& context
) : m_renderHandler (std::move (ptr)), m_browserContext (context) { }

CefRefPtr<CefRenderHandler> BrowserClient::GetRenderHandler () { return m_renderHandler; }

CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler () { return this; }

CefRefPtr<CefLoadHandler> BrowserClient::GetLoadHandler () { return this; }

void BrowserClient::OnLoadEnd (CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
    CEF_REQUIRE_UI_THREAD ();
    if (frame->IsMain ()) {
	++m_loadGeneration;
    }
}

void BrowserClient::OnAfterCreated (CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD ();
    this->m_browserContext.onBrowserCreated (browser);
}

void BrowserClient::OnBeforeClose (CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD ();
    this->m_browserContext.onBrowserClosed (browser);
}
