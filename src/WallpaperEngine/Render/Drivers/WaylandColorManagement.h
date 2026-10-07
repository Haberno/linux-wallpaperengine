#pragma once

#include <cstdint>

struct wl_registry;
struct wl_surface;
struct wp_color_manager_v1;
struct wp_image_description_v1;
struct wp_color_management_surface_v1;

namespace WallpaperEngine::Render::Drivers {
class WaylandColorManagement final {
public:
    WaylandColorManagement () = default;
    ~WaylandColorManagement ();
    WaylandColorManagement (const WaylandColorManagement&) = delete;
    WaylandColorManagement& operator= (const WaylandColorManagement&) = delete;

    void bind (wl_registry* registry, uint32_t name, uint32_t version);
    void removeGlobal (uint32_t name);
    void reset ();
    bool hasScRGBProtocol () const;
    bool ready () const;
    wp_color_management_surface_v1* getSurface (wl_surface* surface) const;
    void queuePresentation (wp_color_management_surface_v1* surface, bool scRGB) const;

private:
    wp_color_manager_v1* m_manager = nullptr;
    wp_image_description_v1* m_description = nullptr;
    uint32_t m_globalName = 0;
    bool m_done = false;
    bool m_windowsScRGB = false;
    bool m_perceptual = false;
    bool m_ready = false;
};
} // namespace WallpaperEngine::Render::Drivers
