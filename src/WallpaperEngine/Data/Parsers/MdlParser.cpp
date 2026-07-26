#include "MdlParser.h"

#include <cstring>

#include "WallpaperEngine/Assets/AssetLocator.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Data::Parsers;

namespace {
/** Bit zero selects 32-bit indices; the remaining bits only describe the submesh. */
constexpr uint32_t INDEX_32_BIT_FLAG = 0x1;

/** Bit one adds an extra uint before the bounds; puppet channelmap submeshes set it. */
constexpr uint32_t EXTRA_FIELD_FLAG = 0x2;

/** Skin indices and weights travel together and are what makes a mesh skinned. */
constexpr uint32_t SKIN_VERTEX_BITS = 0x01800000;

struct VertexLayout {
    uint32_t tag = 0;
    uint32_t strideBytes = 0;
    uint32_t positionOffset = 0;
    uint32_t normalOffset = MdlMesh::AttributeAbsent;
    uint32_t tangentOffset = MdlMesh::AttributeAbsent;
    uint32_t uvOffset = MdlMesh::AttributeAbsent;
    uint32_t texCoordVec4Offset = MdlMesh::AttributeAbsent;
    bool skinned = false;
    uint32_t blendIndicesOffset = MdlMesh::AttributeAbsent;
    uint32_t blendWeightsOffset = MdlMesh::AttributeAbsent;
};

/**
 * Optional vertex attributes in the order they are laid out after the position vec3,
 * with the byte size each contributes to the stride. The 0x00010000 attribute has no
 * identified use; only its size matters, so nothing points at it.
 */
constexpr struct {
    uint32_t bit;
    uint32_t bytes;
    uint32_t VertexLayout::* offset;
} VERTEX_ATTRIBUTES[] = {
    {0x00000002, 12, &VertexLayout::normalOffset},
    {0x00000004, 16, &VertexLayout::tangentOffset},
    {0x00010000, 4, nullptr},
    {0x00800000, 16, &VertexLayout::blendIndicesOffset},
    {0x01000000, 16, &VertexLayout::blendWeightsOffset},
    {0x00000020, 16, &VertexLayout::texCoordVec4Offset},
    {0x00000008, 8, &VertexLayout::uvOffset},
};

/** Every bit the layout above accounts for, plus 0x1 which carries no payload of its own. */
constexpr uint32_t KNOWN_VERTEX_BITS = 0x0181002f;

/**
 * Derives the vertex layout from the attribute bitmask. Returns a zero stride for masks
 * carrying bits we cannot size, since guessing would desynchronize the whole submesh.
 */
VertexLayout getVertexLayout (uint32_t tag) {
    if ((tag & ~KNOWN_VERTEX_BITS) != 0) {
	return {};
    }

    // a position vec3 always leads the vertex, whether or not 0x1 is set
    VertexLayout layout {.tag = tag, .strideBytes = 12, .positionOffset = 0};

    layout.skinned = (tag & SKIN_VERTEX_BITS) != 0;

    for (const auto& [bit, bytes, offset] : VERTEX_ATTRIBUTES) {
	if ((tag & bit) == 0) {
	    continue;
	}

	if (offset != nullptr) {
	    layout.*offset = layout.strideBytes;
	}

	layout.strideBytes += bytes;
    }

    return layout;
}

template <typename T> T readValue (const std::vector<char>& data, size_t& offset, const std::string& filename) {
    if (offset + sizeof (T) > data.size ()) {
	sLog.exception ("Unexpected end of MDLV file ", filename);
    }

    T value;
    std::memcpy (&value, data.data () + offset, sizeof (T));
    offset += sizeof (T);
    return value;
}
} // namespace

MdlMesh MdlParser::load (const Project& project, const std::string& filename) {
    const auto stream = project.assetLocator->read (filename);
    const std::vector<char> data { std::istreambuf_iterator<char> (*stream), std::istreambuf_iterator<char> () };

    return parse (data, filename);
}

