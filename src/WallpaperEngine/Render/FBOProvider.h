#pragma once

#include <glm/vec2.hpp>

#include <string_view>

#include "CFBO.h"
#include "WallpaperEngine/Data/Model/Effect.h"

namespace WallpaperEngine::Render {
using namespace WallpaperEngine::Data::Model;

class FBOProvider {
public:
    explicit FBOProvider (const FBOProvider* parent);

    std::shared_ptr<CFBO> create (const FBO& base, uint32_t flags, glm::vec2 size);
    std::shared_ptr<CFBO> create (
	const std::string& name, TextureFormat format, uint32_t flags, float scale, glm::vec2 realSize,
	glm::vec2 textureSize, bool withDepthBuffer = false, bool depthTexture = false
    );
    std::shared_ptr<CFBO> alias (const std::string& newName, const std::string& original);
    std::shared_ptr<CFBO> alias (const std::string& newName, std::shared_ptr<CFBO> original);
    [[nodiscard]] std::shared_ptr<CFBO> find (const std::string& name) const;

    /** Set the scene supersampling factor inherited by child effect providers. */
    void setRenderScale (float scale);
    [[nodiscard]] float getRenderScale () const;

    /** Pure sizing helpers kept public so quality scaling can be regression-tested without GL. */
    [[nodiscard]] static bool isFixedSizeTarget (std::string_view name);
    [[nodiscard]] static glm::uvec2 calculateTargetSize (glm::vec2 size, float renderScale, bool scalable = true);

private:
    const FBOProvider* m_parent;
    float m_renderScale = 1.0f;
    std::map<std::string, std::shared_ptr<CFBO>> m_fbos = {};
};
}
