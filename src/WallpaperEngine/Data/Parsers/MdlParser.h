#pragma once

#include <string>
#include <vector>

#include "WallpaperEngine/Data/Model/MdlMesh.h"

namespace WallpaperEngine::Data::Model {
struct Project;
}

namespace WallpaperEngine::Data::Parsers {
using namespace WallpaperEngine::Data::Model;

/**
 * Reads static meshes out of MDLV model containers ("model" objects in 3D
 * scenes). Skinned puppet meshes keep their own loader in CImage.
 */
class MdlParser {
public:
    static MdlMesh load (const Project& project, const std::string& filename);
    static MdlMesh parse (const std::vector<char>& data, const std::string& filename);
};
} // namespace WallpaperEngine::Data::Parsers
