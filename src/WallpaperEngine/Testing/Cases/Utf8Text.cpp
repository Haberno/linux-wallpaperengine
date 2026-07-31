#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Render/Objects/CText.h"

using WallpaperEngine::Render::Objects::computeTextAlignmentOffset;
using WallpaperEngine::Render::Objects::computeTextEffectLayout;
using WallpaperEngine::Render::Objects::nextUtf8Codepoint;

TEST_CASE ("utf8 codepoint decoding") {
    // "aé画😀" — 1-, 2-, 3- and 4-byte sequences
    const std::string text = "a\xC3\xA9\xE7\x94\xBB\xF0\x9F\x98\x80";
    size_t offset = 0;

    REQUIRE (nextUtf8Codepoint (text, offset) == 0x61);
    REQUIRE (nextUtf8Codepoint (text, offset) == 0xE9);
    REQUIRE (nextUtf8Codepoint (text, offset) == 0x753B);
    REQUIRE (nextUtf8Codepoint (text, offset) == 0x1F600);
    REQUIRE (offset == text.size ());
}

TEST_CASE ("utf8 malformed input consumes one byte at a time") {
    // stray continuation byte, then a lead byte truncated by end-of-string
    const std::string bad = "\x80\xC3";
    size_t offset = 0;

    REQUIRE (nextUtf8Codepoint (bad, offset) == 0xFFFD);
    REQUIRE (offset == 1);
    REQUIRE (nextUtf8Codepoint (bad, offset) == 0xFFFD);
    REQUIRE (offset == bad.size ());

    // lead byte followed by a non-continuation byte resynchronizes on the next char
    const std::string resync = "\xE7g";
    offset = 0;
    REQUIRE (nextUtf8Codepoint (resync, offset) == 0xFFFD);
    REQUIRE (nextUtf8Codepoint (resync, offset) == 0x67);
    REQUIRE (offset == resync.size ());
}

TEST_CASE ("text alignment follows native glyph metrics instead of the serialized layer size") {
    // x: [-10, 90], y-up: [-20, 80], so the actual glyph-bounds center is (40, 30).
    const glm::vec4 bounds = { -10.0f, -20.0f, 90.0f, 80.0f };

    const glm::vec2 center = computeTextAlignmentOffset ("center", "center", bounds, 100.0f, -25.0f, 120.0f, 1);
    REQUIRE (center.x == 0.0f);
    REQUIRE (center.y == -20.0f); // bounds center - ascender/2

    const glm::vec2 top = computeTextAlignmentOffset ("left", "top", bounds, 100.0f, -25.0f, 120.0f, 1);
    REQUIRE (top.x == 50.0f); // move the glyph center so its left-aligned block starts at the origin
    REQUIRE (top.y == -70.0f); // bounds center - ascender

    const glm::vec2 bottom = computeTextAlignmentOffset ("right", "bottom", bounds, 100.0f, -25.0f, 120.0f, 1);
    REQUIRE (bottom.x == -50.0f);
    REQUIRE (bottom.y == 55.0f); // bounds center - descender
}

TEST_CASE ("native text vertical alignment accounts for following rows") {
    const glm::vec4 bounds = { 0.0f, -140.0f, 80.0f, 80.0f };

    const glm::vec2 center = computeTextAlignmentOffset ("center", "center", bounds, 100.0f, -20.0f, 120.0f, 2);
    REQUIRE (center.y == -20.0f); // -30 - ((100 - 120) / 2)

    const glm::vec2 bottom = computeTextAlignmentOffset ("center", "bottom", bounds, 100.0f, -20.0f, 120.0f, 2);
    REQUIRE (bottom.y == 110.0f); // -30 - (-20 - 120)
}

TEST_CASE ("text effects use the native padded line box and split placement") {
    // Summer85 at 30pt/300DPI for "12:34": ink bounds are -4,-8..283,94,
    // while native expands the generated line box to -4,-31..283,114.
    const glm::vec4 glyphBounds = { -4.0f, -8.0f, 283.0f, 94.0f };
    const auto layout
	= computeTextEffectLayout ("center", "center", glyphBounds, 114.0f, -27.0f, 145.0f, 1, { 32, 32 });

    REQUIRE (layout.surfaceSize == glm::vec2 (351.0f, 209.0f));
    REQUIRE (layout.rasterOffset == glm::vec2 (0.0f, -1.5f));
    REQUIRE (layout.compositeOffset == glm::vec2 (0.0f, 15.5f));
    REQUIRE (layout.rasterOffset + layout.compositeOffset == glm::vec2 (0.0f, 14.0f));
}
