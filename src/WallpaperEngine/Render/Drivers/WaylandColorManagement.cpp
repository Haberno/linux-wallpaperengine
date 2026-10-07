#include "WaylandColorManagement.h"

#include "color-management-v1-protocol.h"
#include <algorithm>

using namespace WallpaperEngine::Render::Drivers;

WaylandColorManagement::~WaylandColorManagement () { reset (); }

void WaylandColorManagement::bind (wl_registry* registry, uint32_t name, uint32_t version) {
    if (m_manager || version == 0) return;
    m_globalName = name;
    m_manager = static_cast<wp_color_manager_v1*> (
        wl_registry_bind (registry, name, &wp_color_manager_v1_interface, std::min (version, 3u))
    );
    if (!m_manager) return;
    static const wp_color_manager_v1_listener listener = {
        .supported_intent = [] (void* data, wp_color_manager_v1*, uint32_t intent) {
            if (intent == WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL)
                static_cast<WaylandColorManagement*> (data)->m_perceptual = true;
        },
        .supported_feature = [] (void* data, wp_color_manager_v1*, uint32_t feature) {
            if (feature == WP_COLOR_MANAGER_V1_FEATURE_WINDOWS_SCRGB)
                static_cast<WaylandColorManagement*> (data)->m_windowsScRGB = true;
        },
        .supported_tf_named = [] (void*, wp_color_manager_v1*, uint32_t) { },
        .supported_primaries_named = [] (void*, wp_color_manager_v1*, uint32_t) { },
        .done = [] (void* data, wp_color_manager_v1*) {
            auto& owner = *static_cast<WaylandColorManagement*> (data);
            owner.m_done = true;
            if (!owner.m_windowsScRGB || !owner.m_perceptual || owner.m_description) return;
            owner.m_description = wp_color_manager_v1_create_windows_scrgb (owner.m_manager);
            if (!owner.m_description) return;
            static const wp_image_description_v1_listener descriptionListener = {
                .failed = [] (void* data, wp_image_description_v1* description, uint32_t, const char*) {
                    auto& owner = *static_cast<WaylandColorManagement*> (data);
                    owner.m_ready = false;
                    wp_image_description_v1_destroy (description);
                    owner.m_description = nullptr;
                },
                .ready = [] (void* data, wp_image_description_v1*, uint32_t) {
                    static_cast<WaylandColorManagement*> (data)->m_ready = true;
                },
                .ready2 = [] (void* data, wp_image_description_v1*, uint32_t, uint32_t) {
                    static_cast<WaylandColorManagement*> (data)->m_ready = true;
                },
            };
            wp_image_description_v1_add_listener (owner.m_description, &descriptionListener, &owner);
        },
    };
    wp_color_manager_v1_add_listener (m_manager, &listener, this);
}

void WaylandColorManagement::removeGlobal (uint32_t name) {
    if (name == m_globalName) reset ();
}

void WaylandColorManagement::reset () {
    if (m_description) wp_image_description_v1_destroy (m_description);
    if (m_manager) wp_color_manager_v1_destroy (m_manager);
    m_description = nullptr;
    m_manager = nullptr;
    m_globalName = 0;
    m_done = m_windowsScRGB = m_perceptual = m_ready = false;
}

bool WaylandColorManagement::hasScRGBProtocol () const {
    return m_manager && m_done && m_windowsScRGB && m_perceptual;
}

bool WaylandColorManagement::ready () const {
    return hasScRGBProtocol () && m_description && m_ready;
}

wp_color_management_surface_v1* WaylandColorManagement::getSurface (wl_surface* surface) const {
    return m_manager && surface ? wp_color_manager_v1_get_surface (m_manager, surface) : nullptr;
}

void WaylandColorManagement::queuePresentation (wp_color_management_surface_v1* surface, bool scRGB) const {
    if (!surface) return;
    // This pending state is committed by the caller's existing EGL buffer swap.
    if (scRGB && ready ())
        wp_color_management_surface_v1_set_image_description (
            surface, m_description, WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL
        );
    else
        wp_color_management_surface_v1_unset_image_description (surface);
}
