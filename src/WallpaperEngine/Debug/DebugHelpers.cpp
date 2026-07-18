#include "DebugHelpers.h"

#include "WallpaperEngine/Logging/Log.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace WallpaperEngine;

std::string Debug::dumpText (
    const char* envVar, std::string name, const std::string& extension, const std::string& content) {
    const char* dumpDir = std::getenv (envVar);

    if (dumpDir == nullptr) {
        return {};
    }

    std::error_code ec;
    std::filesystem::create_directories (dumpDir, ec);
    static std::atomic<int> sDumpCounter {0};
    std::ranges::replace (name, '/', '_');
    name += "." + std::to_string (sDumpCounter++) + extension;
    const std::string path = std::string (dumpDir) + "/" + name;
    std::ofstream out (path);
    out << content;
    sLog.out ("Dumped ", path);
    return path;
}
