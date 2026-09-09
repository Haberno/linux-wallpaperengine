#include "WallpaperEngine/Data/Assets/Texture.h"
#include "WallpaperEngine/Data/Parsers/TextureParser.h"
#include "WallpaperEngine/Data/Utils/BinaryReader.h"

#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Parsers/PropertyParser.h"
#include <lz4.h>
#include <stb_image_write.h>

#ifdef CHECK
#undef CHECK
#endif
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
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

TEST_CASE ("legacy TEXV0004 preserves texture metadata and every mip payload", "[texture][legacy]") {
    using namespace WallpaperEngine::Data::Assets;
    for (const auto format : { TextureFormat_DXT5, TextureFormat_ARGB8888 }) {
	CAPTURE (format);
	std::string legacy ("TEXV0004", 9);
	const uint32_t flags = TextureFlags_ClampUVs | TextureFlags_NoInterpolation;
	for (const uint32_t value : { static_cast<uint32_t> (format), flags, 8u, 8u, 6u, 5u }) {
	    appendValue (legacy, value);
	}
	appendValue (legacy, 3u); // One implicit image, followed directly by its mip count.
	std::vector<std::string> payloads;
	for (const uint32_t size : { 8u, 4u, 2u }) {
	    const uint32_t bytes = format == TextureFormat_DXT5 ? ((size + 3) / 4) * ((size + 3) / 4) * 16
							      : size * size * 4;
	    payloads.emplace_back (bytes, static_cast<char> (size));
	    for (const uint32_t value : { size, size, bytes }) {
		appendValue (legacy, value);
	    }
	    legacy += payloads.back ();
	}
	// The equivalent version-5 file adds section headers, an extra info field,
	// and an image count. The shared mip records must decode identically.
	std::string modern ("TEXV0005", 9);
	modern.append ("TEXI0001", 9);
	modern.append (legacy, 9, 24);
	appendValue (modern, 0u);
	modern.append ("TEXB0001", 9);
	appendValue (modern, 1u);
	modern.append (legacy, 33, std::string::npos);
	for (const auto& bytes : { legacy, modern }) {
	    const auto stream = std::make_shared<std::istringstream> (bytes, std::ios::binary);
	    const auto texture = TextureParser::parse (BinaryReader (stream));
	    CHECK (texture->format == format);
	    CHECK (texture->flags == flags);
	    CHECK (texture->textureWidth == 8);
	    CHECK (texture->textureHeight == 8);
	    CHECK (texture->width == 6);
	    CHECK (texture->height == 5);
	    CHECK (texture->freeImageFormat == FIF_UNKNOWN);
	    CHECK (texture->imageCount == 1);
	    REQUIRE (texture->images.size () == 1);
	    const auto& mips = texture->images.at (0);
	    REQUIRE (mips.size () == payloads.size ());
	    for (size_t level = 0; level < mips.size (); ++level) {
		CHECK (mips[level]->width == (8u >> level));
		CHECK (mips[level]->height == (8u >> level));
		CHECK (mips[level]->compression == 0);
		CHECK (mips[level]->uncompressedSize == static_cast<int> (payloads[level].size ()));
		REQUIRE (mips[level]->uncompressedData != nullptr);
		CHECK (std::string (mips[level]->uncompressedData.get (), mips[level]->uncompressedSize) == payloads[level]);
	    }
	    CHECK (stream->tellg () == static_cast<std::streamoff> (bytes.size ()));
	}
    }
}

TEST_CASE ("legacy texture parsing rejects incomplete headers and payloads", "[texture][legacy]") {
    std::string bytes ("TEXV0004", 9);
    for (const uint32_t value : { static_cast<uint32_t> (TextureFormat_ARGB8888), 0u, 1u, 1u, 1u, 1u,
				 1u, 1u, 1u, 4u }) {
	appendValue (bytes, value);
    }
    bytes += "rgba";
    for (size_t length = 0; length < bytes.size (); ++length) {
	CAPTURE (length);
	const auto stream = std::make_shared<std::istringstream> (bytes.substr (0, length), std::ios::binary);
	CHECK_THROWS (TextureParser::parse (BinaryReader (stream)));
    }
    for (const uint32_t count : { 0u, 33u }) {
	auto invalid = bytes;
	std::memcpy (invalid.data () + 33, &count, sizeof (count));
	const auto stream = std::make_shared<std::istringstream> (invalid, std::ios::binary);
	CHECK_THROWS (TextureParser::parse (BinaryReader (stream)));
    }
    for (const char version : { '3', '6' }) {
	auto unsupported = bytes;
	unsupported[7] = version;
	const auto stream = std::make_shared<std::istringstream> (unsupported, std::ios::binary);
	CHECK_THROWS (TextureParser::parse (BinaryReader (stream)));
    }
}

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

