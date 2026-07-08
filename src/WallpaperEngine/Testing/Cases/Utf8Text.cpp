#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Render/Objects/CText.h"

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
