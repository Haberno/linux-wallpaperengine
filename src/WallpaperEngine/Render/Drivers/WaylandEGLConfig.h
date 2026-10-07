#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace WallpaperEngine::Render::Drivers {
struct WaylandEGLConfigProperties {
    EGLint red = 0, green = 0, blue = 0, alpha = 0;
    EGLint componentType = EGL_COLOR_COMPONENT_TYPE_FIXED_EXT;
    EGLint samples = 0;
    bool isScRGB () const {
        return componentType == EGL_COLOR_COMPONENT_TYPE_FLOAT_EXT
            && red == 16 && green == 16 && blue == 16 && alpha == 16;
    }
};

bool queryWaylandEGLConfig (EGLDisplay display, EGLConfig config, WaylandEGLConfigProperties& properties);
EGLConfig chooseWaylandFloatConfig (EGLDisplay display, WaylandEGLConfigProperties& properties);
} // namespace WallpaperEngine::Render::Drivers
