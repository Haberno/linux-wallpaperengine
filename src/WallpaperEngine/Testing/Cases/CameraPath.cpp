#include "WallpaperEngine/Data/Parsers/CameraPathParser.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Model::CameraPath;
using WallpaperEngine::Data::Model::CameraTransform;
using WallpaperEngine::Data::Parsers::CameraPathParser;
using WallpaperEngine::Render::Wallpapers::CScene;
using namespace WallpaperEngine::Data::Model;

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

    const CameraTransform quarter = paths[0].evaluate (0.5f, {});
    CHECK (quarter.center.x == Catch::Approx (0.40625f));
    CHECK (quarter.center.y == Catch::Approx (0.8125f));
    CHECK (quarter.center.z == Catch::Approx (1.21875f));
    CHECK (quarter.eye.x == Catch::Approx (0.8125f));
    CHECK (quarter.zoom == Catch::Approx (1.203125f));
}

TEST_CASE ("Legacy camera paths honor disabled entries and synthesize timestamps") {
    const JSON data = JSON::parse (R"json({
        "paths": [
            {
                "name": "disabled path",
                "disabled": true,
                "duration": 6.0,
                "transforms": [{"center":"99 99 99"}]
            },
            {
                "name": "legacy",
                "visible": false,
                "duration": 6.0,
                "transforms": [
                    {"timestamp":0.0,"center":"0 0 0","eye":"0 0 2","up":"0 1 0","zoom":2.0},
                    {"disabled":true,"center":"99 99 99","zoom":99.0},
                    {"center":"4 0 0","eye":"4 0 2"},
                    {"center":"6 0 0","eye":"6 0 2"}
                ]
            }
        ]
    })json");

    const auto paths = CameraPathParser::parse (data);
    REQUIRE (paths.size () == 1);
    CHECK (paths[0].duration == Catch::Approx (6.0f));
    REQUIRE (paths[0].center[0].keyframes.size () == 3);
    CHECK (paths[0].center[0].keyframes[0].time == Catch::Approx (0.0f));
    CHECK (paths[0].center[0].keyframes[1].time == Catch::Approx (4.0f));
    CHECK (paths[0].center[0].keyframes[2].time == Catch::Approx (6.0f));
    CHECK (paths[0].center[0].keyframes[1].value == Catch::Approx (4.0f));

    REQUIRE (paths[0].zoom.keyframes.size () == 3);
    CHECK (paths[0].zoom.keyframes[0].value == Catch::Approx (2.0f));
    CHECK (paths[0].zoom.keyframes[1].value == Catch::Approx (1.0f));
    CHECK (paths[0].zoom.keyframes[2].value == Catch::Approx (1.0f));
    CHECK (paths[0].up[1].keyframes[1].value == Catch::Approx (0.0f));
}

TEST_CASE ("Legacy camera paths keep their authored duration") {
    const JSON data = JSON::parse (R"json({
        "paths": [{
            "duration": 2.0,
            "transforms": [
                {"timestamp":0.0,"center":"0 0 0"},
                {"timestamp":4.0,"center":"4 0 0"}
            ]
        }]
    })json");

    const auto paths = CameraPathParser::parse (data);
    REQUIRE (paths.size () == 1);
    CHECK (paths[0].duration == Catch::Approx (2.0f));
    CHECK (paths[0].center[0].keyframes.back ().time == Catch::Approx (4.0f));
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
    CHECK (paths[0].lengthFrames == 10);
    CHECK (paths[0].secondsPerFrame == Catch::Approx (0.1f));
    CHECK (paths[0].flags == CameraPathSingle);
    CHECK_FALSE (paths[0].legacy);

    // Handle x offsets are normalized against the segment, so symmetric handles
    // still put the value midpoint on the frame midpoint.
    CHECK (paths[0].center[0].evaluateFrame (5, 0.0f) == Catch::Approx (0.5f).margin (0.001f));
    // A channel with no handles at all stays linear. The solver stops once the
    // frame it solved for is within a hundredth of a frame, so off-midpoint
    // samples carry that much slack scaled by the channel's range.
    CHECK (paths[0].fov.evaluateFrame (5, 0.0f) == Catch::Approx (50.0f).margin (0.001f));
    CHECK (paths[0].fov.evaluateFrame (2, 0.0f) == Catch::Approx (44.0f).margin (0.05f));

    CameraTransform fallback;
    fallback.center = { 9.0f, 8.0f, 7.0f };
    fallback.fov = 50.0f;
    const CameraTransform sampled = paths[0].evaluate (0.0f, fallback);
    CHECK (sampled.center.x == Catch::Approx (0.0f).margin (0.0001f));
    // channels the path does not author fall back to the camera's own transform
    CHECK (sampled.center.y == Catch::Approx (8.0f));
    CHECK (sampled.fov == Catch::Approx (40.0f));
    CHECK (sampled.zoom == Catch::Approx (1.0f));
}

