#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

#include <lz4.h>
#include <stb_image.h>

#include "TextureParser.h"
#include "WallpaperEngine/Data/Assets/Texture.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Data::Assets;
using namespace WallpaperEngine::Data::Parsers;

void WallpaperEngine::Data::Assets::freeDecodedPixels (void* pixels) {
    stbi_image_free (pixels);
}

namespace {
void decodeMipmap (Mipmap& mipmap) {
    int width = 0, height = 0, fileChannels = 0;
    stbi_uc* pixels = stbi_load_from_memory (
	reinterpret_cast<unsigned char*> (mipmap.uncompressedData.get ()), mipmap.uncompressedSize, &width, &height,
	&fileChannels, 4
    );

    // on failure leave the mipmap untouched, the render thread will retry and report
    if (pixels == nullptr) {
	return;
    }

    mipmap.decodedData.reset (pixels);
    mipmap.decodedWidth = width;
    mipmap.decodedHeight = height;
}

/** Every mipmap of the texture that still has to be decoded, appended to out */
void collectDecodable (Texture& texture, std::vector<Mipmap*>& out) {
    // videos are fed to the player as-is and non-FIF formats upload their raw data
    // directly, so only image-format textures have anything to decode
    if (texture.isVideoMp4 || texture.flags & TextureFlags_Video || texture.freeImageFormat == FIF_UNKNOWN) {
	return;
    }

    for (auto& [index, mipmaps] : texture.images) {
	for (const auto& mipmap : mipmaps) {
	    if (mipmap->decodedData == nullptr && mipmap->uncompressedData != nullptr) {
		out.emplace_back (mipmap.get ());
	    }
	}
    }
}
} // namespace

void TextureParser::decodeMipmaps (Texture& texture) {
    std::vector<Mipmap*> pending;
    collectDecodable (texture, pending);

    for (Mipmap* mipmap : pending) {
	decodeMipmap (*mipmap);
    }
}

void TextureParser::decodeMipmaps (const std::vector<Texture*>& textures) {
    std::vector<Mipmap*> pending;
    for (Texture* texture : textures) {
	collectDecodable (*texture, pending);
    }

    if (pending.empty ()) {
	return;
    }

    std::atomic<size_t> next = 0;
    const auto worker = [&next, &pending] {
	for (size_t index = next++; index < pending.size (); index = next++) {
	    decodeMipmap (*pending[index]);
	}
    };

    const auto threads =
	std::min<size_t> (pending.size (), std::max (1u, std::thread::hardware_concurrency ()));
    std::vector<std::jthread> pool;
    pool.reserve (threads - 1);

    for (size_t thread = 1; thread < threads; thread++) {
	pool.emplace_back (worker);
    }

    // this thread takes work too instead of waiting on the pool
    worker ();
}

TextureUniquePtr TextureParser::parse (const BinaryReader& file) {
    auto result = std::make_unique<Texture> ();

    parseTextureHeader (*result, file);
    parseContainer (*result, file);

    // a rejected mipmap leaves the read position mid-payload, so nothing after it can be trusted
    bool truncated = false;

    for (uint32_t image = 0; image < result->imageCount && !truncated; image++) {
	const uint32_t mipmapCount = file.nextUInt32 ();
	MipmapList mipmaps;

	for (uint32_t mipmap = 0; mipmap < mipmapCount; mipmap++) {
	    auto parsed = parseMipmap (file, *result);

	    if (parsed == nullptr) {
		truncated = true;
		break;
	    }

	    mipmaps.emplace_back (std::move (parsed));
	}

	if (mipmaps.empty ()) {
	    sLog.exception ("Cannot parse texture, the first mipmap's header is not valid");
	}

	result->images.emplace (image, mipmaps);
    }

    if (truncated || !result->isAnimated ()) {
	return result;
    }

    parseAnimations (*result, file);

    return result;
}

