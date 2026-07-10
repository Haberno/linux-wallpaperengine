#include "WPSchemeHandlerFactory.h"
#include "WPSchemeHandler.h"
#include "WallpaperEngine/Application/WallpaperApplication.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/WebBrowser/WebBrowserContext.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

using namespace WallpaperEngine::WebBrowser::CEF;

WPSchemeHandlerFactory::WPSchemeHandlerFactory (const WallpaperEngine::Application::WallpaperApplication& application) :
    m_application (application) { }

CefRefPtr<CefResourceHandler> WPSchemeHandlerFactory::Create (
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& scheme_name,
    CefRefPtr<CefRequest> request
) {
    CEF_REQUIRE_IO_THREAD ();

    // the workshop id rides in the host part; resolve the matching background at
    // request time so live-swapped wallpapers are served too
    CefURLParts parts;
    if (!CefParseURL (request->GetURL (), parts)) {
	return nullptr;
    }

    const std::string workshopId = CefString (&parts.host);

    for (const auto& [screen, project] : this->m_application.getBackgrounds ()) {
	if (project != nullptr && project->workshopId == workshopId) {
	    return new WPSchemeHandler (*project);
	}
    }

    // No id match (odd ids happen for local, non-workshop items): fall back to the current web
    // background — an engine serves one wallpaper per screen, so this is the only sane source
    // anyway, and returning nullptr would surface as ERR_UNKNOWN_URL_SCHEME on the wallpaper.
    for (const auto& [screen, project] : this->m_application.getBackgrounds ()) {
	if (project != nullptr && project->wallpaper != nullptr && project->wallpaper->is<Web> ()) {
	    return new WPSchemeHandler (*project);
	}
    }

    return nullptr;
}

std::string WPSchemeHandlerFactory::generateSchemeUrl (const std::string& workshopId, const std::string& file) {
    return std::string (WPENGINE_SCHEME) + "://" + workshopId + "/" + file;
}
