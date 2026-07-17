#include "CTexture.h"
#include "WallpaperEngine/Logging/Log.h"

#include <algorithm>
#include <lz4.h>

#define STB_IMAGE_IMPLEMENTATION
#include "RenderContext.h"

#include <stb_image.h>

using namespace WallpaperEngine::Render;

CTexture::CTexture (RenderContext& context, TextureUniquePtr header) :
    Helpers::ContextAware (context), m_header (std::move (header)) {
    // ensure the header is parsed
    this->setupResolution ();
    const GLint internalFormat = this->setupInternalFormat ();

    // videos are a bit special, they only have one framebuffer, one mipmap
    if (this->m_header->isVideoMp4 || this->m_header->flags & TextureFlags_Video) {
	if (this->m_header->images.empty () || this->m_header->images.begin ()->second.empty ()) {
	    sLog.exception ("Cannot load video texture, no mipmaps found");
	}

	// generate the texture and set it up to be used by the player
	this->m_textureID = new GLuint[1];
	glGenTextures (1, this->m_textureID);
	this->setupOpenGLParameters (0);

	const auto mipmap = *this->m_header->images.begin ()->second.begin ();

	this->m_player = std::make_unique<GLPlayer> (
	    this->getContext (), this->m_textureID[0],
	    std::make_unique<MemoryStreamProtocol> (mipmap->uncompressedData.get (), mipmap->uncompressedSize),
	    this->m_header->textureWidth, this->m_header->textureHeight
	);
	// setup texture video player
	this->m_player->setMuted ();
	this->m_player->setVolume (0.0f);
	this->m_player->setUntimed ();
	// texture is ready, nothing else to do
	return;
    }

    // allocate texture ids list
    this->m_textureID = new GLuint[this->m_header->imageCount];
    // ask opengl for the correct amount of textures and framebuffers
    glGenTextures (this->m_header->imageCount, this->m_textureID);

    for (const auto& [index, mipmaps] : this->m_header->images) {
	this->setupOpenGLParameters (index);

	int level = 0;

	for (const auto& mipmap : mipmaps) {
	    stbi_uc* handle = nullptr;
	    const void* dataptr = mipmap->uncompressedData.get ();
	    int width = mipmap->width;
	    int height = mipmap->height;
	    const uint32_t bufferSize = mipmap->uncompressedSize;
	    GLenum textureFormat = GL_RGBA;

	    if (this->m_header->freeImageFormat != FIF_UNKNOWN) {
		if (mipmap->decodedData != nullptr) {
		    // pixels were pre-decoded off the render thread (TextureParser::decodeMipmaps),
		    // so the expensive stbi work is skipped entirely
		    dataptr = mipmap->decodedData.get ();
		    width = mipmap->decodedWidth;
		    height = mipmap->decodedHeight;
		} else {
		    int fileChannels;

		    dataptr = handle = stbi_load_from_memory (
			reinterpret_cast<unsigned char*> (mipmap->uncompressedData.get ()), mipmap->uncompressedSize,
			&width, &height, &fileChannels, 4
		    );
		}
	    } else {
		if (this->m_header->format == TextureFormat_R8) {
		    // red textures are 1-byte-per-pixel, so it's alignment has to be set manually
		    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
		    textureFormat = GL_RED;
		} else if (this->m_header->format == TextureFormat_RG88) {
		    textureFormat = GL_RG;
		}
	    }

	    switch (internalFormat) {
		case GL_RGBA8:
		case GL_RG8:
		case GL_R8:
		    glTexImage2D (
			GL_TEXTURE_2D, level, internalFormat, width, height, 0, textureFormat, GL_UNSIGNED_BYTE, dataptr
		    );
		    break;
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
		    glCompressedTexImage2D (
			GL_TEXTURE_2D, level, internalFormat, width, height, 0, bufferSize, dataptr
		    );
		    break;
		default:
		    sLog.exception ("Cannot load texture, unknown format", this->m_header->format);
	    }

	    // stbi_image buffer won't be used anymore, so free memory
	    if (this->m_header->freeImageFormat != FIF_UNKNOWN) {
		stbi_image_free (handle);
		// pre-decoded pixels are on the GPU now, don't keep them in RAM
		mipmap->decodedData.reset ();
		mipmap->decodedWidth = 0;
		mipmap->decodedHeight = 0;
	    }

	    // the raw file bytes are on the GPU now too; only the video path (which
	    // returns early above) keeps reading them after construction, so cached
	    // textures don't need to pin the CPU-side copy anymore
	    mipmap->uncompressedData.reset ();
	    mipmap->uncompressedSize = 0;

	    level++;
	}
    }
}