MipmapSharedPtr TextureParser::parseMipmap (const BinaryReader& file, const Texture& header) {
    auto result = std::make_shared<Mipmap> ();
    const bool isVideo = (header.flags & TextureFlags_Video) != 0;

    // Still-image TEXB0004 mipmaps prefix the ordinary dimensions with editor metadata.
    // Video TEXB0004 mipmaps do not: their width starts immediately after the mip count,
    // followed by height/compression/sizes and the embedded MP4 bytes.
    if (header.containerVersion == ContainerVersion_TEXB0004 && !isVideo) {
	// some integers that we can ignore as they only seem to affect
	// the editor
	std::ignore = file.nextUInt32 ();
	std::ignore = file.nextUInt32 ();
	// this format includes some json in the header that we might need
	// to parse at some point...
	result->json = file.nextNullTerminatedString ();
	// last ignorable integer
	std::ignore = file.nextUInt32 ();
    }

    result->width = file.nextUInt32 ();
    result->height = file.nextUInt32 ();

    if (header.containerVersion == ContainerVersion_TEXB0004 || header.containerVersion == ContainerVersion_TEXB0003
	|| header.containerVersion == ContainerVersion_TEXB0002) {
	result->compression = file.nextUInt32 ();
	result->uncompressedSize = file.nextInt ();
    }

    result->compressedSize = file.nextInt ();

    // Packed video textures carry a raw MP4 payload. Its preceding uncompressed-size
    // field is zero, so the following compressed-size field is the authoritative byte
    // count even though the payload itself is not compressed.
    if (isVideo) {
	if (result->compressedSize <= 0) {
	    return nullptr;
	}

	result->uncompressedSize = result->compressedSize;
	result->compression = 0;
	result->uncompressedData = std::make_unique<char[]> (result->uncompressedSize);
	file.next (result->uncompressedData.get (), result->uncompressedSize);
	return result;
    }

    if (result->compression == 0) {
	// this might be better named as mipmap_bytes_size instead of compressedSize
	// as in uncompressed files this variable actually holds the file length
	result->uncompressedSize = result->compressedSize;
    }

    // TEXB0004 containers with a variant table (conditionalImages > 1) interleave the alternate
    // images' payloads between the base image's mipmap levels, so the read position after mip0 is
    // variant data, not the next header. that layout is not decoded yet, and the garbage header
    // used to reach LZ4 and abort the whole wallpaper. report the level as unusable instead; the
    // caller keeps the levels parsed so far and CTexture sizes GL_TEXTURE_MAX_LEVEL off that count,
    // so the texture stays mipmap-complete
    // ponytail: costs the smaller mipmaps on those textures, drop it once the variant header is
    // reverse-engineered (2924081598, 3766299002)
    if (result->width == 0 || result->height == 0 || result->compression > 1 || result->compressedSize <= 0
	|| result->uncompressedSize <= 0) {
	return nullptr;
    }

    result->uncompressedData = std::unique_ptr<char[]> (new char[result->uncompressedSize]);

    if (result->compression == 1) {
	result->compressedData = std::unique_ptr<char[]> (new char[result->compressedSize]);
	// read the compressed data into the buffer
	file.next (result->compressedData.get (), result->compressedSize);
	// finally decompress it
	int bytes = LZ4_decompress_safe (
	    result->compressedData.get (), result->uncompressedData.get (), result->compressedSize,
	    result->uncompressedSize
	);

	if (bytes < 0) {
	    sLog.exception ("Cannot decompress texture data, LZ4_decompress_safe returned an error");
	}

	// nothing reads the compressed copy again, and the texture cache holds mipmaps for the
	// lifetime of the process, so keeping it would retain the whole packed payload for nothing
	result->compressedData.reset ();
    } else {
	file.next (result->uncompressedData.get (), result->uncompressedSize);
    }

    return result;
}

FrameSharedPtr TextureParser::parseFrameV1 (const BinaryReader& file) {
    auto result = std::make_shared<Frame> ();

    result->frameNumber = file.nextUInt32 ();
    result->frametime = file.nextFloat ();
    result->x = static_cast<float> (file.nextUInt32 ());
    result->y = static_cast<float> (file.nextUInt32 ());
    result->width1 = static_cast<float> (file.nextUInt32 ());
    std::ignore = file.nextUInt32 (); // unknown
    std::ignore = file.nextUInt32 (); // unknown
    result->height1 = static_cast<float> (file.nextUInt32 ());

    return result;
}

