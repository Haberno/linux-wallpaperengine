#include "MdlParser.h"

#include <cstring>

#include "WallpaperEngine/Assets/AssetLocator.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Data::Parsers;

namespace {
/** Attribute mask observed on static meshes: position | normal | tangent | uv */
constexpr uint32_t STATIC_VERTEX_TAG = 15;
constexpr uint32_t STATIC_VERTEX_STRIDE = 48;
/** Attribute mask observed on skinned character meshes: position | normal | tangent | skin | uv */
constexpr uint32_t SKINNED_VERTEX_TAG = 0x0180000f;
constexpr uint32_t SKINNED_VERTEX_STRIDE = 80;

/** Bit zero selects 32-bit indices. BoostModel assets also set the independent 0x400 bit. */
constexpr uint32_t INDEX_32_BIT_FLAG = 0x1;
constexpr uint32_t AUXILIARY_SUBMESH_FLAG = 0x400;
constexpr uint32_t KNOWN_SUBMESH_FLAGS = INDEX_32_BIT_FLAG | AUXILIARY_SUBMESH_FLAG;

struct VertexLayout {
    uint32_t tag = 0;
    uint32_t strideBytes = 0;
    uint32_t positionOffset = 0;
    uint32_t normalOffset = 0;
    uint32_t tangentOffset = 0;
    uint32_t uvOffset = 0;
    bool skinned = false;
    uint32_t blendIndicesOffset = 0;
    uint32_t blendWeightsOffset = 0;
};

VertexLayout getVertexLayout (uint32_t tag) {
    if (tag == STATIC_VERTEX_TAG) {
	return {
	    .tag = tag,
	    .strideBytes = STATIC_VERTEX_STRIDE,
	    .positionOffset = 0,
	    .normalOffset = 12,
	    .tangentOffset = 24,
	    .uvOffset = 40,
	    .skinned = false,
	};
    }

    if (tag == SKINNED_VERTEX_TAG) {
	return {
	    .tag = tag,
	    .strideBytes = SKINNED_VERTEX_STRIDE,
	    .positionOffset = 0,
	    .normalOffset = 12,
	    .tangentOffset = 24,
	    .uvOffset = 72,
	    .skinned = true,
	    .blendIndicesOffset = 40,
	    .blendWeightsOffset = 56,
	};
    }

    return {};
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

    size_t offset = markerSize;
    // header: two DWORDs of unknown meaning ((15, 1) in the wild), then the submesh count
    offset += sizeof (uint32_t) * 2;
    const auto submeshCount = readValue<uint32_t> (data, offset, filename);

    if (submeshCount == 0 || submeshCount > 256) {
	sLog.exception ("Unsupported MDLV submesh count ", submeshCount, " in ", filename);
    }

    MdlMesh mesh {};

    for (uint32_t submeshIndex = 0; submeshIndex < submeshCount; submeshIndex++) {
	// submeshes are separated by a small zero gap (6 bytes observed)
	while (offset < data.size () && data[offset] == '\0') {
	    offset++;
	}

	const size_t materialEnd = std::string_view (data.data (), data.size ()).find ('\0', offset);
	if (materialEnd == std::string_view::npos) {
	    sLog.exception ("Malformed MDLV material path in ", filename);
	}

	MdlSubmesh submesh {};
	submesh.materialPath = std::string (data.data () + offset, materialEnd - offset);
	offset = materialEnd + 1;

	// Bit zero marks 32-bit indices. Other known bits describe the submesh but do not
	// change the index payload width; BoostModel assets set 0x400 and still store u16.
	const auto submeshFlags = readValue<uint32_t> (data, offset, filename);

	if ((submeshFlags & ~KNOWN_SUBMESH_FLAGS) != 0) {
	    sLog.exception ("Unsupported MDLV submesh flags ", submeshFlags, " in ", filename);
	}
	const bool wideIndices = (submeshFlags & INDEX_32_BIT_FLAG) != 0;

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

	const auto tag = readValue<uint32_t> (data, offset, filename);
	const auto vertexBytes = readValue<uint32_t> (data, offset, filename);
	const auto layout = getVertexLayout (tag);

	if (layout.strideBytes == 0 || vertexBytes % layout.strideBytes != 0) {
	    sLog.exception ("Unsupported MDLV vertex layout (tag ", tag, ") in ", filename);
	}

	if (mesh.strideBytes == 0) {
	    mesh.strideBytes = layout.strideBytes;
	    mesh.positionOffset = layout.positionOffset;
	    mesh.normalOffset = layout.normalOffset;
	    mesh.tangentOffset = layout.tangentOffset;
	    mesh.uvOffset = layout.uvOffset;
	    mesh.skinned = layout.skinned;
	    mesh.blendIndicesOffset = layout.blendIndicesOffset;
	    mesh.blendWeightsOffset = layout.blendWeightsOffset;
	} else if (
	    mesh.strideBytes != layout.strideBytes || mesh.positionOffset != layout.positionOffset
	    || mesh.normalOffset != layout.normalOffset || mesh.tangentOffset != layout.tangentOffset
	    || mesh.uvOffset != layout.uvOffset || mesh.skinned != layout.skinned
	    || mesh.blendIndicesOffset != layout.blendIndicesOffset
	    || mesh.blendWeightsOffset != layout.blendWeightsOffset
	) {
	    sLog.exception ("Mixed MDLV vertex layouts are not supported in ", filename);
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

	const uint32_t vertexCount = vertexBytes / mesh.strideBytes;
	for (const auto index : submesh.indices) {
	    if (index >= vertexCount) {
		sLog.exception ("MDLV index out of range in ", filename);
	    }
	}

	mesh.submeshes.push_back (std::move (submesh));
    }

    return mesh;
}
