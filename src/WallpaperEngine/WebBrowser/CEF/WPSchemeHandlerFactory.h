#pragma once

#include "include/cef_scheme.h"
#include <string>

namespace WallpaperEngine::Application {
class WallpaperApplication;
}

namespace WallpaperEngine::Data::Model {
struct Project;
}

namespace WallpaperEngine::WebBrowser::CEF {
using namespace WallpaperEngine::Data::Model;

/**
 * Serves wallpaper files for the fixed "wp" scheme. The workshop id travels in the
 * URL host (wp://<workshopId>/<file>) and the matching background is resolved per
 * request, so wallpapers swapped in over the control socket work without having to
 * register a new scheme (CEF only accepts custom scheme names at initialization).
 */
class WPSchemeHandlerFactory : public CefSchemeHandlerFactory {
public:
    explicit WPSchemeHandlerFactory (const WallpaperEngine::Application::WallpaperApplication& application);

    CefRefPtr<CefResourceHandler> Create (
	CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& scheme_name,
	CefRefPtr<CefRequest> request
    ) override;

    /** Full URL that loads the given file of the given wallpaper through the wp scheme */
    static std::string generateSchemeUrl (const std::string& workshopId, const std::string& file);

private:
    const WallpaperEngine::Application::WallpaperApplication& m_application;

    IMPLEMENT_REFCOUNTING (WPSchemeHandlerFactory);
    DISALLOW_COPY_AND_ASSIGN (WPSchemeHandlerFactory);
};
} // namespace WallpaperEngine::WebBrowser::CEF