MdlMesh MdlParser::parse (const std::vector<char>& data, const std::string& filename) {
    constexpr size_t markerSize = 9; // "MDLV00XX\0"
    if (data.size () < markerSize || std::memcmp (data.data (), "MDLV00", 6) != 0) {
	sLog.exception ("Not an MDLV model file: ", filename);
    }

    // the container revision drives the submesh layout below; "MDLV0016" -> 16
    uint32_t version = 0;
    for (size_t digit = 4; digit < 8; digit++) {
	if (data[digit] < '0' || data[digit] > '9') {
	    sLog.exception ("Malformed MDLV version in ", filename);
	}

	version = version * 10 + static_cast<uint32_t> (data[digit] - '0');
    }

    size_t offset = markerSize;
    // header: the default vertex tag, how many material paths each submesh carries, then
    // the submesh count. Revisions before 16 store the tag here only, not per submesh.
    const auto headerVertexTag = readValue<uint32_t> (data, offset, filename);
    const auto materialCount = readValue<uint32_t> (data, offset, filename);
    const auto submeshCount = readValue<uint32_t> (data, offset, filename);

    if (materialCount == 0 || materialCount > 16) {
	sLog.exception ("Unsupported MDLV material count ", materialCount, " in ", filename);
    }

    if (submeshCount == 0 || submeshCount > 256) {
	sLog.exception ("Unsupported MDLV submesh count ", submeshCount, " in ", filename);
    }

    MdlMesh mesh {};

    for (uint32_t submeshIndex = 0; submeshIndex < submeshCount; submeshIndex++) {
	MdlSubmesh submesh {};

	// a submesh lists materialCount paths back to back; the first one draws it and the
	// rest are auxiliary (channelmaps and the like), but all of them have to be stepped over
	for (uint32_t material = 0; material < materialCount; material++) {
	    // submeshes are separated by a short run of padding, usually 6 zero bytes; Kirby_puppet
	    // leaves other control bytes in it, and no material path starts below '!', so skip the
	    // whole low range instead. A real desync still trips the stride and index checks below
	    while (offset < data.size () && static_cast<unsigned char> (data[offset]) <= ' ') {
		offset++;
	    }

	    const size_t materialEnd = std::string_view (data.data (), data.size ()).find ('\0', offset);
	    if (materialEnd == std::string_view::npos) {
		sLog.exception ("Malformed MDLV material path in ", filename);
	    }

	    if (material == 0) {
		submesh.materialPath = std::string (data.data () + offset, materialEnd - offset);
	    }

	    offset = materialEnd + 1;
	}

	// Bit zero marks 32-bit indices. The remaining bits only describe the submesh and do
	// not change the payload, so they are read through; BoostModel assets set 0x400.
	const auto submeshFlags = readValue<uint32_t> (data, offset, filename);
	const bool wideIndices = (submeshFlags & INDEX_32_BIT_FLAG) != 0;

	// bit one prefixes the bounds with one more uint; skipping it desynchronizes the
	// rest of the submesh (the channelmap overlay in Kirby_puppet.mdl sets it)
	if ((submeshFlags & EXTRA_FIELD_FLAG) != 0) {
	    readValue<uint32_t> (data, offset, filename);
	}

	// per-submesh bounds only exist from revision 17 on
	if (version >= 17) {
	    glm::vec3 boundingBoxMin;
	    boundingBoxMin.x = readValue<float> (data, offset, filename);
	    boundingBoxMin.y = readValue<float> (data, offset, filename);
	    boundingBoxMin.z = readValue<float> (data, offset, filename);
	    glm::vec3 boundingBoxMax;
	    boundingBoxMax.x = readValue<float> (data, offset, filename);
	    boundingBoxMax.y = readValue<float> (data, offset, filename);
	    boundingBoxMax.z = readValue<float> (data, offset, filename);

	    if (!mesh.hasBoundingBox) {
		mesh.boundingBoxMin = boundingBoxMin;
		mesh.boundingBoxMax = boundingBoxMax;
		mesh.hasBoundingBox = true;
	    } else {
		mesh.boundingBoxMin = glm::min (mesh.boundingBoxMin, boundingBoxMin);
		mesh.boundingBoxMax = glm::max (mesh.boundingBoxMax, boundingBoxMax);
	    }
	}

	// revisions before 16 describe every submesh with the tag from the file header
	const auto tag = version >= 16 ? readValue<uint32_t> (data, offset, filename) : headerVertexTag;
	const auto vertexBytes = readValue<uint32_t> (data, offset, filename);
	const auto layout = getVertexLayout (tag);

	if (layout.strideBytes == 0 || vertexBytes % layout.strideBytes != 0) {
	    sLog.exception ("Unsupported MDLV vertex layout (tag ", tag, ") in ", filename);
	}

	submesh.vertexTag = tag;
	submesh.strideBytes = layout.strideBytes;
	submesh.blendIndicesOffset = layout.blendIndicesOffset;
	submesh.texCoordVec4Offset = layout.texCoordVec4Offset;

	// the mesh-level layout describes the first submesh; consumers that draw the whole
	// container with one vertex format compare tags and skip the submeshes that differ
	if (mesh.strideBytes == 0) {
	    mesh.vertexTag = tag;
	    mesh.strideBytes = layout.strideBytes;
	    mesh.positionOffset = layout.positionOffset;
	    mesh.normalOffset = layout.normalOffset;
	    mesh.tangentOffset = layout.tangentOffset;
	    mesh.uvOffset = layout.uvOffset;
	    mesh.skinned = layout.skinned;
	    mesh.blendIndicesOffset = layout.blendIndicesOffset;
	    mesh.blendWeightsOffset = layout.blendWeightsOffset;
	}

	if (offset + vertexBytes > data.size ()) {
	    sLog.exception ("Unexpected end of MDLV vertex data in ", filename);
	}

	submesh.vertices.resize (vertexBytes / sizeof (float));
	std::memcpy (submesh.vertices.data (), data.data () + offset, vertexBytes);
	offset += vertexBytes;

	const auto indexBytes = readValue<uint32_t> (data, offset, filename);
	const uint32_t indexWidth = wideIndices ? sizeof (uint32_t) : sizeof (uint16_t);

	if (indexBytes % indexWidth != 0 || offset + indexBytes > data.size ()) {
	    sLog.exception ("Unexpected end of MDLV index data in ", filename);
	}

	const size_t indexCount = indexBytes / indexWidth;
	submesh.indices.resize (indexCount);

	if (wideIndices) {
	    std::memcpy (submesh.indices.data (), data.data () + offset, indexBytes);
	} else {
	    for (size_t i = 0; i < indexCount; i++) {
		uint16_t narrow;
		std::memcpy (&narrow, data.data () + offset + i * sizeof (uint16_t), sizeof (uint16_t));
		submesh.indices[i] = narrow;
	    }
	}

	offset += indexBytes;

	const uint32_t vertexCount = vertexBytes / layout.strideBytes;
	for (const auto index : submesh.indices) {
	    if (index >= vertexCount) {
		sLog.exception ("MDLV index out of range in ", filename);
	    }
	}

	mesh.submeshes.push_back (std::move (submesh));
    }

    return mesh;
}
