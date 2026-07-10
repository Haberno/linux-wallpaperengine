#pragma once

// Matrices manipulation for OpenGL
#include <glm/ext.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "WallpaperEngine/Audio/AudioStream.h"
#include "WallpaperEngine/Render/CWallpaper.h"
#include "WallpaperEngine/WebBrowser/CEF/BrowserClient.h"
#include "WallpaperEngine/WebBrowser/CEF/RenderHandler.h"

#include "WallpaperEngine/Data/Model/Wallpaper.h"

namespace WallpaperEngine::WebBrowser::CEF {
class RenderHandler;
}

namespace WallpaperEngine::Render::Wallpapers {
class CWeb : public CWallpaper {
public:
    CWeb (
	const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
	WallpaperEngine::WebBrowser::WebBrowserContext& browserContext,
	const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode
    );
    ~CWeb () override;
    [[nodiscard]] int getWidth () const override { return this->m_width; }

    [[nodiscard]] int getHeight () const override { return this->m_height; }

    void setSize (int width, int height);

protected:
    void renderFrame (const glm::ivec4& viewport) override;
    void updateMouse (const glm::ivec4& viewport);
    const Web& getWeb () const { return *this->getWallpaperData ().as<Web> (); }

    friend class CWallpaper;

private:
    // Wallpaper Engine web API bridge (browser side): feeds the injected
    // window.wallpaper* listeners with live audio, media (via playerctl/MPRIS) and
    // the wallpaper's properties, by calling the page's __wp* entry points.
    void pushBridgeData ();
    std::string m_lastArtSent;
    bool m_propertiesSent = false;
    uint64_t m_frame = 0;

private:
    WallpaperEngine::WebBrowser::WebBrowserContext& m_browserContext;
    CefRefPtr<CefBrowser> m_browser = nullptr;
    CefRefPtr<WallpaperEngine::WebBrowser::CEF::BrowserClient> m_client = nullptr;
    CefRefPtr<WallpaperEngine::WebBrowser::CEF::RenderHandler> m_renderHandler = nullptr;

    int m_width = 16;
    int m_height = 17;

    WallpaperEngine::Input::MouseClickStatus m_leftClick = Input::Released;
    WallpaperEngine::Input::MouseClickStatus m_rightClick = Input::Released;

    glm::vec2 m_mousePosition = {};
    glm::vec2 m_mousePositionLast = {};
};
}
