#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <vector>

#include "TextureCache.h"
#include "WallpaperEngine/Application/WallpaperApplication.h"
#include "WallpaperEngine/Input/InputContext.h"
#include "WallpaperEngine/Input/MouseInput.h"
#include "WallpaperEngine/Render/Drivers/Output/Output.h"
#include "WallpaperEngine/Render/Drivers/Output/OutputViewport.h"
#include "WallpaperEngine/Render/Drivers/VideoDriver.h"
#include "WallpaperEngine/Render/TransitionMode.h"

namespace WallpaperEngine {
namespace Application {
    class WallpaperApplication;
}
namespace Media {
    class MediaSource;
}

namespace Render {
    namespace Drivers {
	class VideoDriver;

	namespace Output {
	    class Output;
	    class OutputViewport;
	} // namespace Output
    } // namespace Drivers

    class CWallpaper;
    class TextureCache;

    class RenderContext {
    public:
	RenderContext (Drivers::VideoDriver& driver, WallpaperApplication& app, Media::MediaSource& mediaSource);

	void render (Drivers::Output::OutputViewport* viewport);
	/**
	 * @param keepAlive resources the outgoing wallpaper still references (e.g. its
	 *		    Project); held until the transition finishes. Without it the
	 *		    wallpaper is swapped instantly.
	 * @param transition how to reveal the new wallpaper over the old one
	 */
	void setWallpaper (
	    const std::string& display, std::shared_ptr<CWallpaper> wallpaper,
	    std::shared_ptr<void> keepAlive = nullptr,
	    TransitionMode transition = TransitionMode_Fade
	);
	void setPause (bool newState) const;
	[[nodiscard]] Input::InputContext& getInputContext () const;
	[[nodiscard]] const WallpaperApplication& getApp () const;
	[[nodiscard]] const Drivers::VideoDriver& getDriver () const;
	[[nodiscard]] const Drivers::Output::Output& getOutput () const;
	/**
	 * Output size captured the first time it is requested. Scenes with an automatic orthogonal
	 * projection and no measurable objects fall back to this instead of the live output size so an
	 * in-process wallpaper rebuild (live property change, control-socket bg swap) sizes its
	 * framebuffers exactly like the original build — the live size can differ between those moments
	 * (surface configure timing, window re-tiling) and a scene rebuilt at a different size than its
	 * first build breaks the effect-composite chain.
	 */
	[[nodiscard]] glm::ivec2 getStableOutputSize () const;
	[[nodiscard]] std::shared_ptr<const TextureProvider> resolveTexture (const std::string& name) const;
	[[nodiscard]] std::shared_ptr<const TextureProvider>
	resolveTexture (const std::string& name, const Assets::AssetLocator& assetLocator) const;
	/**
	 * Registers a ready texture under the given name so resolveTexture returns it instead
	 * of loading the file synchronously. Used by asynchronous background switches to
	 * pre-stage textures that were parsed off the render thread.
	 */
	void storeTexture (const std::string& name, std::shared_ptr<const TextureProvider> texture) const;
	void storeTexture (
	    const std::string& name, const Assets::AssetLocator& assetLocator,
	    std::shared_ptr<const TextureProvider> texture
	) const;
	/**
	 * Ticks every texture held by the cache, see TextureCache::updateAll
	 */
	void updateAllTextures () const;
	[[nodiscard]] const std::map<std::string, std::shared_ptr<CWallpaper>>& getWallpapers () const;
	[[nodiscard]] Media::MediaSource& getMediaSource () const;

    private:
	/** Crossfade between the previous and the current wallpaper of a screen */
	struct Transition {
	    std::shared_ptr<CWallpaper> from;
	    /** Keeps the outgoing wallpaper's backing data (Project) alive during the animation */
	    std::shared_ptr<void> keepAlive;
	    TransitionMode mode;
	    float startTime;
	    /** Normalized origin for point-based transitions (e.g. disc), randomized per switch */
	    glm::vec2 center;
	};

	/** How long a wallpaper switch crossfade lasts, in seconds */
	static constexpr float TRANSITION_DURATION = 1.0f;

	/** Video driver in use */
	Drivers::VideoDriver& m_driver;
	/** Maps screen -> wallpaper list */
	std::map<std::string, std::shared_ptr<CWallpaper>> m_wallpapers = {};
	/** Screens with a crossfade in progress, keeping the outgoing wallpaper alive */
	std::map<std::string, Transition> m_transitions = {};
	/** App that holds the render context */
	WallpaperApplication& m_app;
	/** Source for the media playback information */
	Media::MediaSource& m_mediaSource;
	/** Texture cache for the render */
	std::unique_ptr<TextureCache> m_textureCache = nullptr;
	/** First-requested output size, see getStableOutputSize */
	mutable std::optional<glm::ivec2> m_stableOutputSize = std::nullopt;
    };
} // namespace Render
} // namespace WallpaperEngine
