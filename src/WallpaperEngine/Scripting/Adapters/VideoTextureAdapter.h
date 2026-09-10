#pragma once

#include "ObjectAdapter.h"
#include <memory>

namespace WallpaperEngine::Render {
class TextureProvider;
}

namespace WallpaperEngine::Scripting::Adapters {
class VideoTextureAdapter : public ObjectAdapter {
public:
    explicit VideoTextureAdapter (ScriptEngine& engine);
    using ObjectAdapter::instantiate;
    JSValue instantiate (std::shared_ptr<const Render::TextureProvider> texture);
};
}