FrameSharedPtr TextureParser::parseFrame (const BinaryReader& file) {
    auto result = std::make_shared<Frame> ();

    result->frameNumber = file.nextUInt32 ();
    result->frametime = file.nextFloat ();
    result->x = file.nextFloat ();
    result->y = file.nextFloat ();
    result->width1 = file.nextFloat ();
    result->width2 = file.nextFloat ();
    result->height2 = file.nextFloat ();
    result->height1 = file.nextFloat ();

    return result;
}

TextureMap TextureParser::parseTextureMap (const JSON& it) {
    if (!it.is_array ()) {
	return {};
    }

    TextureMap result = {};
    int textureIndex = -1;

    for (const auto& cur : it) {
	textureIndex++;

	if (cur.is_null ()) {
	    continue;
	}

	if (cur.is_object ()) {
	    const auto nameIt = cur.find ("name");
	    if (nameIt != cur.end () && nameIt->is_string ()) {
		result.emplace (textureIndex, nameIt->get<std::string> ());
	    }
	} else if (cur.is_string ()) {
	    std::string texName = cur;
	    if (!texName.empty ()) {
		result.emplace (textureIndex, texName);
	    }
	}
    }

    return result;
}

TextureFormat TextureParser::parseTextureFormat (uint32_t value) {
    switch (value) {
	case TextureFormat_UNKNOWN:
	case TextureFormat_ARGB8888:
	case TextureFormat_RGB888:
	case TextureFormat_RGB565:
	case TextureFormat_DXT5:
	case TextureFormat_DXT3:
	case TextureFormat_DXT1:
	case TextureFormat_RG88:
	case TextureFormat_R8:
	case TextureFormat_RG1616f:
	case TextureFormat_R16f:
	case TextureFormat_BC7:
	case TextureFormat_RGBa1010102:
	case TextureFormat_RGBA16161616f:
	case TextureFormat_RGB161616f:
	    return static_cast<TextureFormat> (value);

	default:
	    sLog.exception ("unknown texture format: ", value);
    }
}

void TextureParser::parseTextureHeader (Texture& header, const BinaryReader& file) {
    char magic[9] = { 0 };

    file.next (magic, 9);

    if (strncmp (magic, "TEXV0005", 9) != 0) {
	sLog.exception ("unexpected texture container type: ", std::string_view (magic, 9));
    }

    file.next (magic, 9);

    if (strncmp (magic, "TEXI0001", 9) != 0) {
	sLog.exception ("unexpected texture sub-container type: ", std::string_view (magic, 9));
    }

    header.format = parseTextureFormat (file.nextUInt32 ());
    header.flags = parseTextureFlags (file.nextUInt32 ());
    header.textureWidth = file.nextUInt32 ();
    header.textureHeight = file.nextUInt32 ();
    header.width = file.nextUInt32 ();
    header.height = file.nextUInt32 ();

    // ignore some more bytes
    std::ignore = file.nextUInt32 ();
}

