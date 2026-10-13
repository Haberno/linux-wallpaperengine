#pragma once

#include <cstdint>
#include <vector>
#include <glm/vec4.hpp>

struct FT_FaceRec_;

namespace WallpaperEngine::Render::Objects {
struct TextMsdfGlyph {
    glm::ivec4 inkBounds { 0 }; // xMin, yMin, xMax, yMax in original hinted font pixels
    int width = 0;
    int height = 0;
    float margin = 0;
    std::vector<uint8_t> pixels;
};

/** Convert the currently loaded non-color outline using native projection/correction. */
TextMsdfGlyph generateTextMsdfGlyph (FT_FaceRec_* face);
}
