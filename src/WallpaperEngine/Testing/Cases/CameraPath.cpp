#include "WallpaperEngine/Data/Parsers/CameraPathParser.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Model::CameraTransform;
using WallpaperEngine::Data::Parsers::CameraPathParser;

TEST_CASE ("Legacy camera paths interpolate timestamp transforms") {
    const JSON data = JSON::parse (R"json({
        "paths": [{
            "name": "legacy",
            "duration": 2.0,
            "transforms": [
                {"timestamp":0.0,"center":"0 0 0","eye":"0 0 2","up":"0 1 0","zoom":1.0},
                {"timestamp":2.0,"center":"2 4 6","eye":"4 6 8","up":"0 1 0","zoom":2.0}
            ]
        }]
    })json");

    const auto paths = CameraPathParser::parse (data);
    REQUIRE (paths.size () == 1);
    CHECK (paths[0].duration == Catch::Approx (2.0f));

    const CameraTransform sampled = paths[0].evaluate (1.0f, {});
    CHECK (sampled.center.x == Catch::Approx (1.0f));
    CHECK (sampled.center.y == Catch::Approx (2.0f));
    CHECK (sampled.center.z == Catch::Approx (3.0f));
    CHECK (sampled.eye.x == Catch::Approx (2.0f));
    CHECK (sampled.zoom == Catch::Approx (1.5f));
}

TEST_CASE ("Modern camera curves use frame rate and Bezier handles") {
    const JSON data = JSON::parse (R"json({
        "paths": [{
            "name": "curve",
            "visible": true,
            "options": {"fps":10.0,"length":10,"mode":"single"},
            "center": {"c0":[
                {"frame":0,"value":0,"front":{"enabled":true,"x":0.3333333,"y":0}},
                {"frame":10,"value":1,"back":{"enabled":true,"x":-0.3333333,"y":0}}
            ]},
            "eye": {}, "up": {},
            "fov":[{"frame":0,"value":40},{"frame":10,"value":60}],
            "zoom":null
        }]
    })json");

    const auto paths = CameraPathParser::parse (data);
    REQUIRE (paths.size () == 1);
    CHECK (paths[0].duration == Catch::Approx (1.0f));

    CameraTransform fallback;
    fallback.center = { 9.0f, 8.0f, 7.0f };
    fallback.fov = 50.0f;
    const CameraTransform sampled = paths[0].evaluate (0.5f, fallback);
    CHECK (sampled.center.x == Catch::Approx (0.5f).margin (0.0001f));
    CHECK (sampled.center.y == Catch::Approx (8.0f));
    CHECK (sampled.fov == Catch::Approx (50.0f));
    CHECK (sampled.zoom == Catch::Approx (1.0f));
}
