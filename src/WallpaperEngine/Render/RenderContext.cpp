#include <algorithm>
#include <iostream>
#include <random>

#include <GL/glew.h>

#include "CWallpaper.h"
#include "RenderContext.h"

#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"

namespace WallpaperEngine::Render {
RenderContext::RenderContext (
    Drivers::VideoDriver& driver, WallpaperApplication& app, Media::MediaSource& mediaSource
) :
    m_driver (driver), m_app (app), m_mediaSource (mediaSource),
    m_textureCache (std::make_unique<TextureCache> (*this)) { }

void RenderContext::render (Drivers::Output::OutputViewport* viewport) {
    viewport->makeCurrent ();

#if !NDEBUG
    const std::string str = "Rendering to output " + viewport->name;

    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, str.c_str ());
#endif /* DEBUG */

    // search the background in the viewport selection

    // render the background
    if (const auto ref = this->m_wallpapers.find (viewport->name); ref != this->m_wallpapers.end ()) {
        // Shared span owners render their source once per frame. Require all
        // primary consumers to support HDR so decoder mode is independent of
        // which output the driver renders first. Secondary captures stay SDR.
        const auto canPresentHDR = [this] (const std::shared_ptr<CWallpaper>& wallpaper) {
            if (!wallpaper->hasHDRSource ()) return false;
            for (const auto& [name, output] : this->getOutput ().getViewports ()) {
                const auto current = this->m_wallpapers.find (name);
                const auto transition = this->m_transitions.find (name);
                const bool outgoing = transition != this->m_transitions.end ()
                    && transition->second.from == wallpaper
                    && (transition->second.startTime < 0.0f
                        || this->m_driver.getRenderTime () - transition->second.startTime < TRANSITION_DURATION);
                if (((current != this->m_wallpapers.end () && current->second == wallpaper) || outgoing)
                    && !output->supportsScRGB ()) return false;
            }
            return true;
        };
        const auto outgoing = this->m_transitions.find (viewport->name);
        const bool incomingHDR = canPresentHDR (ref->second);
        const bool outgoingActive = outgoing != this->m_transitions.end ()
            && (outgoing->second.startTime < 0.0f
                || this->m_driver.getRenderTime () - outgoing->second.startTime < TRANSITION_DURATION);
        const bool outgoingHDR = outgoingActive && canPresentHDR (outgoing->second.from);
        const bool scRGB = viewport->supportsScRGB () && (incomingHDR || outgoingHDR);
        // Both sides of a transition must encode the same physical destination.
        const float hdrHeadroom = viewport->getHDRHeadroom ();
        viewport->setScRGBPresentation (scRGB);
        ref->second->prepareHDRPresentation (incomingHDR);
        if (outgoing != this->m_transitions.end ()) outgoing->second.from->prepareHDRPresentation (outgoingHDR);
	// transition: draw the outgoing wallpaper first, then reveal the new one on top
	if (const auto transition = this->m_transitions.find (viewport->name);
	    transition != this->m_transitions.end ()) {
	    // start the clock on the first rendered frame, not when the wallpaper was
	    // installed - loading the scene blocks the render loop long enough that the
	    // animation window would otherwise expire before a single frame is drawn
	    if (transition->second.startTime < 0.0f) {
		transition->second.startTime = this->m_driver.getRenderTime ();
	    }

	    const float elapsed = this->m_driver.getRenderTime () - transition->second.startTime;
	    const float progress = elapsed / TRANSITION_DURATION;

	    if (progress >= 1.0f) {
		this->m_transitions.erase (transition);
	    } else {
		transition->second.from->setFpsOverlay (-1.0f);
		transition->second.from->setLoadOverlay ({ -1.0f }, false);
		transition->second.from->renderWithColorSpace (
		    viewport->viewport, this->getOutput ().renderVFlip (), viewport->globalPosition,
		    viewport->logicalSize, scRGB, hdrHeadroom
		);
		ref->second->setTransition (transition->second.mode, progress, transition->second.center);
	    }
	}

        float fps = -1.0f;
        if (this->m_app.getContext ().settings.render.showFps) {
            auto& counter = this->m_frameRates[viewport->name];
            const double now = std::chrono::duration<double> (
                std::chrono::steady_clock::now ().time_since_epoch ()
            ).count ();
            counter.frame (now);
            fps = counter.fps ();
        }
        ref->second->setFpsOverlay (fps);
	const auto& settings = this->m_app.getContext ().settings.render;
	const auto load = this->m_loadTimes.find (viewport->name);
	ref->second->setLoadOverlay (
	    (settings.showFps || settings.showLoadDetails) && load != this->m_loadTimes.end () ? load->second
												: LoadTimes { -1.0f },
	    settings.showLoadDetails
	);
	ref->second->renderWithColorSpace (
	    viewport->viewport, this->getOutput ().renderVFlip (), viewport->globalPosition, viewport->logicalSize, scRGB, hdrHeadroom
	);
	ref->second->setTransition (TransitionMode_None, 1.0f);
    }

#if !NDEBUG
    glPopDebugGroup ();
#endif /* DEBUG */

