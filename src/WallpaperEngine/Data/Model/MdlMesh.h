#pragma once

#include <cstdint>
#include <optional>
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

/** Optional MDMP modifier fields; rule interpretation belongs to the renderer. */
struct MdlMorphModifier {
    uint32_t boneIndex = 0;
    uint32_t rule = 0;
    glm::vec2 range = glm::vec2 (0.0f);
};

/** MDMP0001 target streams retain their signed 16-bit components without normalization. */
struct MdlMorphTarget {
    uint64_t id = 0;
    std::string name;
    std::vector<int16_t> positionDeltas;
    std::vector<int16_t> normalDeltas;
    std::vector<int16_t> tangentDeltas;
    std::vector<int16_t> alphaValues;
    std::optional<MdlMorphModifier> modifier;
};

struct MdlDrawRange;
struct MdlClippingDescriptor;

struct MdlSubmesh {
    uint32_t flags = 0;
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
    uint32_t blendWeightsOffset = MdlAttributeAbsent;
    uint32_t uvOffset = MdlAttributeAbsent;
    uint32_t texCoordVec4Offset = MdlAttributeAbsent;
    /** MDMP positions use this authored scale; packed stream normalization is separate. */
    float morphScale = 0.0f;
    uint32_t morphVertexCount = 0;
    std::vector<MdlMorphTarget> morphTargets;
    /** Revision 21/23 metadata belongs to this submesh, including local range references. */
    uint32_t auxiliaryHeader = 0;
    std::vector<glm::vec3> auxiliaryPositions;
    std::vector<MdlDrawRange> drawRanges;
    std::vector<MdlClippingDescriptor> clippingDescriptors;
};

/**
 * One independently drawable span of a submesh index buffer. Clipping descriptors
 * refer to these records by their position in MdlSubmesh::drawRanges.
 */
struct MdlDrawRange {
    /** Bone whose animated depth orders this range. */
    uint32_t partId = 0;
    /** Index of the submesh that owns this range. */
    uint32_t submeshIndex = 0;
    int32_t depthOffset = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

/** A puppet clipping relationship serialized after each revision-23 submesh. */
struct MdlClippingDescriptor {
    /** Stable authored identifier. Wallpaper Engine does not use it while drawing. */
    uint64_t opaqueId = 0;
    /** Texture name relative to materials/, without the .tex suffix. */
    std::string maskAsset;
    /** Authored clipping/composition flags. Preserve all bits even when not yet consumed. */
    uint32_t flags = 0;
    /** Indices into the owning MdlSubmesh::drawRanges that receive the generated mask. */
    std::vector<uint32_t> targets;
    /** Indices into the owning MdlSubmesh::drawRanges that generate the mask. */
    std::vector<uint32_t> sources;
};

struct MdlMesh {
    static constexpr uint32_t AttributeAbsent = MdlAttributeAbsent;
    /** Numeric MDLV container revision. */
    uint32_t version = 0;
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
    /**
     * First-submesh compatibility view of the revision-21 auxiliary stream.
     * Every observed file stores one vector per first-submesh vertex and auxiliaryHeader == 1, but consumers must
     * not infer clipping semantics from it.
     */
    uint32_t auxiliaryHeader = 0;
    std::vector<glm::vec3> auxiliaryPositions;
    /** First-submesh compatibility view of revision-21 drawable index-buffer spans. */
    std::vector<MdlDrawRange> drawRanges;
    /** First-submesh compatibility view of revision-23 clipping mask relationships. */
    std::vector<MdlClippingDescriptor> clippingDescriptors;
};
} // namespace WallpaperEngine::Data::Model
