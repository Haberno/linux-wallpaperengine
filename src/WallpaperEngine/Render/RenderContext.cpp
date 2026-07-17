#include <iostream>
#include <random>

#include <GL/glew.h>

#include "CWallpaper.h"
#include "RenderContext.h"

#include "WallpaperEngine/Data/Model/Project.h"

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
		transition->second.from->render (
		    viewport->viewport, this->getOutput ().renderVFlip (), viewport->globalPosition,
		    viewport->logicalSize, false
		);
		ref->second->setTransition (transition->second.mode, progress, transition->second.center);
	    }
	}

	ref->second->render (
	    viewport->viewport, this->getOutput ().renderVFlip (), viewport->globalPosition, viewport->logicalSize
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

	    // startTime is stamped on the first rendered frame (see render above)
	    this->m_transitions.insert_or_assign (
		display,
		Transition {
		    .from = previous->second,
		    .keepAlive = std::move (keepAlive),
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

std::shared_ptr<const TextureProvider> RenderContext::resolveTexture (const std::string& name) const {
    return this->m_textureCache->resolve (name);
}

std::shared_ptr<const TextureProvider>
RenderContext::resolveTexture (const std::string& name, const Assets::AssetLocator& assetLocator) const {
    return this->m_textureCache->resolve (name, assetLocator);
}

void RenderContext::storeTexture (const std::string& name, std::shared_ptr<const TextureProvider> texture) const {
    this->m_textureCache->store (name, std::move (texture));
}

void RenderContext::storeTexture (
    const std::string& name, const Assets::AssetLocator& assetLocator,
    std::shared_ptr<const TextureProvider> texture
) const {
    this->m_textureCache->store (name, assetLocator, std::move (texture));
}

void RenderContext::updateAllTextures () const { this->m_textureCache->updateAll (); }

const std::map<std::string, std::shared_ptr<CWallpaper>>& RenderContext::getWallpapers () const {
    return this->m_wallpapers;
}

Media::MediaSource& RenderContext::getMediaSource () const { return this->m_mediaSource; }

} // namespace WallpaperEngine::Render
