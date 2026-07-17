#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Parsers/MaterialParser.h"

using WallpaperEngine::Data::JSON::JSON;
using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Data::Parsers::MaterialParser;

TEST_CASE ("optional tolerates authored type drift") {
    // workshop 3758354038 authors text "padding" as a vector string where older scenes
    // store a number; a mismatched optional must default, not std::terminate the engine
    const auto data = JSON::parse (R"({"padding": "32.00000 32.00000", "maxrows": 1})");

    REQUIRE (data.optional ("padding", 7) == 7);
    REQUIRE_FALSE (data.optional<int> ("padding").has_value ());
    REQUIRE (data.optional ("maxrows", 0) == 1);
    REQUIRE (data.optional ("missing", 3) == 3);
}

TEST_CASE ("alpha-to-coverage material blending is preserved") {
    REQUIRE (MaterialParser::parseBlendMode ("alphatocoverage") == BlendingMode_AlphaToCoverage);
}
