#include <algorithm>

#include "ProjectParser.h"
#include "WallpaperEngine/Logging/Log.h"

#include "WallpaperParser.h"

#include "PropertyParser.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/FileSystem/Container.h"

using namespace WallpaperEngine::Data::Parsers;

static int backgroundId = 0;

ProjectUniquePtr ProjectParser::parse (const JSON& data, AssetLocatorUniquePtr container) {
    const auto general = data.optional ("general");
    const auto workshopId = data.optional ("workshopid");
    auto actualWorkshopId = std::to_string (--backgroundId);
    const auto maybeType = data.optional<std::string> ("type");

    if (!maybeType.has_value ()) {
	// editor assets (effects, models, particle systems...) ship a project.json with
	// category "Asset" and no "type"; they are imported into other wallpapers by the
	// Wallpaper Engine editor and have nothing to render standalone, so give a clearer
	// explanation than the generic missing-key error
	if (data.optional ("category", std::string {}) == "Asset") {
	    sLog.exception (
		"Workshop item \"", data.optional ("title", std::string {"<untitled>"}),
		"\" is an editor asset (category \"Asset\", ", data.optional ("file", std::string {"<no file>"}),
		"), not a wallpaper - it can only be imported into other wallpapers and cannot be loaded standalone");
	}

	sLog.exception ("Project type missing. Contents: ", data.dump ());
    }

    auto type = *maybeType;

    if (workshopId.has_value ()) {
	if (workshopId->is_number ()) {
	    actualWorkshopId = std::to_string (workshopId->get<int> ());
	} else if (workshopId->is_string ()) {
	    actualWorkshopId = workshopId->get<std::string> ();
	} else {
	    sLog.error ("Invalid workshop id: ", workshopId->dump ());
	}
    }

    // lowercase for consistency
    std::ranges::transform (type, type.begin (), tolower);

    auto result = std::make_unique<Project> (Project {
	.title = data.require<std::string> ("title", "Project title missing"),
	.type = parseType (type),
	.workshopId = actualWorkshopId,
	.supportsAudioProcessing = general.has_value () && general.value ().optional ("supportsaudioprocessing", false),
	.properties = parseProperties (general),
	.assetLocator = std::move (container),
    });

    result->wallpaper = WallpaperParser::parse (data.require ("file", "Project's main file missing"), *result);

    return result;
}

Project::Type ProjectParser::parseType (const std::string& type) {
    if (type == "scene") {
	return Project::Type_Scene;
    }

    if (type == "video") {
	return Project::Type_Video;
    }

    if (type == "web") {
	return Project::Type_Web;
    }

    sLog.exception ("Unsupported project type ", type);
}

Properties ProjectParser::parseProperties (const std::optional<JSON>& data) {
    if (!data.has_value ()) {
	return {};
    }

    const auto properties = data.value ().optional ("properties");

    if (!properties.has_value ()) {
	return {};
    }

    Properties result = {};

    for (const auto& cur : properties.value ().items ()) {
	const auto& property = PropertyParser::parse (cur.value (), cur.key ());

	// ignore properties that failed, these are generally groups
	if (property == nullptr) {
	    continue;
	}

	result.emplace (cur.key (), property);
    }

    return result;
}
