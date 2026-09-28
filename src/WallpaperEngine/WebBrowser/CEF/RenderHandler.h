#pragma once

#include "WallpaperEngine/Render/Wallpapers/CWeb.h"
#include "include/cef_browser.h"

#include <cstdint>
#include <vector>

namespace WallpaperEngine::Render::Wallpapers {
class CWeb;
}

namespace WallpaperEngine::WebBrowser::CEF {
// *************************************************************************
//! \brief Private implementation to handle CEF events to draw the web page.
// *************************************************************************
class RenderHandler : public CefRenderHandler {
public:
    explicit RenderHandler (WallpaperEngine::Render::Wallpapers::CWeb* webdata);

    //! \brief
    ~RenderHandler () override = default;

    //! \brief CefRenderHandler interface
    void GetViewRect (CefRefPtr<CefBrowser> browser, CefRect& rect) override;

    //! \brief CefRenderHandler interface
    //! Update the OpenGL texture.
    void OnPaint (
	CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirtyRects, const void* buffer, int width,
	int height
    ) override;

    /** Upload only the latest queued browser image when the wallpaper is drawn. */
    void uploadTexture ();

    /** Stops late CEF callbacks from accessing a CWeb that is being destroyed. */
    void detach ();

    //! \brief CefBase interface
    IMPLEMENT_REFCOUNTING (RenderHandler);

private:
    WallpaperEngine::Render::Wallpapers::CWeb* m_webdata = nullptr;
    std::vector<uint8_t> m_pixels;
    int m_paintWidth = 0;
    int m_paintHeight = 0;
    bool m_pendingPaint = false;
};
} // namespace WallpaperEngine::WebBrowser::CEF