CTexture::~CTexture () {
    // first release the player to prevent using null references
    this->m_player.reset ();

    if (this->m_header->isVideoMp4 || this->m_header->flags & TextureFlags_Video) {
	glDeleteTextures (1, this->m_textureID);
    } else {
	glDeleteTextures (this->m_header->imageCount, this->m_textureID);
    }

    delete[] this->m_textureID;
}

bool CTexture::isBlockCompressedPayload (
    const TextureFormat format, const uint32_t width, const uint32_t height, const size_t byteCount
) {
    size_t bytesPerBlock = 0;

    switch (format) {
	case TextureFormat_DXT1: bytesPerBlock = 8; break;
	case TextureFormat_DXT3:
	case TextureFormat_DXT5:
	case TextureFormat_BC7: bytesPerBlock = 16; break;
	default: return false;
    }

    const size_t blocksWide = std::max<size_t> (1, (static_cast<size_t> (width) + 3) / 4);
    const size_t blocksHigh = std::max<size_t> (1, (static_cast<size_t> (height) + 3) / 4);
    return byteCount == blocksWide * blocksHigh * bytesPerBlock;
}

void CTexture::setupResolution () {
    if (this->isAnimated ()) {
	this->m_resolution = { this->m_header->textureWidth, this->m_header->textureHeight, this->m_header->gifWidth,
			       this->m_header->gifHeight };
    } else {
	if (this->m_header->freeImageFormat != FIF_UNKNOWN) {
	    // wpengine-texture format always has one mipmap
	    // get first image size
	    const auto element = this->m_header->images.find (0)->second.begin ();

	    // set the texture resolution
	    this->m_resolution
		= { (*element)->width, (*element)->height, this->m_header->width, this->m_header->height };
	} else {
	    // set the texture resolution
	    this->m_resolution = { this->m_header->textureWidth, this->m_header->textureHeight, this->m_header->width,
				   this->m_header->height };
	}
    }
}

GLint CTexture::setupInternalFormat () const {
    if (this->m_header->freeImageFormat != FIF_UNKNOWN) {
	return GL_RGBA8;
    }

    const auto firstImage = this->m_header->images.find (0);
    if (firstImage == this->m_header->images.end () || firstImage->second.empty ()) {
	sLog.exception ("Cannot determine texture format without mipmap data");
    }

    const auto& firstMipmap = *firstImage->second.front ();
    const bool blockCompressed = isBlockCompressedPayload (
	this->m_header->format, firstMipmap.width, firstMipmap.height,
	static_cast<size_t> (std::max (firstMipmap.uncompressedSize, 0))
    );

    const bool dxtAuthored = this->m_header->format == TextureFormat_DXT1
	|| this->m_header->format == TextureFormat_DXT3 || this->m_header->format == TextureFormat_DXT5;
    const size_t rawRgbaBytes
	= static_cast<size_t> (firstMipmap.width) * static_cast<size_t> (firstMipmap.height) * 4;
    if (dxtAuthored && !blockCompressed
	&& static_cast<size_t> (std::max (firstMipmap.uncompressedSize, 0)) != rawRgbaBytes) {
	sLog.exception (
	    "Unsupported DXT texture payload size: ", firstMipmap.uncompressedSize,
	    " for ", firstMipmap.width, "x", firstMipmap.height
	);
    }

    // detect the image format and hand it to openGL to be used
    switch (this->m_header->format) {
	case TextureFormat_DXT5:
	    return blockCompressed ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_RGBA8;
	case TextureFormat_DXT3:
	    return blockCompressed ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT : GL_RGBA8;
	case TextureFormat_DXT1:
	    return blockCompressed ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_RGBA8;
	case TextureFormat_ARGB8888:
	    return GL_RGBA8;
	case TextureFormat_R8:
	    return GL_R8;
	case TextureFormat_RG88:
	    return GL_RG8;
	default:
	    sLog.exception ("Cannot determine texture format");
    }
}

