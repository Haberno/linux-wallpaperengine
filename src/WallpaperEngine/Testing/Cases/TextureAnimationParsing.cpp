#include "WallpaperEngine/Data/Assets/Texture.h"
#include "WallpaperEngine/Data/Parsers/TextureParser.h"
#include "WallpaperEngine/Data/Utils/BinaryReader.h"

#ifdef CHECK
#undef CHECK
#endif
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <sstream>

using namespace WallpaperEngine::Data::Assets;
using WallpaperEngine::Data::Parsers::TextureParser;
using WallpaperEngine::Data::Utils::BinaryReader;

namespace {
template <typename T> void appendValue (std::string& output, const T value) {
    output.append (reinterpret_cast<const char*> (&value), sizeof (value));
}

std::string makeSheet (const uint32_t version, const std::array<float, 6>& coordinates) {
    std::string bytes ("TEXV0005", 9);
    bytes.append ("TEXI0001", 9);
    // The authored 5x7 image occupies part of a block-compressed 20x12 atlas.
    for (const uint32_t value : { static_cast<uint32_t> (TextureFormat_DXT1),
				 static_cast<uint32_t> (TextureFlags_IsGif), 20u, 12u, 5u, 7u, 0u }) {
	appendValue (bytes, value);
    }
    bytes.append ("TEXB0001", 9);
    for (const uint32_t value : { 1u, 1u, 20u, 12u, 120u }) {
	appendValue (bytes, value);
    }
    bytes.append (120, '\0'); // 5x3 BC1 blocks.
    std::string animationMagic ("TEXS0001", 9);
    animationMagic[7] = static_cast<char> ('0' + version);
    bytes += animationMagic;
    appendValue (bytes, 1u);
    if (version == 3) {
	appendValue (bytes, 9u);
	appendValue (bytes, 11u);
    }
    appendValue (bytes, 0u); // Atlas page.
    appendValue (bytes, 0.25f);
    for (const float value : coordinates) {
	if (version == 1) {
	    appendValue (bytes, static_cast<int32_t> (value));
	} else {
	    appendValue (bytes, value);
	}
    }
    return bytes;
}

TextureUniquePtr parseSheet (const uint32_t version, const std::array<float, 6>& coordinates) {
    const auto stream = std::make_shared<std::istringstream> (makeSheet (version, coordinates), std::ios::binary);
    return TextureParser::parse (BinaryReader (stream));
}
} // namespace

TEST_CASE ("TEXS0001 preserves signed coordinates and rotated frame bases", "[texture][animation][texs]") {
    SECTION ("a quarter-turn keeps both off-axis basis components") {
	const auto texture = parseSheet (1, { 12, 2, 0, 5, -7, 0 });
	REQUIRE (texture->frames.size () == 1);
	const auto& frame = *texture->frames.front ();
	CHECK (frame.frameNumber == 0);
	CHECK (frame.frametime == 0.25f);
	CHECK (frame.x == 12);
	CHECK (frame.y == 2);
	CHECK (frame.width1 == 0);
	CHECK (frame.width2 == 5);
	CHECK (frame.height2 == -7);
	CHECK (frame.height1 == 0);
    }

    SECTION ("signed origins and basis components retain their sign") {
	std::string bytes;
	appendValue (bytes, 0u);
	appendValue (bytes, 0.25f);
	for (const int32_t value : { -2, -3, -5, -6, -7, -8 }) {
	    appendValue (bytes, value);
	}
	const auto stream = std::make_shared<std::istringstream> (bytes, std::ios::binary);
	const auto parsed = TextureParser::parseFrameV1 (BinaryReader (stream));
	const auto& frame = *parsed;
	CHECK (frame.x == -2);
	CHECK (frame.y == -3);
	CHECK (frame.width1 == -5);
	CHECK (frame.width2 == -6);
	CHECK (frame.height2 == -7);
	CHECK (frame.height1 == -8);
    }
}

TEST_CASE ("older TEXS logical dimensions come from TEXI rather than the first frame", "[texture][animation][texs]") {
    for (const uint32_t version : { 1u, 2u }) {
	CAPTURE (version);
	for (const std::array<float, 6> coordinates : { std::array<float, 6> { 4, 2, 8, 0, 0, 8 },
						     std::array<float, 6> { 12, 2, 0, 5, -7, 0 } }) {
	    CAPTURE (coordinates);
	    const auto texture = parseSheet (version, coordinates);
	    CHECK (texture->gifWidth == 5);
	    CHECK (texture->gifHeight == 7);
	    CHECK (texture->width == 5);
	    CHECK (texture->height == 7);
	    CHECK (texture->textureWidth == 20);
	    CHECK (texture->textureHeight == 12);
	    REQUIRE (texture->images.at (0).size () == 1);
	    CHECK (texture->images.at (0).front ()->width == 20);
	    CHECK (texture->images.at (0).front ()->height == 12);
	}
    }
}

TEST_CASE ("TEXS0003 retains its explicit logical dimensions and floating frame coordinates", "[texture][animation][texs]") {
    const auto texture = parseSheet (3, { 4.5f, 2.5f, 8, 0, 0, 8 });
    CHECK (texture->gifWidth == 9);
    CHECK (texture->gifHeight == 11);
    CHECK (texture->width == 5);
    CHECK (texture->height == 7);
    REQUIRE (texture->frames.size () == 1);
    CHECK (texture->frames.front ()->x == 4.5f);
    CHECK (texture->frames.front ()->y == 2.5f);
}
