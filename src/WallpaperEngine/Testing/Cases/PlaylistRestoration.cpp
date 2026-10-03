#include "WallpaperEngine/Application/PlaylistState.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

using WallpaperEngine::Application::ApplicationContext;
namespace PlaylistState = WallpaperEngine::Application::PlaylistState;

TEST_CASE ("Timer playlists restore position and elapsed shutdown time", "[playlist-restoration]") {
    char name[] = "/tmp/lwe-playlist-state-test-XXXXXX";
    const char* created = mkdtemp (name);
    REQUIRE (created != nullptr);
    const std::filesystem::path directory = created;
    struct Cleanup {
	std::filesystem::path path;
	~Cleanup () {
	    std::error_code ignored;
	    std::filesystem::remove_all (path, ignored);
	}
    } cleanup { directory };

    ApplicationContext::PlaylistDefinition definition;
    definition.items = { "/wallpaper/a", "/wallpaper/b", "/wallpaper/c" };
    definition.settings.delayMinutes = 1;
    const auto at = [] (int64_t seconds) {
	return std::chrono::system_clock::time_point { std::chrono::seconds (seconds) };
    };
    std::mt19937 rng (17);
    PlaylistState::save ("DP-1", definition, { { 2, 0, 1 }, 1, 20 }, at (1000), directory);

    const auto shortStop = PlaylistState::restore ("DP-1", definition, rng, at (1015), directory);
    REQUIRE (shortStop.has_value ());
    CHECK (shortStop->order == std::vector<std::size_t> { 2, 0, 1 });
    CHECK (shortStop->orderIndex == 1);
    CHECK (shortStop->elapsedSeconds == 35);

    const auto boundary = PlaylistState::restore ("DP-1", definition, rng, at (1040), directory);
    REQUIRE (boundary.has_value ());
    CHECK (boundary->orderIndex == 1);
    CHECK (boundary->elapsedSeconds == 60);

    const auto expired = PlaylistState::restore ("DP-1", definition, rng, at (1041), directory);
    REQUIRE (expired.has_value ());
    CHECK (expired->orderIndex == 2);
    // Golden from native 14041d0c0: remainder(61 seconds / 60, 1 minute).
    CHECK (expired->elapsedSeconds == Catch::Approx (0.01666665077f));

    const auto longStop = PlaylistState::restore ("DP-1", definition, rng, at (1201), directory);
    REQUIRE (longStop.has_value ());
    CHECK (longStop->orderIndex == 2);
    CHECK (longStop->elapsedSeconds == Catch::Approx (0.6833333969f));

    definition.settings.delayMinutes = 2;
    const auto longerDelay = PlaylistState::restore ("DP-1", definition, rng, at (1041), directory);
    REQUIRE (longerDelay.has_value ());
    CHECK (longerDelay->orderIndex == 1);
    CHECK (longerDelay->elapsedSeconds == 61);

    const auto backwardsClock = PlaylistState::restore ("DP-1", definition, rng, at (999), directory);
    REQUIRE (backwardsClock.has_value ());
    CHECK (backwardsClock->elapsedSeconds == 20);
    CHECK_FALSE (PlaylistState::restore ("DP-2", definition, rng, at (1015), directory).has_value ());

    definition.settings.order = "random";
    definition.settings.delayMinutes = 1;
    PlaylistState::save ("DP-1", definition, { { 2, 0, 1 }, 2, 59 }, at (1000), directory);
    const auto randomPosition = PlaylistState::restore ("DP-1", definition, rng, at (1000), directory);
    REQUIRE (randomPosition.has_value ());
    CHECK (randomPosition->orderIndex == 2);
    CHECK (randomPosition->order == std::vector<std::size_t> { 2, 0, 1 });
    auto expectedOrder = randomPosition->order;
    auto expectedRng = rng;
    std::shuffle (expectedOrder.begin (), expectedOrder.end (), expectedRng);
    const auto randomWrap = PlaylistState::restore ("DP-1", definition, rng, at (1002), directory);
    REQUIRE (randomWrap.has_value ());
    CHECK (randomWrap->orderIndex == 0);
    CHECK (randomWrap->order == expectedOrder);

    definition.items.push_back ("/wallpaper/d");
    CHECK_FALSE (PlaylistState::restore ("DP-1", definition, rng, at (1015), directory).has_value ());
}

