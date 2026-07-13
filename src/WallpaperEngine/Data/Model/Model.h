#pragma once

#include <optional>
#include <string>

#include <glm/vec2.hpp>
#include "Types.h"

namespace WallpaperEngine::Data::Model {
// TODO: FIND A BETTER NAMING SO THIS DOESN'T COLLIDE WITH THE NAMESPACE ITSELF
struct ModelStruct {
    /** The filename of the model */
    std::string filename;
    /** The material used for this model */
    MaterialUniquePtr material;
    /** Whether this model is a solid layer */
    bool solidlayer;
    /** Whether this model is a fullscreen layer */
    bool fullscreen;
    /** Whether this model is a passthrough layer */
    bool passthrough;
    /** Whether this models's size should be determined automatically or not */
    bool autosize;
    /** Whether this models's padding should be disabled or not */
    bool nopadding;
    /** Not sure what's used for */
    std::optional<int> width;
    /** Not sure what's used for */
    std::optional<int> height;
    /** UV pixel offset into the texture when autosize is true (sprite-sheet crop origin) */
    std::optional<glm::vec2> cropoffset;
    /** Model file for puppet */
    std::optional<std::string> puppet;
};
} // namespace WallpaperEngine::Data::Model