void TextureParser::parseContainer (Texture& header, const BinaryReader& file) {
    char magic[9] = { 0 };

    file.next (magic, 9);

    header.imageCount = file.nextUInt32 ();

    if (strncmp (magic, "TEXB0004", 9) == 0) {
	header.containerVersion = ContainerVersion_TEXB0004;
	header.freeImageFormat = parseFIF (file.nextUInt32 ());
	// number of conditional image variants stored ahead of the base image; each one is an
	// alternate picked by a user property (3737268876 ships Link's tunic as three
	// 'tuniccolor' alternates on top of the default green base image)
	const uint32_t conditionalImages = file.nextUInt32 ();
	// mp4 containers report a single entry too, so only trust that when the header also
	// flags video; parseMipmap needs TextureFlags_Video to read the payload anyway
	header.isVideoMp4 = conditionalImages == 1 && (header.flags & TextureFlags_Video) != 0;

	if (header.freeImageFormat == FIF_UNKNOWN && header.isVideoMp4) {
	    header.freeImageFormat = FIF_MP4;
	}

	// default to TEXB0003 format here
	if (header.freeImageFormat != FIF_MP4) {
	    header.containerVersion = ContainerVersion_TEXB0003;

	    // the variant table sits between the container header and the base image, which uses
	    // the plain TEXB0003 mipmap layout. Skip it so the base image parses; that is the
	    // variant the property falls back to and the only one the engine can pick today.
	    for (uint32_t variant = 0; variant < conditionalImages; variant++) {
		std::ignore = file.nextUInt32 ();
		std::ignore = file.nextUInt32 ();
		std::ignore = file.nextUInt32 ();
		std::ignore = file.nextNullTerminatedString ();
	    }
	}
    } else if (strncmp (magic, "TEXB0003", 9) == 0) {
	header.containerVersion = ContainerVersion_TEXB0003;
	header.freeImageFormat = parseFIF (file.nextUInt32 ());
    } else if (strncmp (magic, "TEXB0002", 9) == 0) {
	header.containerVersion = ContainerVersion_TEXB0002;
    } else if (strncmp (magic, "TEXB0001", 9) == 0) {
	header.containerVersion = ContainerVersion_TEXB0001;
    } else {
	sLog.exception ("unknown texture format type: ", std::string_view (magic, 9));
    }
}

void TextureParser::parseAnimations (Texture& header, const BinaryReader& file) {
    char magic[9] = { 0 };

    // image is animated, keep parsing the rest of the image info
    file.next (magic, 9);

    if (strncmp (magic, "TEXS0001", 9) == 0) {
	header.animatedVersion = AnimatedVersion_TEXS0001;
    } else if (strncmp (magic, "TEXS0002", 9) == 0) {
	header.animatedVersion = AnimatedVersion_TEXS0002;
    } else if (strncmp (magic, "TEXS0003", 9) == 0) {
	header.animatedVersion = AnimatedVersion_TEXS0003;
    } else {
	sLog.exception ("found animation information of unknown type: ", std::string_view (magic, 9));
    }

    uint32_t frameCount = file.nextUInt32 ();

    if (header.animatedVersion == AnimatedVersion_TEXS0003) {
	header.gifWidth = file.nextUInt32 ();
	header.gifHeight = file.nextUInt32 ();
    }

    while (frameCount-- > 0) {
	if (header.animatedVersion == AnimatedVersion_TEXS0001) {
	    header.frames.push_back (parseFrameV1 (file));
	} else {
	    header.frames.push_back (parseFrame (file));
	}
    }

    // ensure gif width and height is right for TEXS0001, TEXS0002
    if (header.animatedVersion == AnimatedVersion_TEXS0001 || header.animatedVersion == AnimatedVersion_TEXS0002) {
	header.gifWidth = (*header.frames.begin ())->width1;
	header.gifHeight = (*header.frames.begin ())->height1;
    }

    // Calculate spritesheet grid dimensions from animation frames
    // Spritesheets are grid-based textures where each frame is at a specific position
    if (!header.frames.empty () && header.width > 0 && header.height > 0) {
	auto& firstFrame = *header.frames.front ();
	float frameWidth = firstFrame.width1;
	float frameHeight = firstFrame.height1;

	if (frameWidth > 0.0f && frameHeight > 0.0f) {
	    const uint32_t cols = static_cast<uint32_t> (std::round (static_cast<double> (header.width) / frameWidth));
	    const uint32_t rows
		= static_cast<uint32_t> (std::round (static_cast<double> (header.height) / frameHeight));
	    const uint32_t frameCount = static_cast<uint32_t> (header.frames.size ());

	    // Only populate spritesheet metadata if the inferred grid can actually hold all frames
	    // This prevents GIFs (where frameWidth == textureWidth) from being treated as 1×1 spritesheets
	    if (cols > 0 && rows > 0 && cols * rows >= frameCount) {
		header.spritesheetCols = cols;
		header.spritesheetRows = rows;
		header.spritesheetFrames = frameCount;

		float totalDuration = 0.0f;
		for (const auto& frame : header.frames) {
		    totalDuration += frame->frametime;
		}
		header.spritesheetDuration = totalDuration;
	    }
	}
    }
}

