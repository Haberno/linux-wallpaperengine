#include "JSON.h"

#include "WallpaperEngine/Data/Parsers/UserSettingParser.h"

#include "WallpaperEngine/Logging/Log.h"

#include <cctype>

using namespace WallpaperEngine::Data::JSON;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;

namespace {
std::string removeTrailingCommas (const std::string_view contents, bool& changed) {
    std::string repaired;
    repaired.reserve (contents.size ());
    bool inString = false;
    bool escaped = false;

    for (size_t index = 0; index < contents.size (); index++) {
	const char current = contents[index];

	if (inString) {
	    repaired.push_back (current);
	    if (escaped) {
		escaped = false;
	    } else if (current == '\\') {
		escaped = true;
	    } else if (current == '"') {
		inString = false;
	    }
	    continue;
	}

	if (current == '"') {
	    inString = true;
	    repaired.push_back (current);
	    continue;
	}

	if (current == ',') {
	    size_t next = index + 1;
	    while (next < contents.size () && std::isspace (static_cast<unsigned char> (contents[next]))) {
		next++;
	    }
	    if (next < contents.size () && (contents[next] == ']' || contents[next] == '}')) {
		changed = true;
		continue;
	    }
	}

	repaired.push_back (current);
    }

    return repaired;
}
}

JSON WallpaperEngine::Data::JSON::parseCompatible (const std::string_view contents, const std::string_view source) {
    try {
	return JSON::parse (contents);
    } catch (const JSON::parse_error&) {
	bool changed = false;
	auto repaired = removeTrailingCommas (contents, changed);
	if (!changed) {
	    throw;
	}

	auto result = JSON::parse (repaired);
	sLog.out (
	    "Accepted Wallpaper Engine JSON trailing comma", source.empty () ? "" : " in ",
	    source.empty () ? "" : std::string (source)
	);
	return result;
    }
}

UserSettingUniquePtr JsonExtensions::user (const std::string& key, const Properties& properties) const {
    const auto value = this->require (key, "User setting without default value must be present");

    return UserSettingParser::parse (value, properties);
}

UserSettingUniquePtr JsonExtensions::color (const std::string& key, const Properties& properties) const {
    const auto value = this->require (key, "User setting without default value must be present");

    return UserSettingParser::parse (value, properties, true);
}