TEST_CASE ("Authored handles ease the value away from a straight line") {
    // One real channel out of Pokemon Deep Sea Dive 3562141459's
    // scripts/camera_paths_2233.json: two keyframes, auto tangents, zero y
    // offsets. The shot travels in a straight line but must not travel at a
    // constant rate, which is the whole point of the handles.
    const JSON data = JSON::parse (R"json({
        "paths": [{
            "options": {"fps":30.0,"length":300,"mode":"single"},
            "center": {"c0":[
                {"frame":0,"value":3.35324,
                 "back":{"enabled":true,"x":-1,"y":-0.0},"front":{"enabled":true,"x":0.50166667,"y":0}},
                {"frame":300,"value":2.38255,
                 "back":{"enabled":true,"x":-0.50166667,"y":-0.0},"front":{"enabled":true,"x":1,"y":0}}
            ]}
        }]
    })json");

    const auto paths = CameraPathParser::parse (data);
    REQUIRE (paths.size () == 1);
    CHECK (paths[0].duration == Catch::Approx (10.0f));

    const auto& channel = paths[0].center[0];
    REQUIRE (channel.keyframes.size () == 2);
    CHECK (channel.keyframes[1].frame == 300);

    CHECK (channel.evaluateFrame (0, 0.0f) == Catch::Approx (3.35324f).margin (0.001f));
    CHECK (channel.evaluateFrame (300, 0.0f) == Catch::Approx (2.38255f).margin (0.001f));
    // symmetric handles still cross the midpoint on time
    CHECK (channel.evaluateFrame (150, 0.0f) == Catch::Approx (2.86789f).margin (0.002f));
    // ...but the quarter points lag the straight line by ~6.8% of the range
    CHECK (channel.evaluateFrame (75, 0.0f) == Catch::Approx (3.17650f).margin (0.002f));
    CHECK (channel.evaluateFrame (225, 0.0f) == Catch::Approx (2.55929f).margin (0.002f));

    // and the same easing has to survive the seconds-to-frame sampling
    CameraTransform fallback;
    const CameraTransform quarter = paths[0].evaluate (2.5f, fallback);
    CHECK (quarter.center.x == Catch::Approx (3.17650f).margin (0.01f));
}

TEST_CASE ("Step keyframes hold the previous value for their whole segment") {
    const JSON data = JSON::parse (R"json({
        "paths": [{
            "options": {"fps":10.0,"length":10},
            "center": {"c0":[
                {"frame":0,"value":0},
                {"frame":5,"value":1,"step":true,"front":{"enabled":true,"x":1,"y":5}},
                {"frame":10,"value":2}
            ]}
        }]
    })json");

    const auto paths = CameraPathParser::parse (data);
    REQUIRE (paths.size () == 1);
    const auto& channel = paths[0].center[0];
    // a step keyframe drops its handles entirely
    CHECK (channel.keyframes[1].step);
    CHECK (channel.keyframes[1].outgoing.offset.x == Catch::Approx (0.0f));

    CHECK (channel.evaluateFrame (1, 0.0f) == Catch::Approx (0.0f));
    CHECK (channel.evaluateFrame (4, 0.0f) == Catch::Approx (0.0f));
    CHECK (channel.evaluateFrame (5, 0.0f) == Catch::Approx (1.0f));
    CHECK (channel.evaluateFrame (7, 0.0f) == Catch::Approx (1.4f).margin (0.001f));
}

