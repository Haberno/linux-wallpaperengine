#pragma once

#include <string>
#include <vector>

#include "WallpaperEngine/Data/Model/MdlAnimation.h"

namespace WallpaperEngine::Data::Model {
struct Project;
}

namespace WallpaperEngine::Data::Parsers {
using namespace WallpaperEngine::Data::Model;

class MdlAnimationParser {
public:
    [[nodiscard]] static MdlAnimationData load (const Project& project, const std::string& filename);
    [[nodiscard]] static MdlAnimationData parse (const std::vector<char>& data, const std::string& filename);
};
} // namespace WallpaperEngine::Data::Parsers
