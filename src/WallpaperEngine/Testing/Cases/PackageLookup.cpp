#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/FileSystem/Adapters/Package.h"

using namespace WallpaperEngine::Data::Assets;
using WallpaperEngine::FileSystem::Adapters::PackageAdapter;

namespace {
PackageAdapter buildAdapter (const std::initializer_list<std::string> filenames) {
    auto package = std::make_unique<Package> ();

    for (const auto& filename : filenames) {
	package->files.push_back (std::make_unique<FileEntry> (filename, 0, 0));
    }

    return PackageAdapter (std::move (package));
}
} // namespace

TEST_CASE ("package lookups ignore case") {
    // workshop 2719499501 stores "Sounds/01 Overworld (MM).mp3" but its scene references
    // "sounds/...", and 2935233995 asks for "models/Rayman.json" against "models/rayman.json".
    // both are fatal on a case-sensitive lookup even though the file is right there
    const auto adapter = buildAdapter ({"Sounds/01 Overworld (MM).mp3", "models/rayman.json"});

    REQUIRE (adapter.exists ("Sounds/01 Overworld (MM).mp3"));
    REQUIRE (adapter.exists ("sounds/01 Overworld (MM).mp3"));
    REQUIRE (adapter.exists ("models/Rayman.json"));
    REQUIRE (adapter.exists ("MODELS/RAYMAN.JSON"));

    REQUIRE_FALSE (adapter.exists ("sounds/missing.mp3"));
    REQUIRE_FALSE (adapter.exists ("rayman.json"));
}
