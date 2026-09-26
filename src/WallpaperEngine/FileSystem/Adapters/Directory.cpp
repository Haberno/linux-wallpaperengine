#include <filesystem>
#include <fstream>
#include <memory>
#include <algorithm>
#include <cctype>

#include "Directory.h"

#include "WallpaperEngine/Assets/AssetLoadException.h"

using namespace WallpaperEngine::FileSystem;
using namespace WallpaperEngine::FileSystem::Adapters;

namespace {
std::string lowercase (std::string value) {
    std::ranges::transform (value, value.begin (), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return value;
}

} // namespace

const DirectoryAdapter::Listing& DirectoryAdapter::listing (const std::filesystem::path& directory) const {
    const auto [it, inserted] = this->m_listings.try_emplace (directory.string ());
    if (inserted) {
	std::error_code error;
	for (auto entry = std::filesystem::directory_iterator (directory, error);
	     !error && entry != std::filesystem::directory_iterator (); entry.increment (error)) {
	    auto name = entry->path ().filename ().string ();
	    // several spellings can fold together; the smallest one wins, deterministically
	    const auto [folded, fresh] = it->second.folded.try_emplace (lowercase (name), name);
	    if (!fresh && name < folded->second) {
		folded->second = name;
	    }
	    it->second.names.insert (std::move (name));
	}
    }
    return it->second;
}

std::optional<std::filesystem::path> DirectoryAdapter::tryResolve (const std::filesystem::path& path) const {
    // Every lookup probes each mount in order, so one missing file repeats across ~50 stock
    // effect mounts. Resolving against the disk each time cost ~1.7M syscalls per scene load;
    // walking cached listings makes a miss free and leaves canonical () for real hits only.
    const auto key = path.string ();
    const auto relative = (this->basepath / path).lexically_normal ().lexically_relative (this->basepath);
    if (relative.empty () || *relative.begin () == "..") {
	return std::nullopt;
    }

    auto finalpath = this->basepath;
    {
	// listings are read under the lock, but each directory only once per adapter
	const std::lock_guard lock (this->m_resolvedMutex);
	if (const auto it = this->m_resolved.find (key); it != this->m_resolved.end ()) {
	    return it->second;
	}
	// Loose workshop assets use Windows filenames too: prefer the exact spelling at every
	// level and fall back to a case-insensitive match only when it does not exist
	for (const auto& component : relative) {
	    const auto& entries = this->listing (finalpath);
	    const auto name = component.string ();
	    if (entries.names.contains (name)) {
		finalpath /= component;
		continue;
	    }
	    const auto match = entries.folded.find (lowercase (name));
	    if (match == entries.folded.end ()) {
		this->m_resolved.try_emplace (key, std::nullopt);
		return std::nullopt;
	    }
	    finalpath /= match->second;
	}
    }

    // canonical () so symlinks cannot escape the mount
    std::error_code error;
    std::optional<std::filesystem::path> result = std::filesystem::canonical (finalpath, error);
    const auto inside = result->lexically_relative (this->basepath);
    if (error || inside.empty () || *inside.begin () == "..") {
	result.reset ();
    }
    const std::lock_guard lock (this->m_resolvedMutex);
    return this->m_resolved.try_emplace (key, std::move (result)).first->second;
}

ReadStreamSharedPtr DirectoryAdapter::open (const std::filesystem::path& path) const {
    const auto resolved = this->tryResolve (path);
    if (!resolved) {
	throw std::filesystem::filesystem_error ("Cannot find file", path, std::error_code ());
    }
    const auto& finalpath = *resolved;

    const auto status = std::filesystem::status (finalpath);

    if (!std::filesystem::exists (finalpath)) {
	throw std::filesystem::filesystem_error ("Cannot find file", path, std::error_code ());
    }

    if (!std::filesystem::is_regular_file (status)) {
	throw std::filesystem::filesystem_error ("Expected file but found a directory", path, std::error_code ());
    }

    return std::make_shared<std::ifstream> (finalpath, std::ios::binary);
}

bool DirectoryAdapter::exists (const std::filesystem::path& path) const {
    const auto finalpath = this->tryResolve (path);
    std::error_code error;
    return finalpath && std::filesystem::is_regular_file (*finalpath, error);
}

std::filesystem::path DirectoryAdapter::physicalPath (const std::filesystem::path& path) const {
    auto finalpath = this->tryResolve (path);
    if (!finalpath) {
	throw std::filesystem::filesystem_error ("Cannot find file", path, std::error_code ());
    }
    return std::move (*finalpath);
}

bool DirectoryFactory::handlesMountpoint (const std::filesystem::path& path) const {
    try {
	const auto finalpath = std::filesystem::canonical (path);
	const auto status = std::filesystem::status (finalpath);

	return std::filesystem::exists (finalpath) && std::filesystem::is_directory (status);
    } catch (std::filesystem::filesystem_error&) {
	return false;
    }
}

AdapterSharedPtr DirectoryFactory::create (const std::filesystem::path& path) const {
    auto finalpath = std::filesystem::canonical (path);
    const auto status = std::filesystem::status (finalpath);

    if (!std::filesystem::exists (finalpath)) {
	throw std::filesystem::filesystem_error ("Cannot find directory", path, std::error_code ());
    }

    if (!std::filesystem::is_directory (status)) {
	throw std::filesystem::filesystem_error ("Expected directory but found a file", path, std::error_code ());
    }

    return std::make_unique<DirectoryAdapter> (finalpath);
}
