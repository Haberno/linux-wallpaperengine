#include "WallpaperEngine/Assets/AssetLoadException.h"
#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>
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
size_t remainingBytes (const BinaryReader& file) {
    auto& input = file.base ();
    const auto position = input.tellg ();
    input.seekg (0, std::ios::end);
    const auto end = input.tellg ();
    input.seekg (static_cast<std::streamoff> (position), std::ios::beg);
    return static_cast<size_t> (end - position);
}

void checkPayloadSize (const BinaryReader& file, int stored, int unpacked) {
    if (stored <= 0 || unpacked <= 0 || unpacked > (1 << 30) || static_cast<size_t> (stored) > remainingBytes (file)) {
	throw std::runtime_error ("Invalid or truncated texture payload");
    }
}

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

TextureUniquePtr TextureParser::parse (const BinaryReader& file) { return parse (file, VariantSelector {}); }

TextureUniquePtr TextureParser::parse (const BinaryReader& file, const VariantSelector& selector) {
    // BinaryReader is also used by older parsers. Require complete reads here so a
    // truncated patch cannot turn an unchecked integer/string read into an allocation.
    auto& input = file.base ();
    const auto exceptions = input.exceptions ();
    input.exceptions (std::ios::failbit | std::ios::badbit);
    const ScopeGuard restoreExceptions ([&] { input.exceptions (exceptions); });
    auto result = std::make_unique<Texture> ();

    parseTextureHeader (*result, file);
    const auto selected = selectedVariants (*result, selector);
    if (result->imageCount == 0 || result->imageCount > 16384) {
	throw std::runtime_error ("Invalid texture image count");
    }

    // a rejected mipmap leaves the read position mid-payload, so nothing after it can be trusted
    bool truncated = false;

    for (uint32_t image = 0; image < result->imageCount && !truncated; image++) {
	const uint32_t mipmapCount = file.nextUInt32 ();
	if (mipmapCount == 0 || mipmapCount > 32) {
	    throw std::runtime_error ("Invalid texture mip count");
	}
	MipmapList mipmaps;

	for (uint32_t mipmap = 0; mipmap < mipmapCount; mipmap++) {
	    auto parsed = parseMipmap (file, *result);

	    if (parsed == nullptr) {
		truncated = true;
		break;
	    }

	    if (!result->variants.empty ()) {
		parseVariantPatches (*parsed, *result, file, selected);
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
	checkPayloadSize (file, result->compressedSize, result->uncompressedSize);
	result->uncompressedData = std::make_unique<char[]> (result->uncompressedSize);
	file.next (result->uncompressedData.get (), result->uncompressedSize);
	return result;
    }

    if (result->compression == 0) {
	// this might be better named as mipmap_bytes_size instead of compressedSize
	// as in uncompressed files this variable actually holds the file length
	result->uncompressedSize = result->compressedSize;
    }

    if (result->width == 0 || result->height == 0 || result->compression > 1 || result->compressedSize <= 0
	|| result->uncompressedSize <= 0) {
	return nullptr;
    }

    checkPayloadSize (file, result->compressedSize, result->uncompressedSize);
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

	if (bytes != result->uncompressedSize) {
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

    const bool legacy = strncmp (magic, "TEXV0004", 9) == 0;
    if (!legacy && strncmp (magic, "TEXV0005", 9) != 0) {
	sLog.exception ("unexpected texture container type: ", std::string_view (magic, 9));
    }

    if (!legacy) {
	file.next (magic, 9);

	if (strncmp (magic, "TEXI0001", 9) != 0) {
	    sLog.exception ("unexpected texture sub-container type: ", std::string_view (magic, 9));
	}
    }

    header.format = parseTextureFormat (file.nextUInt32 ());
    header.flags = parseTextureFlags (file.nextUInt32 ());
    header.textureWidth = file.nextUInt32 ();
    header.textureHeight = file.nextUInt32 ();
    header.width = file.nextUInt32 ();
    header.height = file.nextUInt32 ();

    if (legacy) {
	// TEXV0004 has no TEXI/TEXB sections or image count. One image's mip
	// count follows these six fields, using the same raw records as TEXB0001.
	header.containerVersion = ContainerVersion_TEXB0001;
	header.imageCount = 1;
	return;
    }

    // ignore some more bytes
    std::ignore = file.nextUInt32 ();
    parseContainer (header, file);
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

	if (!(header.flags & TextureFlags_Video)) {
	    if (conditionalImages > 16384) {
		throw std::runtime_error ("Invalid texture variant count");
	    }
	    for (uint32_t variant = 0; variant < conditionalImages; variant++) {
		TextureVariant entry;
		entry.group = file.nextUInt32 ();
		entry.id = file.nextUInt32 ();
		entry.flags = file.nextUInt32 ();
		const auto metadata = JSON::parse (file.nextNullTerminatedString ());
		entry.condition = metadata.value ("condition", JSON ());
		header.variants.emplace_back (std::move (entry));
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

bool TextureParser::matchesCondition (const JSON& condition, const Properties& properties) {
    const std::string name = condition.is_string () ? condition.get<std::string> ()
	: condition.is_object ()                    ? condition.value ("name", std::string {})
						    : std::string {};
    const auto property = properties.find (name);
    if (property == properties.end ()) {
	return false;
    }
    if (condition.is_string ()) {
	return property->second->getBool ();
    }
    const auto expected = condition.find ("condition");
    return expected != condition.end () && expected->is_string ()
	&& property->second->toString () == expected->get<std::string> ();
}

std::vector<uint32_t> TextureParser::selectedVariants (const Texture& texture, const VariantSelector& selector) {
    std::set<uint32_t> groups;
    std::vector<uint32_t> selected;
    if (selector) {
	for (const auto& variant : texture.variants) {
	    // Native TEXB4 selects the first matching condition in each group.
	    if (!groups.contains (variant.group) && selector (variant.condition)) {
		groups.insert (variant.group);
		selected.push_back (variant.id);
	    }
	}
    }
    return selected;
}

void TextureParser::parseVariantPatches (
    Mipmap& mipmap, const Texture& header, const BinaryReader& file, const std::vector<uint32_t>& selected
) {
    // TEXB4: base mip payload, group count, then a patch count per group.
    // Each patch has version/id/x/y/width/height/FIF/byte count, followed by bytes.
    // LZ4 compression is inherited from the base mip, not encoded in the patch.
    const auto groups = file.nextUInt32 ();
    if (groups > 16384) {
	throw std::runtime_error ("Invalid texture patch group count");
    }
    for (uint32_t group = 0; group < groups; ++group) {
	const auto count = file.nextUInt32 ();
	if (count > remainingBytes (file) / 32) {
	    throw std::runtime_error ("Truncated texture patch table");
	}
	for (uint32_t index = 0; index < count; ++index) {
	    std::ignore = file.nextUInt32 (); // record version (1 in authored assets)
	    const auto id = file.nextUInt32 ();
	    const auto x = file.nextUInt32 ();
	    const auto y = file.nextUInt32 ();
	    const auto width = file.nextUInt32 ();
	    const auto height = file.nextUInt32 ();
	    std::ignore = file.nextUInt32 (); // patch FIF; stbi identifies image bytes itself
	    const auto size = file.nextInt ();
	    if (size < 0 || static_cast<size_t> (size) > remainingBytes (file)) {
		throw std::runtime_error ("Truncated texture variant payload");
	    }
	    if (std::ranges::find (selected, id) == selected.end () || size == 0) {
		file.base ().seekg (size, std::ios::cur);
		continue;
	    }
	    const auto variant = std::ranges::find (header.variants, id, &TextureVariant::id);
	    if (variant == header.variants.end ()) {
		throw std::runtime_error ("Unknown texture variant id");
	    }
	    if (width == 0 || height == 0 || x > mipmap.width || width > mipmap.width - x || y > mipmap.height
		|| height > mipmap.height - y) {
		throw std::runtime_error ("Texture variant patch lies outside the mipmap");
	    }
	    std::string payload (size, '\0');
	    file.next (payload.data (), payload.size ());
	    if (variant->flags & 2) {
		// A replacement payload discards the base and earlier patches.
		mipmap.decodedData.reset ();
		mipmap.uncompressedSize = size;
		mipmap.uncompressedData = std::make_unique<char[]> (size);
		std::memcpy (mipmap.uncompressedData.get (), payload.data (), size);
		continue;
	    }

	    const bool imageFormat = header.freeImageFormat != FIF_UNKNOWN;
	    const bool blocks = !imageFormat
		&& (header.format == TextureFormat_DXT1 || header.format == TextureFormat_DXT3
		    || header.format == TextureFormat_DXT5 || header.format == TextureFormat_BC7)
		&& static_cast<size_t> (mipmap.uncompressedSize)
		    != static_cast<size_t> (mipmap.width) * mipmap.height * 4;
	    const size_t unit = blocks                ? (header.format == TextureFormat_DXT1 ? 8 : 16)
		: imageFormat                         ? 4
		: header.format == TextureFormat_R8   ? 1
		: header.format == TextureFormat_RG88 ? 2
						      : 4;
	    const size_t columns = blocks ? (width + 3) / 4 : width;
	    const size_t rows = blocks ? (height + 3) / 4 : height;
	    const size_t stride = (blocks ? (mipmap.width + 3) / 4 : mipmap.width) * unit;
	    const size_t patchSize = columns * rows * unit;
	    if (patchSize > (1 << 30) || (blocks && (x % 4 != 0 || y % 4 != 0))) {
		throw std::runtime_error ("Invalid texture variant block layout");
	    }
	    std::vector<char> unpacked;
	    DecodedPixelsPtr decoded;
	    const char* source = payload.data ();
	    size_t sourceSize = payload.size ();
	    if (mipmap.compression == 1) {
		unpacked.resize (patchSize);
		const int bytes
		    = LZ4_decompress_safe (payload.data (), unpacked.data (), size, static_cast<int> (patchSize));
		if (bytes != static_cast<int> (patchSize)) {
		    throw std::runtime_error ("Cannot decompress texture variant data");
		}
		source = unpacked.data ();
		sourceSize = unpacked.size ();
	    } else if (imageFormat) {
		int decodedWidth = 0, decodedHeight = 0, channels = 0;
		decoded.reset (stbi_load_from_memory (
		    reinterpret_cast<const unsigned char*> (source), size, &decodedWidth, &decodedHeight, &channels, 4
		));
		if (!decoded || decodedWidth != static_cast<int> (width)
		    || decodedHeight != static_cast<int> (height)) {
		    throw std::runtime_error ("Invalid texture variant image");
		}
		source = reinterpret_cast<const char*> (decoded.get ());
		sourceSize = patchSize;
	    }
	    char* target = mipmap.uncompressedData.get ();
	    size_t targetSize = mipmap.uncompressedSize;
	    if (imageFormat) {
		if (!mipmap.decodedData) {
		    decodeMipmap (mipmap);
		}
		if (!mipmap.decodedData || mipmap.decodedWidth != static_cast<int> (mipmap.width)
		    || mipmap.decodedHeight != static_cast<int> (mipmap.height)) {
		    throw std::runtime_error ("Invalid base image for texture variant");
		}
		target = reinterpret_cast<char*> (mipmap.decodedData.get ());
		targetSize = static_cast<size_t> (mipmap.width) * mipmap.height * 4;
	    }
	    const size_t offset = (blocks ? y / 4 : y) * stride + (blocks ? x / 4 : x) * unit;
	    if (sourceSize != patchSize || offset + (rows - 1) * stride + columns * unit > targetSize) {
		throw std::runtime_error ("Texture variant payload size does not match its rectangle");
	    }
	    for (size_t row = 0; row < rows; ++row) {
		auto* destination = reinterpret_cast<unsigned char*> (target + offset + row * stride);
		const auto* pixels = reinterpret_cast<const unsigned char*> (source + row * columns * unit);
		if (variant->flags & 1) {
		    if (unit != 4 || blocks) {
			throw std::runtime_error ("Texture variant blending requires RGBA pixels");
		    }
		    for (size_t pixel = 0; pixel < columns; ++pixel) {
			const int alpha = pixels[pixel * 4 + 3];
			for (size_t channel = 0; channel < 3; ++channel) {
			    const size_t at = pixel * 4 + channel;
			    destination[at] = destination[at] + ((pixels[at] - destination[at]) * alpha >> 8);
			}
			destination[pixel * 4 + 3] = std::max (destination[pixel * 4 + 3], pixels[pixel * 4 + 3]);
		    }
		} else {
		    std::memcpy (destination, pixels, columns * unit);
		}
	    }
	}
    }
}

TextureUniquePtr TextureParser::selectVariants (const Texture& texture, const VariantSelector& selector) {
    if (!texture.variantSource) {
	throw std::runtime_error ("Texture has no retained variant source");
    }
    const auto input = std::make_shared<std::istringstream> (*texture.variantSource, std::ios::binary);
    auto result = parse (BinaryReader (input), selector);
    result->variantSource = texture.variantSource;
    result->spritesheetCols = texture.spritesheetCols;
    result->spritesheetRows = texture.spritesheetRows;
    result->spritesheetFrames = texture.spritesheetFrames;
    result->spritesheetDuration = texture.spritesheetDuration;
    decodeMipmaps (*result);
    return result;
}

TextureUniquePtr
TextureParser::load (const WallpaperEngine::Assets::AssetLocator& locator, const std::string& filename) {
    const auto metadataLoader = [&locator] (const std::string& name) {
	return locator.readString (std::filesystem::path ("materials") / name);
    };
    ReadStreamSharedPtr compiled;
    try {
	compiled = locator.texture (filename);
    } catch (const WallpaperEngine::Assets::AssetLoadException&) {
	// An editor source needs its import settings. Do not reinterpret an arbitrary
	// PNG (or a corrupt .tex) as a compiled texture merely because it exists.
	const auto metadata = WallpaperEngine::Data::JSON::parseCompatible (
	    metadataLoader (filename + ".tex-json"), filename + ".tex-json"
	);
	const auto contents = locator.readString (std::filesystem::path ("materials") / (filename + ".png"));
	auto result = std::make_unique<Texture> ();
	auto mipmap = std::make_shared<Mipmap> ();
	int width = 0, height = 0, channels = 0;
	DecodedPixelsPtr pixels (stbi_load_from_memory (
	    reinterpret_cast<const unsigned char*> (contents.data ()), static_cast<int> (contents.size ()), &width,
	    &height, &channels, 4
	));
	if (!pixels || width <= 0 || height <= 0) {
	    throw std::runtime_error ("Cannot decode source texture " + filename);
	}
	result->format = TextureFormat_ARGB8888;
	const auto format = metadata.value ("format", std::string ("rgba8888"));
	size_t components = 4;
	if (format == "rg88" || format == "rg88n") {
	    result->format = TextureFormat_RG88;
	    components = 2;
	} else if (format == "r8") {
	    result->format = TextureFormat_R8;
	    components = 1;
	} else if (format != "rgba8888") {
	    throw std::runtime_error ("Unsupported source texture format " + format);
	}
	result->width = result->textureWidth = mipmap->width = width;
	result->height = result->textureHeight = mipmap->height = height;
	result->imageCount = 1;
	if (metadata.value ("nointerpolation", false)) {
	    result->flags |= TextureFlags_NoInterpolation;
	}
	if (metadata.value ("clampuvs", false)) {
	    result->flags |= TextureFlags_ClampUVs;
	}
	mipmap->uncompressedSize = static_cast<int> (static_cast<size_t> (width) * height * components);
	mipmap->uncompressedData = std::make_unique<char[]> (mipmap->uncompressedSize);
	for (size_t pixel = 0; pixel < static_cast<size_t> (width) * height; ++pixel) {
	    std::memcpy (mipmap->uncompressedData.get () + pixel * components, pixels.get () + pixel * 4, components);
	}
	result->images[0].push_back (mipmap);
	// The shipped sources request nomip. For other sources, create the chain
	// on the CPU so the ordinary uploader sees the same complete mip layout.
	if (!metadata.value ("nomip", false)) {
	    while (mipmap->width > 1 || mipmap->height > 1) {
		auto next = std::make_shared<Mipmap> ();
		next->width = std::max (1u, mipmap->width / 2);
		next->height = std::max (1u, mipmap->height / 2);
		next->uncompressedSize = static_cast<int> (next->width * next->height * components);
		next->uncompressedData = std::make_unique<char[]> (next->uncompressedSize);
		for (uint32_t y = 0; y < next->height; ++y) {
		    for (uint32_t x = 0; x < next->width; ++x) {
			for (size_t channel = 0; channel < components; ++channel) {
			    unsigned sum = 0;
			    for (uint32_t dy = 0; dy < 2; ++dy) {
				for (uint32_t dx = 0; dx < 2; ++dx) {
				    const size_t at = (std::min (y * 2 + dy, mipmap->height - 1) * mipmap->width
						       + std::min (x * 2 + dx, mipmap->width - 1))
					    * components
					+ channel;
				    sum += static_cast<unsigned char> (mipmap->uncompressedData[at]);
				}
			    }
			    next->uncompressedData[(y * next->width + x) * components + channel]
				= static_cast<char> (sum / 4);
			}
		    }
		}
		result->images[0].push_back (next);
		mipmap = std::move (next);
	    }
	}
	parseSpritesheetMetadata (*result, filename, [&metadata] (const std::string&) { return metadata.dump (); });
	return result;
    }
    auto result = parse (BinaryReader (compiled), filename, metadataLoader);
    if (!result->variants.empty ()) {
	compiled->seekg (0, std::ios::beg);
	result->variantSource = std::make_shared<const std::string> (
	    std::istreambuf_iterator<char> (*compiled), std::istreambuf_iterator<char> ()
	);
    }
    return result;
}