    viewport->swapOutput ();
}

void RenderContext::setWallpaper (
    const std::string& display, std::shared_ptr<CWallpaper> wallpaper, std::shared_ptr<void> keepAlive,
    const TransitionMode transition
) {
    wallpaper->setDestinationFramebuffer (this->m_app.getDestinationFramebuffer ());

    // keep the previous wallpaper (and its backing data) alive and animate into the new one,
    // but only when the caller provided the keep-alive - without it the outgoing wallpaper's
    // Project may already be destroyed and rendering it would use dangling references
    if (keepAlive != nullptr && transition != TransitionMode_None) {
	if (const auto previous = this->m_wallpapers.find (display); previous != this->m_wallpapers.end ()) {
	    // point-based transitions open from a random spot on screen each switch
	    glm::vec2 center { 0.5f, 0.5f };

	    if (transition == TransitionMode_Disc || transition == TransitionMode_Iris
		|| transition == TransitionMode_InkSplash) {
		static std::mt19937 generator { std::random_device {} () };
		std::uniform_real_distribution<float> position (0.1f, 0.9f);
		center = { position (generator), position (generator) };
	    }

	    // Destroy the old renderer before its Project even for a rapid second switch.
	    // Member-wise assignment would replace keepAlive before releasing from.
	    this->m_transitions.erase (display);
	    // startTime is stamped on the first rendered frame (see render above)
	    this->m_transitions.emplace (
		display,
		Transition {
		    .keepAlive = std::move (keepAlive),
		    .from = previous->second,
		    .mode = transition,
		    .startTime = -1.0f,
		    .center = center,
		}
	    );
	}
    } else {
	this->m_transitions.erase (display);
    }

    this->m_wallpapers.insert_or_assign (display, wallpaper);
}

void RenderContext::setPause (const bool newState) const {
    for (const auto& wallpaper : this->m_wallpapers | std::views::values) {
	wallpaper->setPause (newState);
    }
    for (const auto& transition : this->m_transitions | std::views::values) {
	transition.from->setPause (newState);
    }
}

Input::InputContext& RenderContext::getInputContext () const { return this->m_driver.getInputContext (); }

const WallpaperApplication& RenderContext::getApp () const { return this->m_app; }

const Drivers::VideoDriver& RenderContext::getDriver () const { return this->m_driver; }

const Drivers::Output::Output& RenderContext::getOutput () const { return this->m_driver.getOutput (); }

glm::ivec2 RenderContext::getStableOutputSize () const {
    if (!this->m_stableOutputSize.has_value ()) {
	this->m_stableOutputSize
	    = glm::ivec2 (this->getOutput ().getFullWidth (), this->getOutput ().getFullHeight ());
    }

    return this->m_stableOutputSize.value ();
}

glm::ivec2 RenderContext::getSceneOutputSize (const std::string& screenName) const {
    const auto& output = this->getOutput ();
    if (const auto viewport = output.getViewports ().find (screenName); viewport != output.getViewports ().end ()) {
	return { viewport->second->viewport.z, viewport->second->viewport.w };
    }
    return { output.getFullWidth (), output.getFullHeight () };
}