TEST_CASE ("Named sequential playlists retain a checkpoint after appending items", "[playlist-append]") {
    char name[] = "/tmp/lwe-playlist-append-test-XXXXXX";
    const char* created = mkdtemp (name);
    REQUIRE (created != nullptr);
    const std::filesystem::path directory = created;
    struct Cleanup {
	std::filesystem::path path;
	~Cleanup () {
	    std::error_code ignored;
	    std::filesystem::remove_all (path, ignored);
	}
    } cleanup { directory };
    const auto at = [] (int64_t seconds) {
	return std::chrono::system_clock::time_point { std::chrono::seconds (seconds) };
    };
    std::mt19937 rng (17);
    ApplicationContext::PlaylistDefinition original;
    original.name = "Evening";
    original.items = { "/wallpaper/a", "/wallpaper/b", "/wallpaper/c" };
    original.settings.delayMinutes = 1;
    PlaylistState::save ("DP-1", original, { { 0, 1, 2 }, 1, 20 }, at (1000), directory);
    auto appended = original;
    appended.items.push_back ("/wallpaper/d");
    const auto restored = PlaylistState::restore ("DP-1", appended, rng, at (1010), directory);
    REQUIRE (restored.has_value ());
    CHECK (restored->order == std::vector<std::size_t> { 0, 1, 2, 3 });
    CHECK (restored->orderIndex == 1);
    CHECK (restored->elapsedSeconds == 30);
    auto unrelated = appended;
    unrelated.name = "Morning";
    CHECK_FALSE (PlaylistState::restore ("DP-1", unrelated, rng, at (1010), directory).has_value ());
    CHECK_FALSE (PlaylistState::restore ("DP-2", appended, rng, at (1010), directory).has_value ());
    unrelated = appended;
    unrelated.settings.order = "random";
    CHECK_FALSE (PlaylistState::restore ("DP-1", unrelated, rng, at (1010), directory).has_value ());
    unrelated = appended;
    std::swap (unrelated.items[0], unrelated.items[1]);
    CHECK_FALSE (PlaylistState::restore ("DP-1", unrelated, rng, at (1010), directory).has_value ());
    unrelated = original;
    unrelated.items.pop_back ();
    CHECK_FALSE (PlaylistState::restore ("DP-1", unrelated, rng, at (1010), directory).has_value ());
    PlaylistState::save ("DP-1", original, { { 0, 1, 2 }, 2, 20 }, at (1000), directory);
    const auto expired = PlaylistState::restore ("DP-1", appended, rng, at (1041), directory);
    REQUIRE (expired.has_value ());
    CHECK (expired->orderIndex == 3);
    CHECK (expired->elapsedSeconds == Catch::Approx (0.01666665077f));
    PlaylistState::save ("DP-1", original, { { 2, 0, 1 }, 1, 20 }, at (1000), directory);
    CHECK_FALSE (PlaylistState::restore ("DP-1", appended, rng, at (1010), directory).has_value ());
    original.name.clear ();
    appended.name.clear ();
    PlaylistState::save ("DP-1", original, { { 0, 1, 2 }, 1, 20 }, at (1000), directory);
    CHECK_FALSE (PlaylistState::restore ("DP-1", appended, rng, at (1010), directory).has_value ());

    // External v1 fixture: its filename was computed before the v2 implementation.
    {
	std::ofstream file (directory / "8b3ba580973b3028.json");
	file << R"({"version":1,"identity":{"screen":"DP-legacy","items":["/wallpaper/a","/wallpaper/b","/wallpaper/c"],"mode":"timer","order":"sequential"},"order":[0,1,2],"index":1,"elapsed":20,"savedAt":1000})";
    }
    original.name = "Legacy";
    const auto legacy = PlaylistState::restore ("DP-legacy", original, rng, at (1010), directory);
    REQUIRE (legacy.has_value ());
    CHECK (legacy->orderIndex == 1);
    CHECK (legacy->elapsedSeconds == 30);
    appended.name = original.name;
    CHECK_FALSE (PlaylistState::restore ("DP-legacy", appended, rng, at (1010), directory).has_value ());
}
