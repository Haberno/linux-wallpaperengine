#include "FBOProvider.h"
#include <algorithm>
#include <cmath>
#include <gmpxx.h>
#include <utility>

using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Data::Model;

FBOProvider::FBOProvider (const FBOProvider* parent) :
    m_parent (parent), m_renderScale (parent != nullptr ? parent->m_renderScale : 1.0f) { }

bool FBOProvider::isFixedSizeTarget (const std::string_view name) {
    // These sizes are part of the renderer protocol rather than scene resolution.
    // Shadow viewport transforms are expressed against SHADOW_ATLAS_SIZE, and a
    // light cookie is deliberately a single neutral texel.
    return name == "_rt_shadowAtlas" || name == "_alias_lightCookie";
}

glm::uvec2 FBOProvider::calculateTargetSize (
    const glm::vec2 size, const float renderScale, const bool scalable, const int fit
) {
    const float scale = scalable ? std::clamp (renderScale, 0.5f, 2.0f) : 1.0f;
    glm::uvec2 result {
	static_cast<uint32_t> (std::max (1.0f, std::round (size.x * scale))),
	static_cast<uint32_t> (std::max (1.0f, std::round (size.y * scale))),
    };
    // wallpaper64.exe caps the longer edge without upscaling small sources,
    // then truncates the proportional shorter edge. Apply after scene scaling:
    // a fixed-budget simulation must stay capped under supersampling too.
    const auto longest = std::max (result.x, result.y);
    if (fit > 0 && static_cast<uint32_t> (fit) < longest) {
	result = glm::max (glm::uvec2 (glm::vec2 (result) * (static_cast<float> (fit) / longest)), glm::uvec2 (1));
    }
    return result;
}

void FBOProvider::setRenderScale (const float scale) { this->m_renderScale = std::clamp (scale, 0.5f, 2.0f); }

float FBOProvider::getRenderScale () const { return this->m_renderScale; }

std::shared_ptr<CFBO> FBOProvider::create (const FBO& base, uint32_t flags, const glm::vec2 size) {
    const auto targetSize = calculateTargetSize (
	size / base.scale, this->m_renderScale, !isFixedSizeTarget (base.name), base.fit
    );
    return this->m_fbos[base.name] = std::make_shared<CFBO> (
	       base.name,
	       (base.format == "rgba16161616f" || base.format == "rgba16f")
		   ? TextureFormat_RGBA16161616f : TextureFormat_ARGB8888, flags, base.scale, targetSize.x, targetSize.y, targetSize.x, targetSize.y
	   );
}

std::shared_ptr<CFBO> FBOProvider::create (
    const std::string& name, TextureFormat format, uint32_t flags, float scale, glm::vec2 realSize,
    glm::vec2 textureSize, bool withDepthBuffer, bool depthTexture, uint32_t samples
) {
    const bool scalable = !isFixedSizeTarget (name);
    const auto scaledRealSize = calculateTargetSize (realSize, this->m_renderScale, scalable);
    const auto scaledTextureSize = calculateTargetSize (textureSize, this->m_renderScale, scalable);
    return this->m_fbos[name] = std::make_shared<CFBO> (
	       name, format, flags, scale, scaledRealSize.x, scaledRealSize.y, scaledTextureSize.x,
	       scaledTextureSize.y, withDepthBuffer, depthTexture, samples
	   );
}

std::shared_ptr<CFBO> FBOProvider::alias (const std::string& newName, const std::string& original) {
    return this->m_fbos[newName] = this->m_fbos[original];
}

std::shared_ptr<CFBO> FBOProvider::alias (const std::string& newName, std::shared_ptr<CFBO> original) {
    return this->m_fbos[newName] = std::move (original);
}

std::shared_ptr<CFBO> FBOProvider::find (const std::string& name) const {
    if (const auto it = this->m_fbos.find (name); it != this->m_fbos.end ()) {
	return it->second;
    }

    if (this->m_parent == nullptr) {
	return nullptr;
    }

    return this->m_parent->find (name);
}
