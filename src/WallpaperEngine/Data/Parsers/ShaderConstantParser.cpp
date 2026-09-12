#include "ShaderConstantParser.h"

#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Parsers/UserSettingParser.h"

#include <algorithm>
#include <sstream>

using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Model;

ShaderConstantMap ShaderConstantParser::parse (const JSON& it, const Project& project) {
    if (!it.is_object ()) {
	return {};
    }

    ShaderConstantMap result = {};

    for (const auto& cur : it.items ()) {
	auto data = cur.value ();
	auto* value = data.is_object () && data.contains ("value") ? &data["value"] : &data;
	if (value->is_string ()) {
	    std::string text = *value;
	    if (text.find (',') != std::string::npos) {
		// Custom effects also serialize numeric vectors as "0.0, 360.0".
		// Normalize only complete vectors here, keeping ordinary dynamic text unchanged.
		std::replace (text.begin (), text.end (), ',', ' ');
		std::istringstream components (text);
		float component;
		int count = 0;
		while (components >> component) ++count;
		if (components.eof () && count >= 2 && count <= 4) {
		    std::istringstream words (text);
		    std::string word, normalized;
		    while (words >> word) {
			if (!normalized.empty ()) normalized += ' ';
			normalized += word;
		    }
		    *value = std::move (normalized);
		}
	    }
	}
	result.emplace (cur.key (), UserSettingParser::parse (data, project.properties));
    }

    return result;
}
