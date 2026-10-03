#include "PlaylistState.h"

#include "WallpaperEngine/Data/Utils/ScopeGuard.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace WallpaperEngine::Application::PlaylistState {
namespace {
using JSON = nlohmann::json;

std::filesystem::path stateDirectory (const std::filesystem::path& directory) {
    if (!directory.empty ()) return directory;
    if (const char* state = std::getenv ("XDG_STATE_HOME"); state && state[0] == '/') {
	return std::filesystem::path (state) / "linux-wallpaperengine/playlists";
    }
    if (const char* home = std::getenv ("HOME"); home && home[0] == '/') {
	return std::filesystem::path (home) / ".local/state/linux-wallpaperengine/playlists";
    }
    return {};
}

JSON identity (const std::string& screen, const ApplicationContext::PlaylistDefinition& definition) {
    std::vector<std::string> items;
    for (const auto& item : definition.items) items.push_back (item.string ());
    return { { "screen", screen }, { "name", definition.name }, { "items", items }, { "order", definition.settings.order },
	     { "mode", definition.settings.mode } };
}

JSON checkpointKey (JSON identity) {
    if (const auto name = identity.find ("name"); name != identity.end () && !name->get<std::string> ().empty ()) {
	identity.erase ("items");
    }
    return identity;
}

std::filesystem::path filename (const std::filesystem::path& directory, const JSON& identity) {
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : checkpointKey (identity).dump ()) hash = (hash ^ c) * 1099511628211ull;
    std::ostringstream name;
    name << std::hex << std::setw (16) << std::setfill ('0') << hash << ".json";
    return directory / name.str ();
}

int64_t timestamp (std::chrono::system_clock::time_point now) {
    return std::chrono::duration_cast<std::chrono::seconds> (now.time_since_epoch ()).count ();
}

bool valid (const State& state, std::size_t size) {
    if (state.order.size () != size || state.orderIndex >= size || !std::isfinite (state.elapsedSeconds)
	|| state.elapsedSeconds < 0) return false;
    std::vector<bool> seen (size);
    for (const auto index : state.order) {
	if (index >= size || seen[index]) return false;
	seen[index] = true;
    }
    return true;
}
}

std::optional<State> restore (
    const std::string& screen, const ApplicationContext::PlaylistDefinition& definition, std::mt19937& rng,
    std::chrono::system_clock::time_point now, const std::filesystem::path& directory
) {
    if (definition.settings.mode != "timer" || definition.items.size () < 2) return std::nullopt;
    const auto storage = stateDirectory (directory);
    if (storage.empty ()) return std::nullopt;
    auto key = identity (screen, definition);
    auto path = filename (storage, key);
    const bool legacy = !std::filesystem::exists (path);
    if (legacy) {
	// V1 lacks authored identity, so it is usable only for an exact item list.
	key.erase ("name");
	path = filename (storage, key);
	if (!std::filesystem::exists (path)) return std::nullopt;
    }
    std::ifstream input (path);
    if (!input) throw std::runtime_error ("Cannot read playlist state");
    const auto saved = JSON::parse (input);
    if (saved.at ("version") != (legacy ? 1 : 2)) return std::nullopt;
    const auto& previous = saved.at ("identity");
    const auto& items = previous.at ("items");
    const auto& currentItems = key.at ("items");
    const bool appended = !legacy && !definition.name.empty () && definition.settings.order == "sequential"
	&& checkpointKey (previous) == checkpointKey (key) && items.is_array () && items.size () >= 2
	&& items.size () < currentItems.size () && std::equal (items.begin (), items.end (), currentItems.begin ());
    if (previous != key && !appended) return std::nullopt;
    State state { saved.at ("order").get<std::vector<std::size_t>> (), saved.at ("index").get<std::size_t> (),
		  saved.at ("elapsed").get<float> () };
    if (!valid (state, items.size ())) return std::nullopt;
    if (appended) {
	for (std::size_t i = 0; i < state.order.size (); i++) {
	    if (state.order[i] != i) return std::nullopt;
	}
	state.order.resize (definition.items.size ());
	std::iota (state.order.begin (), state.order.end (), 0);
    }
    const auto savedAt = saved.at ("savedAt").get<int64_t> ();
    state.elapsedSeconds += static_cast<float> (std::max (0.0, double (timestamp (now)) - double (savedAt)));
    if (!std::isfinite (state.elapsedSeconds)) return std::nullopt;
    const float delayMinutes = std::max<uint32_t> (1, definition.settings.delayMinutes);
    const float elapsedMinutes = state.elapsedSeconds / 60.f;
    if (elapsedMinutes > delayMinutes) {
	state.orderIndex = (state.orderIndex + 1) % state.order.size ();
	if (state.orderIndex == 0 && definition.settings.order == "random") {
	    std::shuffle (state.order.begin (), state.order.end (), rng);
	}
	// Native 14006bd66..14006bd8c stores this minute-valued remainder directly
	// in the elapsed slot; retain that observed restore behavior.
	state.elapsedSeconds = std::fmod (elapsedMinutes, delayMinutes);
    }
    return state;
}

void save (
    const std::string& screen, const ApplicationContext::PlaylistDefinition& definition, const State& state,
    std::chrono::system_clock::time_point now, const std::filesystem::path& directory
) {
    if (definition.settings.mode != "timer" || definition.items.size () < 2) return;
    if (!valid (state, definition.items.size ())) throw std::runtime_error ("Invalid playlist state");
    const auto storage = stateDirectory (directory);
    if (storage.empty ()) return;
    const auto key = identity (screen, definition);
    const auto path = filename (storage, key);
    const auto data = JSON { { "version", 2 }, { "identity", key }, { "order", state.order },
			    { "index", state.orderIndex }, { "elapsed", state.elapsedSeconds },
			    { "savedAt", timestamp (now) } }.dump ();
    std::filesystem::create_directories (storage);
    std::string temporary = path.string () + ".XXXXXX";
    const int fd = mkstemp (temporary.data ());
    if (fd < 0) throw std::system_error (errno, std::generic_category (), "Create playlist state");
    Data::Utils::ScopeGuard cleanup ([&] {
	close (fd);
	std::error_code ignored;
	std::filesystem::remove (temporary, ignored);
    });
    std::size_t written = 0;
    while (written < data.size ()) {
	const auto count = write (fd, data.data () + written, data.size () - written);
	if (count < 0 && errno == EINTR) continue;
	if (count <= 0) throw std::system_error (errno, std::generic_category (), "Write playlist state");
	written += count;
    }
    std::filesystem::rename (temporary, path);
}
}