void CTexture::setupOpenGLParameters (const uint32_t textureID) const {
    // TODO: LABEL ELEMENTS TOO
    // bind the texture to assign information to it
    glBindTexture (GL_TEXTURE_2D, this->m_textureID[textureID]);

    // set mipmap levels
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, this->m_header->images[textureID].size () - 1);

    // setup texture wrapping and filtering
    if (this->m_header->flags & TextureFlags_ClampUVs) {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    if (this->m_header->flags & TextureFlags_NoInterpolation) {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    } else {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }

    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
}

GLuint CTexture::getTextureID (const uint32_t imageIndex) const {
    // ensure we do not go out of bounds
    if (imageIndex >= this->m_header->imageCount) {
	return this->m_textureID[0];
    }

    return this->m_textureID[imageIndex];
}

uint32_t CTexture::getTextureWidth (const uint32_t imageIndex) const {
    if (imageIndex >= this->m_header->imageCount) {
	return this->getHeader ().textureWidth;
    }

    return (*this->m_header->images[imageIndex].begin ())->width;
}

uint32_t CTexture::getTextureHeight (const uint32_t imageIndex) const {
    if (imageIndex >= this->m_header->imageCount) {
	return this->getHeader ().textureHeight;
    }

    return (*this->m_header->images[imageIndex].begin ())->height;
}

uint32_t CTexture::getRealWidth () const {
    return this->isAnimated () ? this->getHeader ().gifWidth : this->getHeader ().width;
}

uint32_t CTexture::getRealHeight () const {
    return this->isAnimated () ? this->getHeader ().gifHeight : this->getHeader ().height;
}

TextureFormat CTexture::getFormat () const { return this->getHeader ().format; }

uint32_t CTexture::getFlags () const { return this->getHeader ().flags; }

const Texture& CTexture::getHeader () const { return *this->m_header; }

const glm::vec4* CTexture::getResolution () const { return &this->m_resolution; }

const std::vector<FrameSharedPtr>& CTexture::getFrames () const { return this->getHeader ().frames; }

bool CTexture::isAnimated () const { return this->getHeader ().isAnimated (); }

uint32_t CTexture::getSpritesheetCols () const { return this->getHeader ().spritesheetCols; }

uint32_t CTexture::getSpritesheetRows () const { return this->getHeader ().spritesheetRows; }

uint32_t CTexture::getSpritesheetFrames () const { return this->getHeader ().spritesheetFrames; }

float CTexture::getSpritesheetDuration () const { return this->getHeader ().spritesheetDuration; }

size_t CTexture::getRetainedCpuBytes () const {
    // image textures free their CPU-side buffers after the GL upload in the constructor,
    // but the video path keeps the whole file pinned so mpv can stream from memory
    size_t bytes = 0;

    for (const auto& [index, mipmaps] : this->m_header->images) {
	for (const auto& mipmap : mipmaps) {
	    bytes += mipmap->uncompressedSize;
	}
    }

    return bytes;
}

void CTexture::incrementUsageCount () const {
    if (this->m_player) {
	this->m_player->incrementUsageCount ();
    }
}

void CTexture::decrementUsageCount () const {
    if (this->m_player) {
	this->m_player->decrementUsageCount ();
    }
}

void CTexture::update () const {
    if (this->m_player) {
	this->m_player->render ();
    }
}

// CTextures are always ready to be rendered at all times
bool CTexture::isReady () const { return true; }
