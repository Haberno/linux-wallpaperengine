#include "WallpaperEngine/Data/Assets/Texture.h"
#include "WallpaperEngine/Data/Parsers/TextureParser.h"
#include "WallpaperEngine/Data/Utils/BinaryReader.h"

#include <stb_image_write.h>

#ifdef CHECK
#undef CHECK
#endif
#include <catch2/catch_test_macros.hpp>

#include <sstream>

using WallpaperEngine::Data::Assets::FIF_PNG;
using WallpaperEngine::Data::Assets::Mipmap;
using WallpaperEngine::Data::Assets::Texture;
using WallpaperEngine::Data::Assets::TextureFlags_ClampUVs;
using WallpaperEngine::Data::Assets::TextureFlags_Video;
using WallpaperEngine::Data::Assets::TextureFormat_ARGB8888;
using WallpaperEngine::Data::Parsers::TextureParser;
using WallpaperEngine::Data::Utils::BinaryReader;

namespace {
template <typename T> void appendValue (std::string& output, const T value) {
    output.append (reinterpret_cast<const char*> (&value), sizeof (value));
}

/** A PNG of the given size, with pixels varying per position so a mixed-up decode shows up */
std::string encodePng (const int size, const int seed) {
    std::vector<unsigned char> pixels (static_cast<size_t> (size) * size * 4);
    for (size_t index = 0; index < pixels.size (); index++) {
	pixels[index] = static_cast<unsigned char> ((index * 7 + seed * 31) % 251);
    }

    std::string encoded;
    stbi_write_png_to_func (
	[] (void* context, void* data, int length) {
	    static_cast<std::string*> (context)->append (static_cast<const char*> (data), length);
	},
	&encoded, size, size, 4, pixels.data (), size * 4
    );

    return encoded;
}

/** One texture holding mipmapCount PNG mipmaps, halving in size like a real chain */
std::unique_ptr<Texture> makePngTexture (const int mipmapCount, const int seed) {
    auto texture = std::make_unique<Texture> ();
    texture->freeImageFormat = FIF_PNG;
    texture->imageCount = 1;

    WallpaperEngine::Data::Assets::MipmapList mipmaps;
    for (int level = 0; level < mipmapCount; level++) {
	const int size = 64 >> level;
	const std::string encoded = encodePng (size, seed + level);

	auto mipmap = std::make_shared<Mipmap> ();
	mipmap->width = size;
	mipmap->height = size;
	mipmap->uncompressedSize = static_cast<int> (encoded.size ());
	mipmap->uncompressedData = std::unique_ptr<char[]> (new char[encoded.size ()]);
	std::memcpy (mipmap->uncompressedData.get (), encoded.data (), encoded.size ());
	mipmaps.emplace_back (std::move (mipmap));
    }

    texture->images.emplace (0, std::move (mipmaps));

    return texture;
}
} // namespace

TEST_CASE ("TEXB0004 video mipmaps preserve their embedded MP4 payload", "[texture][video]") {
    constexpr uint32_t width = 2324;
    constexpr uint32_t height = 2474;
    const std::string mp4 = std::string ("\0\0\0\x18" "ftypmp42\0\0\0\0mp42mp41", 24);

    std::string bytes;
    bytes.append ("TEXV0005", 9);
    bytes.append ("TEXI0001", 9);
    appendValue<uint32_t> (bytes, TextureFormat_ARGB8888);
    appendValue<uint32_t> (bytes, TextureFlags_Video | TextureFlags_ClampUVs);
    appendValue<uint32_t> (bytes, width);
    appendValue<uint32_t> (bytes, height);
    appendValue<uint32_t> (bytes, width);
    appendValue<uint32_t> (bytes, height);
    appendValue<uint32_t> (bytes, 0);
    bytes.append ("TEXB0004", 9);
    appendValue<uint32_t> (bytes, 1); // image count
    appendValue<uint32_t> (bytes, UINT32_MAX); // FIF_UNKNOWN
    appendValue<uint32_t> (bytes, 0); // no conditional images
    appendValue<uint32_t> (bytes, 1); // mip count
    appendValue<uint32_t> (bytes, width);
    appendValue<uint32_t> (bytes, height);
    appendValue<uint32_t> (bytes, 0); // compression
    appendValue<int> (bytes, 0); // video containers leave the uncompressed size empty
    appendValue<int> (bytes, static_cast<int> (mp4.size ()));
    bytes.append (mp4);

    const auto stream = std::make_shared<std::istringstream> (bytes, std::ios::in | std::ios::binary);
    const auto texture = TextureParser::parse (BinaryReader (stream));

    REQUIRE (texture->images.size () == 1);
    REQUIRE (texture->images.at (0).size () == 1);
    const auto& mipmap = texture->images.at (0).front ();
    CHECK (mipmap->width == width);
    CHECK (mipmap->height == height);
    CHECK (mipmap->compression == 0);
    CHECK (mipmap->compressedSize == static_cast<int> (mp4.size ()));
    CHECK (mipmap->uncompressedSize == static_cast<int> (mp4.size ()));
    REQUIRE (mipmap->uncompressedData != nullptr);
    CHECK (std::memcmp (mipmap->uncompressedData.get (), mp4.data (), mp4.size ()) == 0);
}

TEST_CASE ("batched mipmap decoding matches decoding one texture at a time", "[texture][decode]") {
    // enough textures and mipmaps to hand every thread of the pool some work
    constexpr int textureCount = 12;

    std::vector<std::unique_ptr<Texture>> serial;
    std::vector<std::unique_ptr<Texture>> batched;
    std::vector<Texture*> batch;

    for (int index = 0; index < textureCount; index++) {
	const int mipmapCount = 1 + index % 4;
	serial.emplace_back (makePngTexture (mipmapCount, index));
	batched.emplace_back (makePngTexture (mipmapCount, index));
	batch.emplace_back (batched.back ().get ());
    }

    for (const auto& texture : serial) {
	TextureParser::decodeMipmaps (*texture);
    }

    TextureParser::decodeMipmaps (batch);

    for (int index = 0; index < textureCount; index++) {
	const auto& expected = serial[index]->images.at (0);
	const auto& actual = batched[index]->images.at (0);

	REQUIRE (actual.size () == expected.size ());

	for (size_t level = 0; level < expected.size (); level++) {
	    REQUIRE (expected[level]->decodedData != nullptr);
	    REQUIRE (actual[level]->decodedData != nullptr);
	    CHECK (actual[level]->decodedWidth == expected[level]->decodedWidth);
	    CHECK (actual[level]->decodedHeight == expected[level]->decodedHeight);

	    const size_t bytes = static_cast<size_t> (expected[level]->decodedWidth)
		* expected[level]->decodedHeight * 4;
	    CHECK (std::memcmp (actual[level]->decodedData.get (), expected[level]->decodedData.get (), bytes) == 0);
	}
    }
}