TEST_CASE ("wraploop closes a curve back onto its first value") {
    const JSON data = JSON::parse (R"json({
        "paths": [{
            "options": {"fps":10.0,"length":10,"wraploop":true},
            "center": {"c0":[
                {"frame":0,"value":3,"front":{"enabled":true,"x":0.5,"y":0.25}},
                {"frame":4,"value":7}
            ]}
        }]
    })json");

    const auto paths = CameraPathParser::parse (data);
    REQUIRE (paths.size () == 1);
    CHECK ((paths[0].flags & CameraPathWrapLoop) != 0);

    const auto& channel = paths[0].center[0];
    REQUIRE (channel.keyframes.size () == 3);
    CHECK (channel.keyframes[2].frame == 10);
    CHECK (channel.keyframes[2].value == Catch::Approx (3.0f));
    // the closing handle mirrors the opening one so the seam stays smooth
    CHECK (channel.keyframes[2].incoming.enabled);
    CHECK (channel.keyframes[2].incoming.offset.x == Catch::Approx (-0.5f));
    CHECK (channel.keyframes[2].incoming.offset.y == Catch::Approx (-0.25f));
}

TEST_CASE ("Camera curve keyframes that do not advance are dropped") {
    const JSON data = JSON::parse (R"json({
        "paths": [{
            "options": {"fps":10.0,"length":10,"mode":"mirror","startpaused":true,"random":true},
            "center": {"c0":[
                {"frame":0,"value":0},
                {"frame":6,"value":6},
                {"frame":3,"value":99},
                {"frame":6,"value":99},
                {"frame":9,"value":9}
            ]}
        }]
    })json");

    const auto paths = CameraPathParser::parse (data);
    REQUIRE (paths.size () == 1);
    CHECK (paths[0].flags == (CameraPathMirror | CameraPathStartPaused | CameraPathRandom));

    const auto& channel = paths[0].center[0];
    REQUIRE (channel.keyframes.size () == 3);
    CHECK (channel.keyframes[0].frame == 0);
    CHECK (channel.keyframes[1].frame == 6);
    CHECK (channel.keyframes[2].frame == 9);
}

TEST_CASE ("Camera path playback modes follow the authored options") {
    CameraPath path;
    path.duration = 2.0f;

    SECTION ("loop wraps and never reports back") {
	const auto wrapped = CScene::advanceCameraPath (path, { 1.5f, 0 }, 1.0f);
	CHECK (wrapped.elapsed == Catch::Approx (0.5f));
	CHECK (wrapped.state == 0);
    }

    SECTION ("single stops at the end and hands the queue on") {
	path.flags = CameraPathSingle;
	const auto finished = CScene::advanceCameraPath (path, { 1.5f, 0 }, 1.0f);
	CHECK (finished.elapsed == Catch::Approx (2.0f));
	CHECK ((finished.state & CameraPathFinished) != 0);
	// a finished shot stays put until the queue moves it along
	CHECK (CScene::advanceCameraPath (path, finished, 1.0f).elapsed == Catch::Approx (2.0f));
    }

    SECTION ("mirror turns around at both ends") {
	path.flags = CameraPathMirror;
	const auto reversed = CScene::advanceCameraPath (path, { 1.5f, 0 }, 1.0f);
	CHECK (reversed.elapsed == Catch::Approx (1.5f));
	CHECK ((reversed.state & CameraPathReversed) != 0);
	// now running backwards
	const auto backwards = CScene::advanceCameraPath (path, reversed, 1.0f);
	CHECK (backwards.elapsed == Catch::Approx (0.5f));
	const auto forwards = CScene::advanceCameraPath (path, backwards, 1.0f);
	CHECK (forwards.elapsed == Catch::Approx (0.5f));
	CHECK ((forwards.state & CameraPathReversed) == 0);
    }

    SECTION ("startpaused never advances") {
	path.flags = CameraPathStartPaused;
	CHECK (CScene::advanceCameraPath (path, { 0.0f, 0 }, 1.0f).elapsed == Catch::Approx (0.0f));
    }
}
