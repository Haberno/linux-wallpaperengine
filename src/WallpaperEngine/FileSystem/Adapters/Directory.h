#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Types.h"

namespace WallpaperEngine::FileSystem::Adapters {
struct DirectoryFactory final : Factory {
    DirectoryFactory () = default;

    [[nodiscard]] bool handlesMountpoint (const std::filesystem::path& path) const override;
    [[nodiscard]] AdapterSharedPtr create (const std::filesystem::path& path) const override;
};

struct DirectoryAdapter final : Adapter {
    explicit DirectoryAdapter (std::filesystem::path path) : basepath (std::move (path)) { }

    [[nodiscard]] ReadStreamSharedPtr open (const std::filesystem::path& path) const override;
    [[nodiscard]] bool exists (const std::filesystem::path& path) const override;
    [[nodiscard]] std::filesystem::path physicalPath (const std::filesystem::path& path) const override;

    const std::filesystem::path basepath;

  private:
    struct Listing {
	std::unordered_set<std::string> names;
	/** lowercase name -> actual name */
	std::unordered_map<std::string, std::string> folded;
    };

    [[nodiscard]] std::optional<std::filesystem::path> tryResolve (const std::filesystem::path& path) const;
    /** caller holds m_resolvedMutex */
    [[nodiscard]] const Listing& listing (const std::filesystem::path& directory) const;

    // ponytail: never invalidated; containers live per wallpaper and mounted trees are treated as read-only
    mutable std::mutex m_resolvedMutex;
    mutable std::unordered_map<std::string, std::optional<std::filesystem::path>> m_resolved;
    mutable std::unordered_map<std::string, Listing> m_listings;
};
}