std::optional<RenderContext::SpanGeometry> RenderContext::getSpanGeometry (
    const std::vector<std::string>& screens
) const {
    std::optional<SpanGeometry> result;
    for (const auto& screen : screens) {
	const auto found = this->getOutput ().getViewports ().find (screen);
	if (found == this->getOutput ().getViewports ().end ()) continue;
	const auto& viewport = *found->second;
	const glm::ivec2 start = viewport.globalPosition;
	const glm::ivec2 end = start + viewport.logicalSize;
	if (!result) result = SpanGeometry { {start.x, start.y, end.x, end.y}, 1.0f };
	else {
	    result->bounds.x = std::min (result->bounds.x, start.x);
	    result->bounds.y = std::min (result->bounds.y, start.y);
	    result->bounds.z = std::max (result->bounds.z, end.x);
	    result->bounds.w = std::max (result->bounds.w, end.y);
	}
	result->pixelScale = std::max ({ result->pixelScale,
	    float (viewport.viewport.z) / std::max (viewport.logicalSize.x, 1),
	    float (viewport.viewport.w) / std::max (viewport.logicalSize.y, 1) });
    }
    if (result) {
	result->bounds.z -= result->bounds.x;
	result->bounds.w -= result->bounds.y;
    }
    return result;
}

glm::ivec2 RenderContext::getTextureQualityOutputSize (const std::string& screenName) const {
    for (const auto& group : this->getApp ().getContext ().settings.general.spanGroups) {
	if (group.screens.empty () || screenName != "span:" + group.screens.front ()) continue;
	if (const auto geometry = this->getSpanGeometry (group.screens)) {
	    return glm::ivec2 (glm::round (glm::vec2 (geometry->bounds.z, geometry->bounds.w) * geometry->pixelScale));
	}
    }
    return this->getSceneOutputSize (screenName);
}

uint32_t RenderContext::getTextureReduction (const Data::Model::Wallpaper& wallpaper, glm::ivec2 outputSize) const {
    const auto quality = this->getApp ().getContext ().settings.render.textureResolution;
    if (!wallpaper.is<Data::Model::Scene> ()) {
	return quality == Application::ApplicationContext::TextureResolution::Half ? 2 : 1;
    }
    const auto& projection = wallpaper.as<Data::Model::Scene> ()->camera.projection;
    const glm::ivec2 authoredSize = projection.isAuto ? glm::ivec2 (0)
	: glm::ivec2 (projection.width, projection.height);
    const bool perspective = projection.isPerspective
	|| (!projection.isAuto && (authoredSize.x == 0 || authoredSize.y == 0));
    return Application::ApplicationContext::calculateTextureReduction (
	quality, perspective, authoredSize, outputSize
    );
}

std::shared_ptr<const TextureProvider> RenderContext::resolveTexture (const std::string& name) const {
    return this->m_textureCache->resolve (name);
}

std::shared_ptr<const TextureProvider>
RenderContext::resolveTexture (const std::string& name, const Assets::AssetLocator& assetLocator, uint32_t textureReduction) const {
    return this->m_textureCache->resolve (name, assetLocator, textureReduction);
}

std::shared_ptr<const TextureProvider> RenderContext::resolveTexture (
    const std::string& name, const Assets::AssetLocator& assetLocator, const Data::Model::Properties& properties,
    uint32_t textureReduction
) const {
    return this->m_textureCache->resolve (name, assetLocator, properties, textureReduction);
}

std::vector<std::shared_ptr<const TextureProvider>> RenderContext::prepareTextureFallbacks (
    const std::set<std::string>& names, const Assets::AssetLocator& locator,
    const Data::Model::Properties& properties, const uint32_t textureReduction
) const {
    auto prepared = m_textureCache->prepareFallbacks (names, locator, properties, textureReduction);
    m_app.queueTextureStreams (std::move (prepared.streams));
    return std::move (prepared.textures);
}

void RenderContext::storeTexture (const std::string& name, std::shared_ptr<const TextureProvider> texture) const {
    this->m_textureCache->store (name, std::move (texture));
}

void RenderContext::storeTexture (
    const std::string& name, const Assets::AssetLocator& assetLocator,
    std::shared_ptr<const TextureProvider> texture, uint32_t textureReduction
) const {
    this->m_textureCache->store (name, assetLocator, std::move (texture), textureReduction);
}

void RenderContext::updateAllTextures () const { this->m_textureCache->updateAll (); }

const std::map<std::string, std::shared_ptr<CWallpaper>>& RenderContext::getWallpapers () const {
    return this->m_wallpapers;
}

TextureCacheStats RenderContext::getTextureCacheStats () const { return this->m_textureCache->getStats (); }

Media::MediaSource& RenderContext::getMediaSource () const { return this->m_mediaSource; }

} // namespace WallpaperEngine::Render
