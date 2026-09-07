#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/FileSystem/Adapters/Package.h"
#include "WallpaperEngine/Assets/AssetLoadException.h"
#include "WallpaperEngine/Assets/AssetLocator.h"

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

TEST_CASE ("short workshop shader paths fail normally and valid overrides still resolve", "[assets]") {
    auto files = std::make_unique<WallpaperEngine::FileSystem::Container> ();
    auto& vfs = files->getVFS ();
    vfs.add ("shaders/generic.frag", "ordinary shader");
    vfs.add ("shaders/workshop/123/effect/example.frag", "authored shader");
    vfs.add ("zcompat/scene/shaders/123/example.frag", "compatibility shader");
    vfs.add ("shaders/workshop/456/effect/example.frag", "fallback shader");
    const WallpaperEngine::Assets::AssetLocator locator (std::move (files));

    for (const auto* path : { "", "workshop", "workshop/123", "workshop/123/", "workshop/123/missing.frag" }) {
	INFO (path);
	REQUIRE_THROWS_AS (locator.fragmentShader (path), WallpaperEngine::Assets::AssetLoadException);
    }
    CHECK (locator.fragmentShader ("generic.frag") == "ordinary shader");
    CHECK (locator.fragmentShader ("workshop/123/effect/example.frag") == "compatibility shader");
    CHECK (locator.fragmentShader ("workshop/456/effect/example.frag") == "fallback shader");
}
