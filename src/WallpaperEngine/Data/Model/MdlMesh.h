#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace WallpaperEngine::Data::Model {
/**
 * Mesh read from an MDLV container, as referenced by "model" objects in 3D
 * scenes. The container holds one or more submeshes
 * (count in the header), each with its own material and vertex/index data.
 * Supported vertex layouts:
 * - tag 15: position vec3 @0, normal vec3 @12, tangent vec4 @24,
 *   uv vec2 @40, 48-byte stride.
 * - tag 0x0180000f: same visible attributes, skin indices/weights between
 *   tangent and uv, uv vec2 @72, 80-byte stride.
 * See docs/wiki/MDL File Format.md.
 */
struct MdlSubmesh {
    /** Material json path embedded in the submesh header */
    std::string materialPath;
    /** Interleaved vertex data */
    std::vector<float> vertices;
    /** Triangle list indices (widened to 32 bit when stored as 16 bit) */
    std::vector<uint32_t> indices;
};

struct MdlMesh {
    /** Vertex stride in bytes */
    uint32_t strideBytes = 0;
    /** Byte offsets of each attribute within a vertex */
    uint32_t positionOffset = 0;
    uint32_t normalOffset = 0;
    uint32_t tangentOffset = 0;
    uint32_t uvOffset = 0;
    /** True when vertices carry four uint bone indices and four float weights. */
    bool skinned = false;
    uint32_t blendIndicesOffset = 0;
    uint32_t blendWeightsOffset = 0;
    std::vector<MdlSubmesh> submeshes;
};
} // namespace WallpaperEngine::Data::Model
