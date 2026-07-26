#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace WallpaperEngine::Data::Model {
/**
 * Mesh read from an MDLV container, as referenced by "model" objects in 3D
 * scenes. The container holds one or more submeshes
 * (count in the header), each with its own material and vertex/index data.
 *
 * The vertex tag is an attribute bitmask, not an enum: a position vec3 always
 * leads the vertex and every optional attribute appends its block in a fixed
 * order (normal, tangent, an unidentified 4-byte field, skin indices, skin
 * weights, uv). tag 15 therefore describes a 48-byte stride and 0x0180000f an
 * 80-byte one, the two layouts most scenes ship.
 * See docs/wiki/MDL File Format.md.
 */
/** Offset value marking an attribute the vertex tag did not include */
inline constexpr uint32_t MdlAttributeAbsent = UINT32_MAX;

struct MdlSubmesh {
    /** Material json path embedded in the submesh header */
    std::string materialPath;
    /** Interleaved vertex data */
    std::vector<float> vertices;
    /** Triangle list indices (widened to 32 bit when stored as 16 bit) */
    std::vector<uint32_t> indices;
    /**
     * Attribute bitmask and the layout derived from it. Submeshes of one container can
     * disagree: puppet models pair a skinned body with a channelmap overlay that carries
     * a vec4 texcoord and no weights, so consumers that draw every submesh with a single
     * layout must skip the ones whose tag differs from the mesh's.
     */
    uint32_t vertexTag = 0;
    uint32_t strideBytes = 0;
    uint32_t blendIndicesOffset = MdlAttributeAbsent;
    uint32_t texCoordVec4Offset = MdlAttributeAbsent;
};

struct MdlMesh {
    static constexpr uint32_t AttributeAbsent = MdlAttributeAbsent;
    /** Layout of the first submesh; submeshes with a different tag are not described here. */
    uint32_t vertexTag = 0;
    /** Vertex stride in bytes */
    uint32_t strideBytes = 0;
    /** Byte offsets of each attribute within a vertex, or AttributeAbsent */
    uint32_t positionOffset = 0;
    uint32_t normalOffset = AttributeAbsent;
    uint32_t tangentOffset = AttributeAbsent;
    uint32_t uvOffset = AttributeAbsent;
    /** True when vertices carry four uint bone indices and four float weights. */
    bool skinned = false;
    uint32_t blendIndicesOffset = 0;
    uint32_t blendWeightsOffset = 0;
    /** Union of the per-submesh bounds serialized in the MDLV container. */
    glm::vec3 boundingBoxMin = glm::vec3 (0.0f);
    glm::vec3 boundingBoxMax = glm::vec3 (0.0f);
    bool hasBoundingBox = false;
    std::vector<MdlSubmesh> submeshes;
};
} // namespace WallpaperEngine::Data::Model
