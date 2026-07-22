#pragma once

#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Types.h"

namespace WallpaperEngine::Data::Parsers {
using JSON = WallpaperEngine::Data::JSON::JSON;
using namespace WallpaperEngine::Data::Model;

class MaterialParser {
public:
    static MaterialUniquePtr load (
	const Project& project, const std::string& filename, bool modelDepthDefaults = false
    );
    static MaterialUniquePtr parse (
	const JSON& it, const std::string& filename, const Project& project, bool modelDepthDefaults = false
    );
    static std::vector<MaterialPassUniquePtr>
    parsePasses (const JSON& it, const Project& project, bool modelDepthDefaults = false);
    static MaterialPassUniquePtr
    parsePass (const JSON& it, const Project& project, bool modelDepthDefaults = false);
    static std::map<std::string, int> parseCombos (const JSON& it);
    static BlendingMode parseBlendMode (const std::string& mode);
    static CullingMode parseCullMode (const std::string& mode);
    static DepthtestMode parseDepthtestMode (const std::string& mode);
    static DepthwriteMode parseDepthwriteMode (const std::string& mode);
};
} // namespace WallpaperEngine::Data::Parsers
