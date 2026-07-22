#pragma once

#include <atomic>
#include <string>

#include "TextureProvider.h"

using namespace WallpaperEngine::Render;

namespace WallpaperEngine::Render {
class CFBO final : public TextureProvider {
public:
    CFBO (
	std::string name, const TextureFormat format, const uint32_t flags, const float scale, uint32_t realWidth,
	uint32_t realHeight, uint32_t textureWidth, uint32_t textureHeight, bool withDepthBuffer = false,
	bool depthTexture = false
    );
    ~CFBO () override;

    [[nodiscard]] const std::string& getName () const;
    [[nodiscard]] const float& getScale () const;
    [[nodiscard]] TextureFormat getFormat () const override;
    [[nodiscard]] uint32_t getFlags () const override;
    [[nodiscard]] GLuint getFramebuffer () const;
    [[nodiscard]] GLuint getDepthbuffer () const;
    /**
     * Regenerates the mip chain from level 0 for layer composite targets (a no-op for
     * every other FBO). These targets get sampled onto grazing 3D geometry in the final
     * pass, so a fresh mip chain lets the anisotropic filter damp minification aliasing.
     */
    void generateMipmaps () const;
    [[nodiscard]] GLuint getTextureID (uint32_t imageIndex) const override;
    [[nodiscard]] uint32_t getTextureWidth (uint32_t imageIndex) const override;
    [[nodiscard]] uint32_t getTextureHeight (uint32_t imageIndex) const override;
    [[nodiscard]] uint32_t getRealWidth () const override;
    [[nodiscard]] uint32_t getRealHeight () const override;
    [[nodiscard]] const std::vector<FrameSharedPtr>& getFrames () const override;
    [[nodiscard]] const glm::vec4* getResolution () const override;
    [[nodiscard]] bool isAnimated () const override;
    [[nodiscard]] size_t getApproximateGpuBytes () const override;
    [[nodiscard]] uint32_t getSpritesheetCols () const override;
    [[nodiscard]] uint32_t getSpritesheetRows () const override;
    [[nodiscard]] uint32_t getSpritesheetFrames () const override;
    [[nodiscard]] float getSpritesheetDuration () const override;

    void incrementUsageCount () const override;
    void decrementUsageCount () const override;
    void update () const override;
    bool isReady () const override;

    [[nodiscard]] static size_t getLiveCount ();
    [[nodiscard]] static size_t getLiveGpuBytes ();
    /** Largest live render-target groups, for the control socket's fbostats command. */
    [[nodiscard]] static std::string getLiveDebugSummary (size_t limit = 20);

private:
    /**
     * Creates the framebuffer object on first use. Framebuffers are container objects
     * and are NOT shared between GL contexts: when a wallpaper is built on the async
     * switch worker's shared context, only the texture/renderbuffer can be created
     * there — the FBO itself must be created lazily on the render thread.
     */
    void ensureFramebuffer () const;

    /** Layer composite targets carry a mip chain; nothing else does. */
    [[nodiscard]] bool hasMipmaps () const;

    mutable GLuint m_framebuffer = GL_NONE;
    GLuint m_depthbuffer = GL_NONE;
    GLuint m_texture = GL_NONE;
    size_t m_approximateGpuBytes = 0;
    /** The main texture is a sampleable depth-comparison attachment, not RGBA color. */
    bool m_depthTexture = false;
    glm::vec4 m_resolution = {};
    float m_scale = 0;
    std::string m_name = "";
    TextureFormat m_format = TextureFormat_UNKNOWN;
    uint32_t m_flags = TextureFlags_NoFlags;
    /** Placeholder for frames, FBOs only have ONE */
    std::vector<FrameSharedPtr> m_frames = {};
    static std::atomic<size_t> s_liveCount;
    static std::atomic<size_t> s_liveGpuBytes;
};
} // namespace WallpaperEngine::Render
