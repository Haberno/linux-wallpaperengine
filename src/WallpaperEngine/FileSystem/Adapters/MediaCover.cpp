#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>

#include "MediaCover.h"

#include "WallpaperEngine/Assets/AssetLoadException.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Media/MediaSource.h"

using namespace WallpaperEngine::FileSystem;
using namespace WallpaperEngine::FileSystem::Adapters;

ReadStreamSharedPtr MediaCoverAdapter::open (const std::filesystem::path& path) const {
    if (path != "$mediaThumbnail") {
	throw std::filesystem::filesystem_error (
	    "MediaCoverAdapter only supports $mediaThumbnail", path, std::error_code ()
	);
    }

    if (!source.getMediaInfo ().url.has_value ()) {
	throw std::filesystem::filesystem_error ("Media source does not have a valid URL", path, std::error_code ());
    }

    std::string album = *source.getMediaInfo ().url;

    if (album.starts_with ("file://")) {
	album = album.substr (7);
    } else if (album.starts_with ("http://") || album.starts_with ("https://")) {
	// Remote art (e.g. Spotify's https://i.scdn.co/...): fetch it into a per-URL cache file.
	// Blocking with a short timeout, but this only runs when the track's art actually changes.
	if (album.find_first_not_of (
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/.?=&%_~+-"
	    ) != std::string::npos) {
	    throw std::filesystem::filesystem_error (
		"Media cover URL has unexpected characters", album, std::error_code ()
	    );
	}

	const auto cache = std::filesystem::temp_directory_path ()
	    / ("lwe-art-" + std::to_string (std::hash<std::string> {} (album)));

	if (!std::filesystem::exists (cache)) {
	    const std::string command = "curl -fsm 3 -o '" + cache.string () + "' '" + album + "'";
	    if (std::system (command.c_str ()) != 0) {
		std::filesystem::remove (cache);
		throw std::filesystem::filesystem_error ("Cannot download media cover", album, std::error_code ());
	    }
	}

	album = cache.string ();
    } else {
	throw std::filesystem::filesystem_error (
	    "Only file:// and http(s) URLs are supported for media covers", album, std::error_code ()
	);
    }

    std::filesystem::path file = std::filesystem::absolute (album);

    if (std::filesystem::exists (file) == false) {
	throw std::filesystem::filesystem_error ("Media file does not exist", file, std::error_code ());
    }

    if (std::filesystem::is_regular_file (file) == false) {
	throw std::filesystem::filesystem_error ("Media file is not a regular file", file, std::error_code ());
    }

    return std::make_shared<std::ifstream> (file);
}

bool MediaCoverAdapter::exists (const std::filesystem::path& path) const { return path == ""; }

std::filesystem::path MediaCoverAdapter::physicalPath (const std::filesystem::path& path) const {
    sLog.exception ("MediaCoverAdapter does not support realpath");
}

bool MediaCoverFactory::handlesMountpoint (const std::filesystem::path& path) const {
    return path == "$mediaThumbnail";
}

AdapterSharedPtr MediaCoverFactory::create (const std::filesystem::path& path) const {
    if (path != "$mediaThumbnail") {
	sLog.exception ("MediaCoveradapter only supports $mediaThumbnail");
    }

    return std::make_unique<MediaCoverAdapter> (source);
}