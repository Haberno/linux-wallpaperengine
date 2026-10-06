#pragma once

#include "Helpers/ContextAware.h"
#include "TextureProvider.h"
#include "WallpaperEngine/Data/Assets/Texture.h"
#include "WallpaperEngine/Data/Parsers/TextureParser.h"
#include "WallpaperEngine/VideoPlayback/MPV/GLPlayer.h"

#include <GL/glew.h>
#include <cstddef>
#include <atomic>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <vector>

namespace WallpaperEngine::Render {
class RenderContext;
using namespace WallpaperEngine::Data::Assets;
using namespace WallpaperEngine::VideoPlayback::MPV;
/**
 * A normal texture file in WallpaperEngine's format
 */
class CTexture final : public TextureProvider, public Helpers::ContextAware {
public:
    explicit CTexture (
	RenderContext& context, TextureUniquePtr header, uint32_t textureReduction,
	Data::Parsers::TextureParser::VariantSelector selector = {}
    );
    ~CTexture () override;

    /** CPU-only ownership shared with the existing switch worker; never owns GL resources. */
    struct MipStream {
	TextureUniquePtr texture;
	std::atomic<bool> ready = false;
	std::atomic<bool> cancelled = false;
    };
    static std::shared_ptr<CTexture> createStreaming (
	RenderContext& context, TextureUniquePtr header, uint32_t textureReduction,
	std::vector<std::shared_ptr<MipStream>>& streams
    );
    static void completeMipStreams (const std::vector<std::shared_ptr<MipStream>>& streams);

    /**
     * TEXI records the authored/target texture format, but newer TEXB containers
     * can store DXT-authored images as expanded RGBA8888 pixels. Distinguish the
     * actual payload layout from the authored format before choosing the GL upload.
     */
    [[nodiscard]] static bool isBlockCompressedPayload (
	TextureFormat format, uint32_t width, uint32_t height, size_t byteCount
    );

    [[nodiscard]] GLenum getTextureTarget () const override;
    [[nodiscard]] GLuint getTextureID (uint32_t imageIndex) const override;
    [[nodiscard]] uint32_t getTextureWidth (uint32_t imageIndex) const override;
    [[nodiscard]] uint32_t getTextureHeight (uint32_t imageIndex) const override;
    [[nodiscard]] glm::vec2 getAnimationUVSize (uint32_t imageIndex) const override;
    [[nodiscard]] uint32_t getMipMapCount (uint32_t imageIndex) const override;
    [[nodiscard]] uint32_t getRealWidth () const override;
    [[nodiscard]] uint32_t getRealHeight () const override;
    [[nodiscard]] TextureFormat getFormat () const override;
    [[nodiscard]] uint32_t getFlags () const override;
    [[nodiscard]] const glm::vec4* getResolution () const override;
    [[nodiscard]] const std::vector<FrameSharedPtr>& getFrames () const override;
    [[nodiscard]] bool isAnimated () const override;
    [[nodiscard]] uint32_t getSpritesheetCols () const override;
    [[nodiscard]] uint32_t getSpritesheetRows () const override;
    [[nodiscard]] uint32_t getSpritesheetFrames () const override;
    [[nodiscard]] float getSpritesheetDuration () const override;
    [[nodiscard]] size_t getRetainedCpuBytes () const override;
    [[nodiscard]] size_t getApproximateGpuBytes () const override;

    /**
     * Increments the usage count of the texture
     *
     * Directly controls playback for video CTextures, only started when at least one thing is using it
     * Initializes mpv if needed and starts playback
     */
    void incrementUsageCount () const override;
    /**
     * Decrements the usage count of the texture
     *
     * Directly controls playback for video CTextures, only stopped when nothing is using it
     * De-initializes mpv if needed
     */
    void decrementUsageCount () const override;
    /**
     * Some textures need to be updated
     */
    void update () const override;
    bool isReady () const override;
    /** Parsed metadata, including a packed source when the texture has variants. */
    [[nodiscard]] const Texture& getHeader () const;
    [[nodiscard]] GLPlayer* getVideoPlayer () const { return m_player.get (); }
    /** Selected GIFs share a clock while their scene cache entry is retained. */
    void startUserAnimation (double sceneTime, uint32_t sceneFrame);
    void updateUserAnimation (double sceneTime, uint32_t sceneFrame) const;

private:
    /**
     * @return The texture header
     */

    /**
     * Calculate's texture's resolution vec4
     */
    void setupResolution ();
    /**
     * Determines the texture's internal storage format
     */
    GLint setupInternalFormat () const;
    /**
     * Prepares openGL parameters for loading texture data
     */
    void setupOpenGLParameters (uint32_t textureID) const;

    /** The texture header */
    TextureUniquePtr m_header;
    /** OpenGL's texture ID */
    GLuint* m_textureID = nullptr;
    /** Resolution vector of the texture */
    glm::vec4 m_resolution {};
    /** Half quality omits authored mip zero when a following mip exists. */
    bool m_reduceMipmaps = false;
    /** Scene-selected quality also survives video-to-image variant changes. */
    uint32_t m_textureReduction = 1;
    /** Sum of all uploaded images and mip levels, used by cache accounting. */
    size_t m_approximateGpuBytes = 0;
    /** The video player in use */
    GLPlayerUniquePtr m_player;
    Data::Parsers::TextureParser::VariantSelector m_variantSelector;
    std::vector<uint32_t> m_variantSelection;
    void refreshVariant ();
    void refreshMipStream ();
    std::shared_ptr<MipStream> m_mipStream;
    size_t m_streamCpuBytes = 0;
    bool m_streamingTemporary = false;
    struct UserAnimation {
	uint32_t frame = 0;
	uint32_t update = 0;
	double frameTime = 0.0;
	double duration = 0.0;
    };
    mutable std::optional<UserAnimation> m_userAnimation;
};
} // namespace WallpaperEngine::Assets