uint32_t TextureParser::parseTextureFlags (uint32_t value) {
    // Texture flags are a bitmask, not a bounded enum. Newer Wallpaper Engine
    // assets set metadata bits that this renderer does not consume (for example
    // the 0x200000/0x800000 bits used by several model material masks). Preserve
    // every bit so the known sampling flags still work instead of rejecting an
    // otherwise valid texture.
    return value;
}

FIF TextureParser::parseFIF (uint32_t value) {
    switch (value) {
	case FIF_UNKNOWN:
	case FIF_BMP:
	case FIF_ICO:
	case FIF_JPEG:
	case FIF_JNG:
	case FIF_KOALA:
	case FIF_LBM:
	case FIF_MNG:
	case FIF_PBM:
	case FIF_PBMRAW:
	case FIF_PCD:
	case FIF_PCX:
	case FIF_PGM:
	case FIF_PGMRAW:
	case FIF_PNG:
	case FIF_PPM:
	case FIF_PPMRAW:
	case FIF_RAS:
	case FIF_TARGA:
	case FIF_TIFF:
	case FIF_WBMP:
	case FIF_PSD:
	case FIF_CUT:
	case FIF_XBM:
	case FIF_XPM:
	case FIF_DDS:
	case FIF_GIF:
	case FIF_HDR:
	case FIF_FAXG3:
	case FIF_SGI:
	case FIF_EXR:
	case FIF_J2K:
	case FIF_JP2:
	case FIF_PFM:
	case FIF_PICT:
	case FIF_RAW:
	case FIF_WEBP:
	case FIF_JXR:
	    return static_cast<FIF> (value);

	default:
	    sLog.exception ("unknown free image format: ", value);
    }
}

TextureUniquePtr TextureParser::parse (
    const BinaryReader& file, const std::string& filename,
    std::function<std::string (const std::string&)> metadataLoader
) {
    // Parse the binary .tex file first
    auto result = parse (file);

    // Try to load optional .tex-json metadata for spritesheet data
    if (metadataLoader) {
	parseSpritesheetMetadata (*result, filename, metadataLoader);
    }

    return result;
}

void TextureParser::parseSpritesheetMetadata (
    Texture& header, const std::string& filename, std::function<std::string (const std::string&)> metadataLoader
) {
    try {
	std::string texJsonContent = metadataLoader (filename + ".tex-json");
	JSON texJson = WallpaperEngine::Data::JSON::parseCompatible (texJsonContent, filename + ".tex-json");

	// Check for spritesheet sequences
	if (texJson.contains ("spritesheetsequences") && texJson["spritesheetsequences"].is_array ()) {
	    auto& sequences = texJson["spritesheetsequences"];
	    if (!sequences.empty ()) {
		auto& firstSeq = sequences[0];
		int frames = firstSeq.value ("frames", 0);
		float frameWidth = firstSeq.value ("width", 0.0f);
		float frameHeight = firstSeq.value ("height", 0.0f);
		float duration = firstSeq.value ("duration", 1.0f);

		if (frames > 0 && frameWidth > 0.0f && frameHeight > 0.0f && header.width > 0 && header.height > 0) {
		    // Calculate grid dimensions from texture size and frame size
		    header.spritesheetCols = static_cast<uint32_t> (std::round (header.width / frameWidth));
		    header.spritesheetRows = static_cast<uint32_t> (std::round (header.height / frameHeight));
		    header.spritesheetFrames = static_cast<uint32_t> (frames);
		    header.spritesheetDuration = duration;
		}
	    }
	}
    } catch (const std::exception&) {
	// .tex-json file is optional, only used for spritesheet data
    }
}
