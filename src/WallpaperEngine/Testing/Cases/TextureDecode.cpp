#include "WallpaperEngine/Data/Assets/Texture.h"
#include "WallpaperEngine/Data/Parsers/TextureParser.h"

#include <stb_image_write.h>

#ifdef CHECK
#undef CHECK
#endif
#include <catch2/catch_test_macros.hpp>

using WallpaperEngine::Data::Assets::FIF_PNG;
using WallpaperEngine::Data::Assets::Mipmap;
using WallpaperEngine::Data::Assets::Texture;
using WallpaperEngine::Data::Parsers::TextureParser;

namespace {
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
