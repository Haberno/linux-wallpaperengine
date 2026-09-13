#include "ShaderDiskCache.h"
#include "ShaderCacheVersion.generated.h"
#include "WallpaperEngine/Data/Utils/ScopeGuard.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/file.h>
#include <unistd.h>
#include <vector>
#include <zlib.h>

using namespace WallpaperEngine::Render::Shaders;
namespace fs = std::filesystem;
namespace {
constexpr uint64_t MAGIC = 0x4c57455348445201ULL;
constexpr size_t MAX_ENTRY_BYTES = 64ULL * 1024 * 1024;
using Header = std::array<uint64_t, 5>; // magic, key/vertex/fragment sizes, checksum

std::string filename (const std::string& key) {
    // Stable bucket name; the complete identity is verified on every read.
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : key) hash = (hash ^ byte) * 1099511628211ULL;
    std::ostringstream out;
    out << std::hex << std::setw (16) << std::setfill ('0') << hash << ".bin";
    return out.str ();
}

uint64_t checksum (const Header& header, const std::string& payload) {
    const auto crc = crc32 (0, reinterpret_cast<const Bytef*> (header.data ()), sizeof (uint64_t) * 4);
    return crc32 (crc, reinterpret_cast<const Bytef*> (payload.data ()), static_cast<uInt> (payload.size ()));
}

fs::path cacheDirectory () {
    if (const char* cache = std::getenv ("XDG_CACHE_HOME"); cache && cache[0] == '/') {
        return fs::path (cache) / "linux-wallpaperengine/shaders";
    }
    if (const char* home = std::getenv ("HOME"); home && home[0] == '/') {
        return fs::path (home) / ".cache/linux-wallpaperengine/shaders";
    }
    return {};
}
}

ShaderDiskCache::ShaderDiskCache (std::filesystem::path directory, std::string version, size_t budget)
    : m_directory (std::move (directory)), m_version (std::move (version)), m_budget (budget) { }

ShaderDiskCache& ShaderDiskCache::get () {
    static ShaderDiskCache cache (cacheDirectory (), SHADER_CACHE_VERSION);
    return cache;
}

std::string ShaderDiskCache::identity (std::string_view stage, const std::string& key) const {
    return m_version + '\0' + std::string (stage) + '\0' + key;
}

std::optional<ShaderDiskCache::Result> ShaderDiskCache::load (std::string_view stage, const std::string& key) const {
    if (!m_enabled || m_directory.empty () || key.size () > MAX_ENTRY_BYTES) return {};
    const auto id = identity (stage, key);
    std::ifstream input (m_directory / filename (id), std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto size = input.tellg ();
    if (size < static_cast<std::streamoff> (sizeof (Header))
        || size > static_cast<std::streamoff> (std::min (m_budget, MAX_ENTRY_BYTES))) return {};
    input.seekg (0);
    Header header {};
    input.read (reinterpret_cast<char*> (header.data ()), sizeof (header));
    const auto payloadSize = static_cast<size_t> (size) - sizeof (header);
    // Subtractions are guarded to avoid overflow from damaged length fields.
    if (!input || header[0] != MAGIC || header[1] != id.size () || header[1] > payloadSize
        || header[2] > payloadSize - header[1] || header[3] != payloadSize - header[1] - header[2]) return {};
    std::string payload (payloadSize, '\0');
    input.read (payload.data (), payload.size ());
    if (!input || header[4] != checksum (header, payload) || payload.compare (0, id.size (), id) != 0) return {};
    return Result { payload.substr (header[1], header[2]), payload.substr (header[1] + header[2], header[3]) };
}

bool ShaderDiskCache::store (std::string_view stage, const std::string& key, const Result& result) const {
    const auto limit = std::min (m_budget, MAX_ENTRY_BYTES);
    if (!m_enabled || m_directory.empty () || key.size () > limit || result.first.size () > limit || result.second.size () > limit)
        return false;
    const auto id = identity (stage, key);
    const size_t bytes = sizeof (Header) + id.size () + result.first.size () + result.second.size ();
    if (bytes > limit) return false;
    std::error_code error;
    fs::create_directories (m_directory, error);
    if (error) return false;
    // Writers share a budget across versions/processes. Contention just skips a
    // cache write; loading the wallpaper must never wait for another process.
    const int lock = open ((m_directory / ".lock").c_str (), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0) return false;
    WallpaperEngine::Data::Utils::ScopeGuard unlock ([&] { close (lock); });
    if (flock (lock, LOCK_EX | LOCK_NB) != 0) return false;
    const auto target = m_directory / filename (id);
    struct Entry { fs::file_time_type time; fs::path path; uintmax_t size; };
    std::vector<Entry> entries;
    uintmax_t total = 0;
    for (fs::directory_iterator it (m_directory, error), end; !error && it != end; it.increment (error)) {
        const auto path = it->path ();
        if (path.filename ().string ().starts_with (".tmp-")) {
            fs::remove (path, error); // unfinished write from an earlier process
        } else if (path.extension () == ".bin" && path != target) {
            const auto size = it->file_size (error);
            if (error) return false;
            const auto time = it->last_write_time (error);
            if (error) return false;
            entries.push_back ({ time, path, size });
            total += size;
        }
    }
    if (error) return false;
    std::ranges::sort (entries, {}, &Entry::time);
    for (const auto& entry : entries) {
        if (total + bytes <= m_budget) break;
        fs::remove (entry.path, error);
        if (error) return false;
        total -= entry.size;
    }

    std::string temporary = (m_directory / ".tmp-XXXXXX").string ();
    const int descriptor = mkostemp (temporary.data (), O_CLOEXEC);
    if (descriptor < 0) return false;
    WallpaperEngine::Data::Utils::ScopeGuard cleanup ([&] { unlink (temporary.c_str ()); });
    FILE* output = fdopen (descriptor, "wb");
    if (!output) { close (descriptor); return false; }
    const std::string payload = id + result.first + result.second;
    Header header { MAGIC, id.size (), result.first.size (), result.second.size (), 0 };
    header[4] = checksum (header, payload);
    const bool written = fwrite (header.data (), 1, sizeof (header), output) == sizeof (header)
        && fwrite (payload.data (), 1, payload.size (), output) == payload.size ();
    const bool closed = fclose (output) == 0;
    if (!written || !closed) return false;
    fs::rename (temporary, target, error); // readers see a complete old or new file
    return !error;
}