namespace {
using namespace WallpaperEngine::Data::Assets;
using WallpaperEngine::Data::Model::Properties;

std::string variantHeader (uint32_t format, int fif, const std::vector<TextureVariant>& variants, uint32_t images = 1) {
    std::string bytes;
    bytes.append ("TEXV0005", 9);
    bytes.append ("TEXI0001", 9);
    for (uint32_t value : { format, 0u, 8u, 8u, 8u, 8u, 0u }) {
	appendValue (bytes, value);
    }
    bytes.append ("TEXB0004", 9);
    appendValue (bytes, images);
    appendValue (bytes, fif);
    appendValue (bytes, static_cast<uint32_t> (variants.size ()));
    for (const auto& variant : variants) {
	appendValue (bytes, variant.group);
	appendValue (bytes, variant.id);
	appendValue (bytes, variant.flags);
	bytes += WallpaperEngine::Data::JSON::JSON { { "condition", variant.condition } }.dump ();
	bytes += '\0';
    }
    return bytes;
}

std::string lz4 (const std::string& bytes) {
    std::string result (LZ4_compressBound (static_cast<int> (bytes.size ())), '\0');
    const int size = LZ4_compress_default (
	bytes.data (), result.data (), static_cast<int> (bytes.size ()), static_cast<int> (result.size ())
    );
    REQUIRE (size > 0);
    result.resize (size);
    return result;
}

void appendMip (std::string& bytes, uint32_t size, const std::string& pixels, bool compressed = false) {
    const auto payload = compressed ? lz4 (pixels) : pixels;
    for (uint32_t value : { size, size, compressed ? 1u : 0u, static_cast<uint32_t> (pixels.size ()),
			    static_cast<uint32_t> (payload.size ()) }) {
	appendValue (bytes, value);
    }
    bytes += payload;
}

void appendPatch (
    std::string& bytes, uint32_t id, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const std::string& payload
) {
    for (uint32_t value : { 1u, id, x, y, width, height, 13u, static_cast<uint32_t> (payload.size ()) }) {
	appendValue (bytes, value);
    }
    bytes += payload;
}

std::unique_ptr<Texture> parseVariants (const std::string& bytes, const TextureParser::VariantSelector& select = {}) {
    return TextureParser::parse (BinaryReader (std::make_shared<std::istringstream> (bytes, std::ios::binary)), select);
}
} // namespace

TEST_CASE ("TEXB4 interleaves conditional patches after every image mip", "[texture][variants]") {
    const std::vector<TextureVariant> variants { { 1, 17, 0, "snow" }, { 1, 29, 0, "night" }, { 2, 45, 0, "detail" } };
    auto bytes = variantHeader (TextureFormat_ARGB8888, FIF_UNKNOWN, variants, 2);
    for (uint32_t image = 0; image < 2; ++image) {
	appendValue (bytes, 2u);
	for (uint32_t size : { 8u, 4u }) {
	    appendMip (bytes, size, std::string (size * size * 4, 'b'));
	    appendValue (bytes, 2u); // groups
	    appendValue (bytes, 2u); // patches in first group
	    appendPatch (bytes, 17, 0, 0, 2, 2, std::string (16, 's'));
	    appendPatch (bytes, 29, 0, 0, size, size, std::string (size * size * 4, 'n'));
	    appendValue (bytes, 1u);
	    appendPatch (bytes, 45, 2, 2, 1, 1, "dddd");
	}
    }
    const auto base = parseVariants (bytes);
    REQUIRE (base->containerVersion == ContainerVersion_TEXB0004);
    REQUIRE (base->images.size () == 2);
    const auto selected = parseVariants (bytes, [] (const auto&) { return true; });
    CHECK (
	TextureParser::selectedVariants (*selected, [] (const auto&) { return true; })
	== std::vector<uint32_t> { 17, 45 }
    );
    for (uint32_t image = 0; image < 2; ++image) {
	REQUIRE (base->images.at (image).size () == 2);
	REQUIRE (selected->images.at (image).size () == 2);
	for (const auto& mip : selected->images.at (image)) {
	    CHECK (mip->uncompressedData[0] == 's');
	    CHECK (mip->uncompressedData[mip->width * 4] == 's');
	    CHECK (mip->uncompressedData[(2 * mip->width + 2) * 4] == 'd');
	    CHECK (mip->uncompressedData[3 * 4] == 'b');
	}
	CHECK (base->images.at (image).back ()->uncompressedData[0] == 'b');
    }
    CHECK_THROWS (parseVariants (bytes.substr (0, bytes.size () - 1)));
    CHECK_THROWS (parseVariants (bytes.substr (0, 75)));
}

