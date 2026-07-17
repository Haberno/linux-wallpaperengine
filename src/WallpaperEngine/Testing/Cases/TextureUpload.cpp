#include "WallpaperEngine/Data/Assets/Texture.h"
#include "WallpaperEngine/Render/CTexture.h"

#ifdef CHECK
#undef CHECK
#endif
#include <catch2/catch_test_macros.hpp>

using WallpaperEngine::Data::Assets::TextureFormat_ARGB8888;
using WallpaperEngine::Data::Assets::TextureFormat_DXT1;
using WallpaperEngine::Data::Assets::TextureFormat_DXT5;
using WallpaperEngine::Data::Assets::TextureFormat_RG88;
using WallpaperEngine::Render::CTexture;

TEST_CASE ("TEX payload layout is detected independently from its authored format", "[texture]") {
    SECTION ("DXT5 block payload") {
	CHECK (CTexture::isBlockCompressedPayload (TextureFormat_DXT5, 512, 512, 512 * 512));
    }

    SECTION ("DXT5 expanded RGBA payload") {
	CHECK_FALSE (CTexture::isBlockCompressedPayload (TextureFormat_DXT5, 512, 512, 512 * 512 * 4));
    }

    SECTION ("DXT1 block payload rounds dimensions to complete blocks") {
	CHECK (CTexture::isBlockCompressedPayload (TextureFormat_DXT1, 5, 7, 2 * 2 * 8));
    }

    SECTION ("uncompressed authored formats never use block upload") {
	CHECK_FALSE (CTexture::isBlockCompressedPayload (TextureFormat_ARGB8888, 512, 512, 512 * 512 * 4));
	CHECK_FALSE (CTexture::isBlockCompressedPayload (TextureFormat_RG88, 512, 512, 512 * 512 * 2));
    }
}
