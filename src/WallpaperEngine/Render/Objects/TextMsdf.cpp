#include "TextMsdf.h"

#include <algorithm>
#include <cmath>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include <msdfgen.h>
#include <msdfgen-ext.h>

using namespace WallpaperEngine::Render::Objects;

TextMsdfGlyph WallpaperEngine::Render::Objects::generateTextMsdfGlyph (FT_Face face) {
    TextMsdfGlyph result;
    if (FT_HAS_COLOR (face) || face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return result;
    FT_Glyph glyph;
    if (FT_Get_Glyph (face->glyph, &glyph) != 0) return result;
    FT_BBox bounds;
    FT_Glyph_Get_CBox (glyph, FT_GLYPH_BBOX_PIXELS, &bounds);
    FT_Done_Glyph (glyph);
    result.inkBounds = { bounds.xMin, bounds.yMin, bounds.xMax, bounds.yMax };
    const float inkWidth = static_cast<float> (bounds.xMax - bounds.xMin);
    const float inkHeight = static_cast<float> (bounds.yMax - bounds.yMin);
    if (inkWidth <= 0 || inkHeight <= 0) return result;
    const float ppem = std::max<unsigned> (face->size->metrics.y_ppem, 1);
    const float pixelScale = 32.0f / ppem;
    const int width = std::max (1, static_cast<int> (std::floor (inkWidth * pixelScale)));
    const int height = std::max (1, static_cast<int> (std::floor (inkHeight * pixelScale)));
    const float scale = std::max (width, height) / std::max (inkWidth, inkHeight);
    const float distanceRange = 24.0f / scale;
    const float translateX = 12.0f / scale - static_cast<float> (bounds.xMin);
    const float translateY = 12.0f / scale - static_cast<float> (bounds.yMin);
    msdfgen::Shape shape;
    if (msdfgen::readFreetypeOutline (shape, &face->glyph->outline, 1.0 / 64) != 0) return result;
    shape.normalize ();
    shape.orientContours ();
    msdfgen::edgeColoringSimple (shape, 3.0, 0);
    result.width = width + 24;
    result.height = height + 24;
    result.margin = ppem * (12.0f / 32);
    msdfgen::Bitmap<float, 3> image (result.width, result.height);
    msdfgen::ErrorCorrectionConfig correction (
        msdfgen::ErrorCorrectionConfig::EDGE_PRIORITY, msdfgen::ErrorCorrectionConfig::CHECK_DISTANCE_AT_EDGE,
        10.0 / 9, 10.0 / 9
    );
    msdfgen::generateMSDF (
        image, shape, msdfgen::Projection (msdfgen::Vector2 (scale), msdfgen::Vector2 (translateX, translateY)),
        msdfgen::Range (distanceRange), msdfgen::MSDFGeneratorConfig (true, correction)
    );
    result.pixels.resize (static_cast<size_t> (result.width) * result.height * 4);
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            const auto source = image (x, result.height - y - 1);
            auto* destination = &result.pixels[(static_cast<size_t> (y) * result.width + x) * 4];
            for (int c = 0; c < 3; ++c) {
                destination[c] = static_cast<uint8_t> (std::clamp (static_cast<int> (source[c] * 256), 0, 255));
            }
            destination[3] = 255;
        }
    }
    return result;
}