TEST_CASE ("TEXB4 decompresses block-aligned partial DXT patches", "[texture][variants]") {
    auto bytes = variantHeader (TextureFormat_DXT5, FIF_UNKNOWN, { { 1, 1, 0, "alternate" } });
    appendValue (bytes, 2u);
    appendMip (bytes, 8, std::string (64, 'b'), true);
    appendValue (bytes, 1u);
    appendValue (bytes, 1u);
    appendPatch (bytes, 1, 4, 4, 4, 4, lz4 (std::string (16, 'v')));
    appendMip (bytes, 4, std::string (16, 'c'), true);
    appendValue (bytes, 1u);
    appendValue (bytes, 1u);
    appendPatch (bytes, 1, 0, 0, 4, 4, lz4 (std::string (16, 'w')));
    const auto texture = parseVariants (bytes, [] (const auto&) { return true; });
    REQUIRE (texture->images.at (0).size () == 2);
    const auto& first = texture->images.at (0).front ();
    CHECK (std::string (first->uncompressedData.get (), 48) == std::string (48, 'b'));
    CHECK (std::string (first->uncompressedData.get () + 48, 16) == std::string (16, 'v'));
    CHECK (std::string (texture->images.at (0).back ()->uncompressedData.get (), 16) == std::string (16, 'w'));
}

TEST_CASE ("TEXB4 patches decoded PNG pixels without dropping smaller levels", "[texture][variants]") {
    auto bytes = variantHeader (TextureFormat_DXT5, FIF_PNG, { { 1, 1, 0, "alternate" } });
    appendValue (bytes, 2u);
    for (uint32_t size : { 8u, 4u }) {
	appendMip (bytes, size, encodePng (size, 1));
	appendValue (bytes, 1u);
	appendValue (bytes, 1u);
	appendPatch (bytes, 1, 1, 1, 2, 2, encodePng (2, 2));
    }
    const auto texture = parseVariants (bytes, [] (const auto&) { return true; });
    REQUIRE (texture->images.at (0).size () == 2);
    for (const auto& mip : texture->images.at (0)) {
	REQUIRE (mip->decodedData != nullptr);
	CHECK (mip->decodedData[0] == 31);
	CHECK (mip->decodedData[(mip->width + 1) * 4] == 62);
	CHECK (mip->decodedWidth == static_cast<int> (mip->width));
    }
}

TEST_CASE ("texture variants follow combo and boolean properties independently", "[texture][variants]") {
    using namespace WallpaperEngine::Data::Model;
    const auto combo = WallpaperEngine::Data::Parsers::PropertyParser::parse (
	{ { "type", "combo" },
	  { "value", "0" },
	  { "options", { { { "value", "0" }, { "label", "Base" } }, { { "value", "1" }, { "label", "Alt" } } } } },
	"style"
    );
    const auto boolean
	= WallpaperEngine::Data::Parsers::PropertyParser::parse ({ { "type", "bool" }, { "value", false } }, "enabled");
    const Properties properties { { "style", combo }, { "enabled", boolean } };
    const WallpaperEngine::Data::JSON::JSON condition { { "name", "style" }, { "condition", "1" } };
    CHECK_FALSE (TextureParser::matchesCondition (condition, properties));
    combo->update (std::string ("1"), DynamicValue::UpdateSource::User);
    CHECK (TextureParser::matchesCondition (condition, properties));
    CHECK_FALSE (TextureParser::matchesCondition ("enabled", properties));
    boolean->update (std::string ("true"), DynamicValue::UpdateSource::User);
    CHECK (TextureParser::matchesCondition ("enabled", properties));
    CHECK_FALSE (TextureParser::matchesCondition (condition, {}));
    CHECK_FALSE (TextureParser::matchesCondition ("missing", properties));
}

