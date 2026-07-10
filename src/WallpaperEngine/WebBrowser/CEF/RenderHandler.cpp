#include "RenderHandler.h"
#include "include/wrapper/cef_helpers.h"

using namespace WallpaperEngine::WebBrowser::CEF;

RenderHandler::RenderHandler (WallpaperEngine::Render::Wallpapers::CWeb* webdata) : m_webdata (webdata) { }

// Required by CEF
void RenderHandler::GetViewRect (CefRefPtr<CefBrowser> browser, CefRect& rect) {
    CEF_REQUIRE_UI_THREAD ();
    const auto* webdata = this->m_webdata;
    rect = webdata == nullptr ? CefRect (0, 0, 1, 1) : CefRect (0, 0, webdata->getWidth (), webdata->getHeight ());
}

// Will be executed in CEF message loop
void RenderHandler::OnPaint (
    CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirtyRects, const void* buffer,
    const int width, const int height
) {
    CEF_REQUIRE_UI_THREAD ();
    const auto* webdata = this->m_webdata;
    if (webdata == nullptr || buffer == nullptr || width <= 0 || height <= 0) {
	return;
    }

    glActiveTexture (GL_TEXTURE0);
    // Upload into the wallpaper color texture. The framebuffer handle is not a
    // texture and leaves the visible web wallpaper blank when bound here.
    glBindTexture (GL_TEXTURE_2D, webdata->getWallpaperTexture ());
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, buffer);
    glBindTexture (GL_TEXTURE_2D, 0);
}

void RenderHandler::detach () {
    // This raw back-pointer is safe because CEF uses its single-threaded external
    // message loop; callbacks and CWeb destruction are serialized on TID_UI.
    CEF_REQUIRE_UI_THREAD ();
    this->m_webdata = nullptr;
}
