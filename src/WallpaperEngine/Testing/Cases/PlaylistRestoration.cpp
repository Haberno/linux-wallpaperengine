#include "WallpaperEngine/Application/PlaylistState.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>

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