TEST_CASE ("source textures need metadata and compiled previews stay texture-only", "[texture][assets]") {
    using WallpaperEngine::Assets::AssetLocator;
    using WallpaperEngine::FileSystem::Container;
    auto files = std::make_unique<Container> ();
    auto& vfs = files->getVFS ();
    vfs.add ("materials/effects/source.png", encodePng (8, 1));
    vfs.add ("materials/effects/source.tex-json", R"({"format":"rgba8888","nomip":true})");
    const AssetLocator locator (std::move (files));
    const auto texture = TextureParser::load (locator, "effects/source");
    REQUIRE (texture->images.at (0).size () == 1);
    CHECK (texture->width == 8);
    CHECK (texture->format == TextureFormat_ARGB8888);
    CHECK (texture->images.at (0).front ()->uncompressedData[0] == 31);

    auto previews = std::make_unique<Container> ();
    previews->getVFS ().add ("effects/example/preview/materials/effects/only.tex", "compiled");
    previews->getVFS ().add ("effects/example/preview/scene.json", "preview scene");
    previews->getVFS ().add ("effects/example/preview/materials/collision.json", "preview material");
    const AssetLocator fallback (std::move (previews), { "effects/example/preview/materials" });
    const auto stream = fallback.texture ("effects/only");
    CHECK (std::string (std::istreambuf_iterator<char> (*stream), {}) == "compiled");
    CHECK_THROWS (fallback.readString ("scene.json"));
    CHECK_THROWS (fallback.readString ("materials/collision.json"));
    CHECK_THROWS (fallback.texture ("../scene.json"));
}

TEST_CASE ("source texture imports preserve channels, sampling flags and compiled precedence", "[texture][assets]") {
    using WallpaperEngine::Assets::AssetLocator;
    using WallpaperEngine::FileSystem::Container;
    for (const auto& [format, components] : std::map<std::string, size_t> {{"rgba8888", 4}, {"rg88n", 2}, {"r8", 1}}) {
	INFO (format);
	auto files = std::make_unique<Container> ();
	files->getVFS ().add ("materials/source.png", encodePng (8, 1));
	files->getVFS ().add ("materials/source.tex-json", WallpaperEngine::Data::JSON::JSON {
	    {"format", format}, {"nomip", false}, {"clampuvs", true}, {"nointerpolation", true}
	});
	const AssetLocator locator (std::move (files));
	const auto texture = TextureParser::load (locator, "source");
	REQUIRE (texture->images.at (0).size () == 4);
	CHECK (texture->images.at (0).back ()->width == 1);
	CHECK (texture->images.at (0).back ()->uncompressedSize == static_cast<int> (components));
	CHECK ((texture->flags & TextureFlags_ClampUVs) != 0);
	CHECK ((texture->flags & TextureFlags_NoInterpolation) != 0);
	CHECK (texture->images.at (0).front ()->uncompressedData[components] == 59);
    }
    auto missing = std::make_unique<Container> ();
    missing->getVFS ().add ("materials/source.png", encodePng (8, 1));
    CHECK_THROWS (TextureParser::load (AssetLocator (std::move (missing)), "source"));

    auto corrupt = std::make_unique<Container> ();
    corrupt->getVFS ().add ("materials/source.tex", "broken compiled texture");
    corrupt->getVFS ().add ("materials/source.png", encodePng (8, 1));
    corrupt->getVFS ().add ("materials/source.tex-json", R"({"format":"rgba8888","nomip":true})");
    CHECK_THROWS (TextureParser::load (AssetLocator (std::move (corrupt)), "source"));
}

TEST_CASE ("installed TEXB4 corpus decodes every authored conditional mip chain", "[.][texture-corpus]") {
    const char* root = std::getenv ("LWE_TEXTURE_CORPUS");
    REQUIRE (root != nullptr);
    size_t textures = 0, variants = 0, mipmaps = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (root)) {
	if (entry.path ().extension () != ".tex") {
	    continue;
	}
	std::ifstream input (entry.path (), std::ios::binary);
	const std::string bytes { std::istreambuf_iterator<char> (input), {} };
	if (bytes.size () < 67 || bytes.substr (46, 9) != std::string ("TEXB0004", 9)) {
	    continue;
	}
	INFO (entry.path ().string ());
	auto base = parseVariants (bytes);
	++textures;
	for (const auto& [image, levels] : base->images) {
	    mipmaps += levels.size ();
	}
	for (const auto& variant : base->variants) {
	    ++variants;
	    const auto selected
		= parseVariants (bytes, [&variant] (const auto& condition) { return condition == variant.condition; });
	    REQUIRE (selected->images.size () == base->images.size ());
	    for (const auto& [image, levels] : base->images) {
		REQUIRE (selected->images.at (image).size () == levels.size ());
		for (size_t level = 0; level < levels.size (); ++level) {
		    CHECK (selected->images.at (image)[level]->width == levels[level]->width);
		    CHECK (selected->images.at (image)[level]->height == levels[level]->height);
		}
	    }
	}
    }
    std::cout << "TEXB4 corpus: " << textures << " textures, " << variants << " variants, " << mipmaps << " mipmaps\n";
    REQUIRE (variants > 0);
}
