#pragma once

#include "ApplicationContext.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace WallpaperEngine::Application::PlaylistState {
struct State {
    std::vector<std::size_t> order;
    std::size_t orderIndex = 0;
    float elapsedSeconds = 0;
};

std::optional<State> restore (
    const std::string& screen, const ApplicationContext::PlaylistDefinition& definition, std::mt19937& rng,
    std::chrono::system_clock::time_point now, const std::filesystem::path& directory = {}
);
void save (
    const std::string& screen, const ApplicationContext::PlaylistDefinition& definition, const State& state,
    std::chrono::system_clock::time_point now, const std::filesystem::path& directory = {}
);
}
