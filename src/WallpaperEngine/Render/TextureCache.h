#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "TextureProvider.h"
#include "WallpaperEngine/Render/Helpers/ContextAware.h"
#include "WallpaperEngine/Render/RenderContext.h"

using namespace WallpaperEngine::Render;

namespace WallpaperEngine::Render {
class AlbumTexture;
namespace Helpers {
    class ContextAware;
}

class RenderContext;

class TextureCache final : Helpers::ContextAware {
public:
    explicit TextureCache (RenderContext& context);
    ~TextureCache () override;

    /**
     * Checks if the given texture was already loaded and returns it
     * If the texture was not loaded yet, it tries to load it from the container
     *
     * @param filename
     * @return
     */
    std::shared_ptr<const TextureProvider> resolve (const std::string& filename);

    /**
     * Registers a texture in the cache
     *
     * @param name
     * @param texture
     */
    void store (const std::string& name, std::shared_ptr<const TextureProvider> texture);

private:
    /** Bookkeeping for LRU eviction */
    struct CacheEntry {
	std::shared_ptr<const TextureProvider> texture;
	/** Monotonic timestamp of the last resolve/store, higher = more recent */
	uint64_t lastUsed;
	/** Rough GPU-side size estimate used for the cache budget */
	size_t approximateBytes;
    };

    /**
     * Evicts least-recently-used textures that only the cache still references
     * until the estimated cache size is back under budget
     */
    void trim ();

    /** The previous album thumbnail texture */
    std::shared_ptr<const AlbumTexture> m_previousThumbnail = nullptr;
    /** The current album thumbnail texture */
    std::shared_ptr<const AlbumTexture> m_currentThumbnail = nullptr;
    /** Cached textures */
    std::map<std::string, CacheEntry> m_textureCache = {};
    /** Monotonic use counter for LRU ordering */
    uint64_t m_useCounter = 0;
    /** Estimated total bytes held by cached textures */
    size_t m_cacheBytes = 0;
    /** The callback to de-register media events */
    std::function<void ()> m_mediaCallback;
};
} // namespace WallpaperEngine::Render
