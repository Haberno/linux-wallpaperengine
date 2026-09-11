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

std::filesystem::path resolve (const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code error;
    auto finalpath = std::filesystem::canonical (root / path, error);
    if (error) {
	// Loose workshop assets use Windows filenames too. Prefer exact spelling at every
	// level, and only scan a directory when that component does not exist.
	const auto relative = (root / path).lexically_normal ().lexically_relative (root);
	if (relative.empty () || *relative.begin () == "..") {
	    throw std::filesystem::filesystem_error ("Cannot find file", path, std::error_code ());
	}
	finalpath = root;
	for (const auto& component : relative) {
	    const auto exact = finalpath / component;
	    if (std::filesystem::exists (exact)) {
		finalpath = exact;
		continue;
	    }
	    std::filesystem::path match;
	    const auto folded = lowercase (component.string ());
	    for (const auto& entry : std::filesystem::directory_iterator (finalpath)) {
		if (lowercase (entry.path ().filename ().string ()) == folded
		    && (match.empty () || entry.path () < match)) {
		    match = entry.path ();
		}
	    }
	    if (match.empty ()) {
		throw std::filesystem::filesystem_error ("Cannot find file", path, std::error_code ());
	    }
	    finalpath = match;
	}
	finalpath = std::filesystem::canonical (finalpath);
    }
    const auto relative = finalpath.lexically_relative (root);
    if (relative.empty () || *relative.begin () == "..") {
	throw std::filesystem::filesystem_error ("Cannot find file", path, std::error_code ());
    }
    return finalpath;
}
} // namespace

ReadStreamSharedPtr DirectoryAdapter::open (const std::filesystem::path& path) const {
    const auto finalpath = resolve (this->basepath, path);

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
    try {
	const auto finalpath = resolve (this->basepath, path);

	const auto status = std::filesystem::status (finalpath);

	if (!std::filesystem::exists (finalpath)) {
	    return false;
	}

	if (!std::filesystem::is_regular_file (status)) {
	    return false;
	}

	return true;
    } catch (std::filesystem::filesystem_error&) {
	return false;
    }
}

std::filesystem::path DirectoryAdapter::physicalPath (const std::filesystem::path& path) const {
    return resolve (this->basepath, path);
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
