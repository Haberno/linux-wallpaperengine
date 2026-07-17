#pragma once

#include <vector>

#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/CameraPath.h"

namespace WallpaperEngine::Data::Parsers {
class CameraPathParser {
public:
    [[nodiscard]] static std::vector<Model::CameraPath> parse (const WallpaperEngine::Data::JSON::JSON& data);
};
} // namespace WallpaperEngine::Data::Parsers
