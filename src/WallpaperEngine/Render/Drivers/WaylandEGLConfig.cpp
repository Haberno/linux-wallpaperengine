#include "WaylandEGLConfig.h"

#include <string>
#include <vector>

using namespace WallpaperEngine::Render::Drivers;

static bool supportsFloatConfigs (EGLDisplay display) {
    const char* extensions = eglQueryString (display, EGL_EXTENSIONS);
    return extensions && (" " + std::string (extensions) + " ").find (" EGL_EXT_pixel_format_float ") != std::string::npos;
}

bool WallpaperEngine::Render::Drivers::queryWaylandEGLConfig (
    EGLDisplay display, EGLConfig config, WaylandEGLConfigProperties& properties
) {
    WaylandEGLConfigProperties result;
    if (!eglGetConfigAttrib (display, config, EGL_RED_SIZE, &result.red)
        || !eglGetConfigAttrib (display, config, EGL_GREEN_SIZE, &result.green)
        || !eglGetConfigAttrib (display, config, EGL_BLUE_SIZE, &result.blue)
        || !eglGetConfigAttrib (display, config, EGL_ALPHA_SIZE, &result.alpha)
        || !eglGetConfigAttrib (display, config, EGL_SAMPLES, &result.samples)) return false;
    if (supportsFloatConfigs (display)
        && !eglGetConfigAttrib (display, config, EGL_COLOR_COMPONENT_TYPE_EXT, &result.componentType)) return false;
    properties = result;
    return true;
}

EGLConfig WallpaperEngine::Render::Drivers::chooseWaylandFloatConfig (
    EGLDisplay display, WaylandEGLConfigProperties& properties
) {
    if (!supportsFloatConfigs (display)) return nullptr;
    const char* extensions = eglQueryString (display, EGL_EXTENSIONS);
    const bool surfaceless = extensions
        && (" " + std::string (extensions) + " ").find (" EGL_KHR_surfaceless_context ") != std::string::npos;
    // The existing build worker needs a pbuffer when surfaceless contexts are absent.
    const EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | (surfaceless ? 0 : EGL_PBUFFER_BIT),
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_COLOR_COMPONENT_TYPE_EXT, EGL_COLOR_COMPONENT_TYPE_FLOAT_EXT,
        EGL_RED_SIZE, 16, EGL_GREEN_SIZE, 16, EGL_BLUE_SIZE, 16, EGL_ALPHA_SIZE, 16,
        EGL_NONE,
    };
    EGLint count = 0;
    if (!eglChooseConfig (display, attributes, nullptr, 0, &count) || count == 0) return nullptr;
    std::vector<EGLConfig> configs (count);
    if (!eglChooseConfig (display, attributes, configs.data (), count, &count)) return nullptr;
    for (EGLint index = 0; index < count; ++index) {
        WaylandEGLConfigProperties candidate;
        if (queryWaylandEGLConfig (display, configs[index], candidate) && candidate.isScRGB ()) {
            properties = candidate;
            return configs[index];
        }
    }
    return nullptr;
}
