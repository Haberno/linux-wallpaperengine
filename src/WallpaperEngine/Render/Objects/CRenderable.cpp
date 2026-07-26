#include "CRenderable.h"

#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Parsers/MaterialParser.h"

#include <algorithm>
#include <cmath>

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render::Objects;
using namespace WallpaperEngine::Render::Objects::Effects;
using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Builders;

CRenderable::CRenderable (Wallpapers::CScene& scene, const Object& object, const Material& material) :
    CObject (scene, object), Render::FBOProvider (&scene), m_material (material) { }

void CRenderable::detectTexture () {
    if (TextureMap* textures = &(*this->m_material.passes.begin ())->textures; !textures->empty ()) {
	std::string textureName = textures->begin ()->second;

	if (textureName.find ("_rt_") == 0 || textureName.find ("_alias_") == 0) {
	    this->m_texture = this->getScene ().findFBO (textureName);
	} else {
	    this->m_texture = this->getContext ().resolveTexture (textureName, this->getScene ().getAssetLocator ());
	}
    }
}

void CRenderable::setup () {
    CObject::setup ();

    // calculate full animation time (if any)
    this->m_animationTime = 0.0f;

    for (const auto& cur : this->getTexture ()->getFrames ()) {
	this->m_animationTime += cur->frametime;
    }

    this->m_textureAnimationPlayback = {};
    this->m_textureAnimationPlayback.anchorRenderTime = this->getTextureAnimationRenderTime ();
}

std::shared_ptr<const TextureProvider> CRenderable::getTexture () const { return this->m_texture; }

double CRenderable::getAnimationTime () const { return this->m_animationTime; }

bool CRenderable::hasTextureAnimation () const {
    return this->m_texture != nullptr && this->m_texture->isAnimated () && !this->m_texture->getFrames ().empty ()
	&& this->m_animationTime > 0.0;
}

size_t CRenderable::getTextureAnimationFrameCount () const {
    return this->hasTextureAnimation () ? this->m_texture->getFrames ().size () : 0;
}

double CRenderable::getTextureAnimationDuration () const { return this->m_animationTime; }

float CRenderable::getTextureAnimationRate () const { return this->m_textureAnimationPlayback.rate; }

float CRenderable::getTextureAnimationRenderTime () const { return this->getContext ().getDriver ().getRenderTime (); }

namespace {
double wrapAnimationTime (const double time, const double duration) {
    if (duration <= 0.0) {
	return 0.0;
    }

    const double wrapped = std::fmod (time, duration);
    return wrapped < 0.0 ? wrapped + duration : wrapped;
}
}

double CRenderable::getControlledTextureAnimationTime () const {
    if (!this->hasTextureAnimation ()) {
	return 0.0;
    }

    const auto& playback = this->m_textureAnimationPlayback;
    const double now = static_cast<double> (this->getTextureAnimationRenderTime ());
    if (playback.joined) {
	return wrapAnimationTime (now * playback.rate, this->m_animationTime);
    }

    const double elapsed = playback.playing ? now - playback.anchorRenderTime : 0.0;
    return wrapAnimationTime (playback.anchorTime + elapsed * playback.rate, this->m_animationTime);
}

void CRenderable::detachTextureAnimationClock () {
    this->m_textureAnimationPlayback.anchorTime = this->getControlledTextureAnimationTime ();
    this->m_textureAnimationPlayback.anchorRenderTime = this->getTextureAnimationRenderTime ();
    this->m_textureAnimationPlayback.joined = false;
}

void CRenderable::setTextureAnimationRate (const float rate) {
    if (!std::isfinite (rate)) {
	return;
    }

    this->detachTextureAnimationClock ();
    this->m_textureAnimationPlayback.rate = rate;
}

void CRenderable::playTextureAnimation () {
    if (!this->hasTextureAnimation () || this->m_textureAnimationPlayback.playing) {
	return;
    }

    this->m_textureAnimationPlayback.anchorRenderTime = this->getTextureAnimationRenderTime ();
    this->m_textureAnimationPlayback.joined = false;
    this->m_textureAnimationPlayback.playing = true;
}

void CRenderable::pauseTextureAnimation () {
    if (!this->hasTextureAnimation () || !this->isTextureAnimationPlaying ()) {
	return;
    }

    this->detachTextureAnimationClock ();
    this->m_textureAnimationPlayback.playing = false;
}

void CRenderable::stopTextureAnimation () {
    if (!this->hasTextureAnimation ()) {
	return;
    }

    this->m_textureAnimationPlayback.anchorTime = 0.0;
    this->m_textureAnimationPlayback.anchorRenderTime = this->getTextureAnimationRenderTime ();
    this->m_textureAnimationPlayback.joined = false;
    this->m_textureAnimationPlayback.playing = false;
}

bool CRenderable::isTextureAnimationPlaying () const {
    return this->hasTextureAnimation ()
	&& (this->m_textureAnimationPlayback.joined || this->m_textureAnimationPlayback.playing);
}

size_t CRenderable::getTextureAnimationFrame () const {
    if (!this->hasTextureAnimation ()) {
	return 0;
    }

    double remaining = this->getControlledTextureAnimationTime ();
    const auto& frames = this->m_texture->getFrames ();
    for (size_t index = 0; index < frames.size (); index++) {
	remaining -= frames[index]->frametime;
	if (remaining <= 0.0) {
	    return index;
	}
    }
    return frames.size () - 1;
}

void CRenderable::setTextureAnimationFrame (const size_t requestedFrame) {
    if (!this->hasTextureAnimation ()) {
	return;
    }

    const auto& frames = this->m_texture->getFrames ();
    const size_t frame = std::min (requestedFrame, frames.size () - 1);
    double frameTime = 0.0;
    for (size_t index = 0; index < frame; index++) {
	frameTime += frames[index]->frametime;
    }

    this->m_textureAnimationPlayback.anchorTime = frameTime;
    this->m_textureAnimationPlayback.anchorRenderTime = this->getTextureAnimationRenderTime ();
    this->m_textureAnimationPlayback.joined = false;
}

void CRenderable::joinTextureAnimation () {
    if (!this->hasTextureAnimation ()) {
	return;
    }

    this->m_textureAnimationPlayback.joined = true;
    this->m_textureAnimationPlayback.playing = true;
}

double CRenderable::getTextureAnimationTime (const std::shared_ptr<const TextureProvider>& texture) const {
    if (texture == nullptr || !texture->isAnimated ()) {
	return 0.0;
    }

    if (texture.get () == this->m_texture.get ()) {
	return this->getControlledTextureAnimationTime ();
    }

    double duration = 0.0;
    for (const auto& frame : texture->getFrames ()) {
	duration += frame->frametime;
    }
    return wrapAnimationTime (this->getTextureAnimationRenderTime (), duration);
}
