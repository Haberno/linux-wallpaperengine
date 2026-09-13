#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace WallpaperEngine::Render::Shaders {
class ShaderDiskCache {
public:
    using Result = std::pair<std::string, std::string>;
    ShaderDiskCache (std::filesystem::path directory, std::string version, size_t budget = 256ULL * 1024 * 1024);
    [[nodiscard]] std::optional<Result> load (std::string_view stage, const std::string& key) const;
    bool store (std::string_view stage, const std::string& key, const Result& result) const;
    /** Called during startup, before any shader work begins. */
    void disable () { m_enabled = false; }
    static ShaderDiskCache& get ();

private:
    std::string identity (std::string_view stage, const std::string& key) const;
    std::filesystem::path m_directory;
    std::string m_version;
    size_t m_budget;
    bool m_enabled = true;
};
}
