#include "WallpaperEngine/Render/Shaders/ShaderDiskCache.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

using WallpaperEngine::Render::Shaders::ShaderDiskCache;

namespace {
struct CacheDirectory {
    std::filesystem::path path;
    CacheDirectory () {
        char name[] = "/tmp/lwe-shader-cache-XXXXXX";
        const char* created = mkdtemp (name);
        REQUIRE (created != nullptr);
        path = created;
    }
    ~CacheDirectory () {
        std::error_code ignored;
        std::filesystem::remove_all (path, ignored);
    }
    std::filesystem::path entry () const {
        for (const auto& file : std::filesystem::directory_iterator (path)) {
            if (file.path ().extension () == ".bin") return file.path ();
        }
        return {};
    }
};
}

TEST_CASE ("shader disk results survive a separate writer process", "[shader-disk-cache]") {
    CacheDirectory directory;
    const pid_t child = fork ();
    REQUIRE (child >= 0);
    if (child == 0) {
        ShaderDiskCache cache (directory.path, "translator-a");
        _exit (cache.store ("glsl", "vertex+fragment", { "translated vertex", "translated fragment" }) ? 0 : 1);
    }
    int status = 0;
    REQUIRE (waitpid (child, &status, 0) == child);
    REQUIRE (WIFEXITED (status));
    REQUIRE (WEXITSTATUS (status) == 0);
    ShaderDiskCache reader (directory.path, "translator-a");
    const auto result = reader.load ("glsl", "vertex+fragment");
    REQUIRE (result.has_value ());
    CHECK (result->first == "translated vertex");
    CHECK (result->second == "translated fragment");
}

TEST_CASE ("shader disk identity includes stage version and complete source", "[shader-disk-cache]") {
    CacheDirectory directory;
    ShaderDiskCache cache (directory.path, "version-a");
    REQUIRE (cache.store ("compat", "vertex\x1f" "fragment", { "rewritten", "" }));
    CHECK_FALSE (cache.load ("glsl", "vertex\x1f" "fragment"));
    CHECK_FALSE (cache.load ("compat", "changed vertex\x1f" "fragment"));
    CHECK_FALSE (cache.load ("compat", "vertex\x1f" "changed fragment"));
    ShaderDiskCache changedVersion (directory.path, "version-b");
    CHECK_FALSE (changedVersion.load ("compat", "vertex\x1f" "fragment"));
    REQUIRE (cache.load ("compat", "vertex\x1f" "fragment"));
    CHECK (cache.load ("compat", "vertex\x1f" "fragment")->first == "rewritten");
}

TEST_CASE ("damaged shader disk entries are misses and can be replaced", "[shader-disk-cache]") {
    CacheDirectory directory;
    ShaderDiskCache cache (directory.path, "v1");
    REQUIRE (cache.store ("glsl", "source", { "vertex output", "fragment output" }));
    const auto path = directory.entry ();
    REQUIRE_FALSE (path.empty ());
    SECTION ("truncated file") { std::filesystem::resize_file (path, 5); }
    SECTION ("corrupted result with unchanged length") {
        std::fstream file (path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp (-1, std::ios::end);
        file.put ('!');
    }
    SECTION ("impossible header lengths") {
        std::fstream file (path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp (16); // retain the valid magic and key length
        file.write (std::string (8, '\xff').data (), 8);
    }
    SECTION ("different source stored under the expected filename") {
        CacheDirectory other;
        ShaderDiskCache otherCache (other.path, "v1");
        REQUIRE (otherCache.store ("glsl", "sourcf", { "wrong vertex", "wrong fragment" }));
        std::filesystem::copy_file (other.entry (), path, std::filesystem::copy_options::overwrite_existing);
    }
    CHECK_FALSE (cache.load ("glsl", "source"));
    REQUIRE (cache.store ("glsl", "source", { "recompiled vertex", "recompiled fragment" }));
    REQUIRE (cache.load ("glsl", "source"));
    CHECK (cache.load ("glsl", "source")->first == "recompiled vertex");
}

TEST_CASE ("shader disk budget evicts old results including previous versions", "[shader-disk-cache]") {
    CacheDirectory directory;
    ShaderDiskCache old (directory.path, "old", 256);
    REQUIRE (old.store ("glsl", "old-source", { std::string (100, 'a'), "" }));
    ShaderDiskCache current (directory.path, "new", 256);
    REQUIRE (current.store ("glsl", "new-source", { std::string (100, 'b'), "" }));
    CHECK_FALSE (old.load ("glsl", "old-source"));
    REQUIRE (current.load ("glsl", "new-source"));
    CHECK_FALSE (current.store ("glsl", "too-large", { std::string (257, 'x'), "" }));
    REQUIRE (current.load ("glsl", "new-source"));
    uintmax_t bytes = 0;
    for (const auto& file : std::filesystem::directory_iterator (directory.path)) bytes += file.file_size ();
    CHECK (bytes <= 256);
}

TEST_CASE ("unavailable shader cache storage remains optional", "[shader-disk-cache]") {
    CacheDirectory directory;
    const auto blocked = directory.path / "file";
    std::ofstream (blocked) << "not a directory";
    ShaderDiskCache cache (blocked, "v1");
    CHECK_FALSE (cache.load ("glsl", "source"));
    CHECK_FALSE (cache.store ("glsl", "source", { "vertex", "fragment" }));
    ShaderDiskCache disabled ({}, "v1");
    CHECK_FALSE (disabled.store ("glsl", "source", { "vertex", "fragment" }));
    CHECK_FALSE (disabled.load ("glsl", "source"));
}

TEST_CASE ("disabled persistent shader caching bypasses reads and writes without deleting files", "[shader-disk-cache]") {
    CacheDirectory directory;
    ShaderDiskCache cache (directory.path, "v1");
    REQUIRE (cache.store ("glsl", "source", { "vertex", "fragment" }));
    const auto path = directory.entry ();
    const auto modified = std::filesystem::last_write_time (path);
    cache.disable ();
    CHECK_FALSE (cache.load ("glsl", "source"));
    CHECK_FALSE (cache.store ("glsl", "source", { "changed", "changed" }));
    CHECK_FALSE (cache.store ("glsl", "different-source", { "other", "other" }));
    CHECK (std::filesystem::last_write_time (path) == modified);
    ShaderDiskCache enabled (directory.path, "v1");
    REQUIRE (enabled.load ("glsl", "source"));
    CHECK (enabled.load ("glsl", "source")->first == "vertex");
    CHECK_FALSE (enabled.load ("glsl", "different-source"));
}

TEST_CASE ("a busy shader cache writer does not block readers or wallpaper loading", "[shader-disk-cache]") {
    CacheDirectory directory;
    ShaderDiskCache cache (directory.path, "v1");
    REQUIRE (cache.store ("glsl", "source", { "original", "fragment" }));
    const int lock = open ((directory.path / ".lock").c_str (), O_RDWR | O_CLOEXEC);
    REQUIRE (lock >= 0);
    REQUIRE (flock (lock, LOCK_EX | LOCK_NB) == 0);
    CHECK_FALSE (cache.store ("glsl", "source", { "replacement", "fragment" }));
    CHECK (cache.load ("glsl", "source").has_value ());
    close (lock);
    REQUIRE (cache.store ("glsl", "source", { "replacement", "fragment" }));
    REQUIRE (cache.load ("glsl", "source"));
    CHECK (cache.load ("glsl", "source")->first == "replacement");
}
