#include "TextureCache.h"

#include "AlbumTexture.h"
#include "WallpaperEngine/FileSystem/Container.h"

#include "CTexture.h"
#include "WallpaperEngine/Assets/AssetLoadException.h"
#include "WallpaperEngine/Render/Helpers/ContextAware.h"

#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Parsers/TextureParser.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::FileSystem;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Assets;

TextureCache::TextureCache (RenderContext& context) : Helpers::ContextAware (context) {
    // these textures are special cases, so make sure they're created only upon request
    this->m_currentThumbnail = std::make_shared<AlbumTexture> (this->getContext ());

#if !NDEBUG
    glObjectLabel (GL_TEXTURE, this->m_currentThumbnail->getTextureID (0), -1, "$mediaThumbnail");
#endif

    this->m_previousThumbnail = std::make_shared<AlbumTexture> (this->getContext ());

#if !NDEBUG
    glObjectLabel (GL_TEXTURE, this->m_previousThumbnail->getTextureID (0), -1, "$mediaPreviousThumbnail");
#endif

    // load the latest texture (if available)
    this->m_currentThumbnail->load ();

    // add these to the cache and return the right one
    this->store ("$mediaThumbnail", this->m_currentThumbnail);
    this->store ("$mediaPreviousThumbnail", this->m_previousThumbnail);

    this->m_mediaCallback = this->getContext ().getMediaSource ().addAlbumArtListener (
	[this] (const Media::MediaSource::MediaInfo& data) {
	    if (this->m_currentThumbnail->isReady ()) {
		// copy over pixel data and setup the new texture with the new data
		this->m_previousThumbnail->copyContents (*this->m_currentThumbnail);
	    }

	    // load the next image
	    this->m_currentThumbnail->load ();
	}
    );
}

TextureCache::~TextureCache () { this->m_mediaCallback (); }

namespace {
/** Soft cap for the texture cache; least-recently-used unreferenced entries are evicted past this */
constexpr size_t TEXTURE_CACHE_BUDGET_BYTES = 512ULL * 1024 * 1024;

size_t estimateTextureBytes (const TextureProvider& texture) {
    // RGBA8 at the allocated texture size plus a third for mipmaps; compressed
    // formats overestimate a bit, which is fine for a soft budget
    const size_t base = static_cast<size_t> (texture.getTextureWidth (0)) * texture.getTextureHeight (0) * 4;

    // video textures keep the whole source file pinned in RAM for mpv to stream from,
    // which dwarfs the GPU-side estimate and previously made them look free to cache
    return base + base / 3 + texture.getRetainedCpuBytes ();
}
} // namespace

std::shared_ptr<const TextureProvider> TextureCache::resolve (const std::string& filename) {
    if (const auto found = this->m_textureCache.find (filename); found != this->m_textureCache.end ()) {
	found->second.lastUsed = ++this->m_useCounter;
	return found->second.texture;
    }

    // search for the texture in all the different containers just in case
    for (const auto& project : this->getContext ().getApp ().getBackgrounds () | std::views::values) {
	try {
	    const auto contents = project->assetLocator->texture (filename);
	    auto stream = BinaryReader (contents);

	    // Create metadata loader lambda that captures the assetLocator
	    // so we need to construct the full path here
	    auto metadataLoader = [&project] (const std::string& metaFilename) -> std::string {
		std::filesystem::path fullPath = std::filesystem::path ("materials") / metaFilename;
		return project->assetLocator->readString (fullPath);
	    };

	    auto parsedTexture = TextureParser::parse (stream, filename, metadataLoader);
	    auto texture = std::make_shared<CTexture> (this->getContext (), std::move (parsedTexture));

#if !NDEBUG
	    glObjectLabel (GL_TEXTURE, texture->getTextureID (0), -1, filename.c_str ());
#endif

	    this->store (filename, texture);

	    return texture;
	} catch (AssetLoadException&) {
	    // ignored, this happens if we're looking at the wrong background
	}
    }

    // TODO: FILL IN WITH A CHECKERED PATTERN TEXTURE INSTEAD?
    throw AssetLoadException ("Cannot find file", filename, std::error_code ());
}

void TextureCache::store (const std::string& name, std::shared_ptr<const TextureProvider> texture) {
    const size_t bytes = estimateTextureBytes (*texture);

    if (const auto it = this->m_textureCache.find (name); it != this->m_textureCache.end ()) {
	this->m_cacheBytes -= it->second.approximateBytes;
	it->second = CacheEntry {std::move (texture), ++this->m_useCounter, bytes};
    } else {
	this->m_textureCache.emplace (name, CacheEntry {std::move (texture), ++this->m_useCounter, bytes});
    }

    this->m_cacheBytes += bytes;
    this->trim ();
}

void TextureCache::updateAll () const {
    // textures only referenced as effect/pass inputs have no CImage driving their
    // update, so tick every cached texture; update() is a no-op for static textures
    // and for videos whose usage count is zero
    for (const auto& entry : this->m_textureCache | std::views::values) {
	entry.texture->update ();
    }
}

void TextureCache::trim () {
    if (this->m_cacheBytes <= TEXTURE_CACHE_BUDGET_BYTES) {
	return;
    }

    // collect entries that only the cache itself keeps alive; anything with more
    // references is in active use by a wallpaper (or the crossfade) and must stay.
    // "$"-prefixed entries are special (album art) and are never evicted
    std::vector<std::map<std::string, CacheEntry>::iterator> candidates;

    for (auto it = this->m_textureCache.begin (); it != this->m_textureCache.end (); ++it) {
	if (it->second.texture.use_count () == 1 && !it->first.starts_with ('$')) {
	    candidates.push_back (it);
	}
    }

    // oldest first
    std::ranges::sort (candidates, [] (const auto& a, const auto& b) {
	return a->second.lastUsed < b->second.lastUsed;
    });

    for (const auto& it : candidates) {
	if (this->m_cacheBytes <= TEXTURE_CACHE_BUDGET_BYTES) {
	    break;
	}

	// ~CTexture releases the GL objects; this runs on the render thread since
	// both resolve() and store() are only called there
	this->m_cacheBytes -= it->second.approximateBytes;
	this->m_textureCache.erase (it);
    }
}
