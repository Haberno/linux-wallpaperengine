#include "MdlParser.h"

#include <algorithm>
#include <cmath>
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
VertexLayout getVertexLayout (uint32_t tag, uint32_t version) {
    // MDLV0013/0014 puppet bodies serialize the compact skinned layout with a
    // zero tag. Later revisions carry the actual skin/uv bits.
    if (tag == 0 && version < 16) {
	return VertexLayout {
	    .tag = tag,
	    .strideBytes = 52,
	    .positionOffset = 0,
	    .uvOffset = 44,
	    .skinned = true,
	    .blendIndicesOffset = 12,
	    .blendWeightsOffset = 28,
	};
    }

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

template <typename T> T readValue (
    const std::vector<char>& data, size_t& offset, const std::string& filename, size_t end = SIZE_MAX
) {
    end = std::min (end, data.size ());
    if (offset > end || sizeof (T) > end - offset) {
	sLog.exception ("Unexpected end of MDLV file ", filename);
    }

    T value;
    std::memcpy (&value, data.data () + offset, sizeof (T));
    offset += sizeof (T);
    return value;
}

std::string readString (
    const std::vector<char>& data, size_t& offset, const std::string& filename, size_t endOffset = SIZE_MAX
) {
    constexpr size_t maximumAssetPathLength = 4096;
    endOffset = std::min (endOffset, data.size ());
    if (offset > endOffset) sLog.exception ("Unexpected end of MDLV string in ", filename);
    const size_t available = endOffset - offset;
    const size_t searchLength = std::min (available, maximumAssetPathLength + 1);
    const auto* end = static_cast<const char*> (std::memchr (data.data () + offset, '\0', searchLength));

    if (end == nullptr) {
	sLog.exception ("Malformed MDLV string in ", filename);
    }

    const size_t length = static_cast<size_t> (end - (data.data () + offset));
    std::string result (data.data () + offset, length);
    offset += length + 1;
    return result;
}

std::vector<uint32_t> readRangeReferences (
    const std::vector<char>& data, size_t& offset, const std::string& filename, const size_t rangeCount,
    const char* label
) {
    const auto count = readValue<uint32_t> (data, offset, filename);
    if (count > (data.size () - offset) / sizeof (uint32_t)) {
	sLog.exception ("Invalid MDLV clipping ", label, " count in ", filename);
    }

    std::vector<uint32_t> result;
    result.reserve (count);
    for (uint32_t index = 0; index < count; index++) {
	const auto reference = readValue<uint32_t> (data, offset, filename);
	if (reference >= rangeCount) {
	    sLog.exception ("MDLV clipping ", label, " range out of bounds in ", filename);
	}
	result.push_back (reference);
    }

    return result;
}
/** Decode MDMP payloads while stepping over other sections by their absolute end. */
void readMorphTargets (const std::vector<char>& data, size_t offset, const std::string& filename, MdlMesh& mesh) {
    constexpr size_t markerSize = 9;
    uint32_t boneCount = 0;
    while (offset + markerSize + sizeof (uint32_t) <= data.size ()) {
	const std::string_view marker (data.data () + offset, markerSize - 1);
	if (data[offset + markerSize - 1] != '\0'
	    || !std::all_of (marker.begin () + 4, marker.end (), [] (char c) { return c >= '0' && c <= '9'; })) return;
	offset += markerSize;
	const size_t end = readValue<uint32_t> (data, offset, filename);
	const bool morph = marker == "MDMP0001";
	if (end < offset || end > data.size ()) {
	    if (morph) sLog.exception ("Invalid MDMP section boundary in ", filename);
	    return;
	}
	if (marker.substr (0, 4) == "MDLS" && end - offset >= sizeof (uint32_t)) {
	    boneCount = readValue<uint32_t> (data, offset, filename, end);
	}
	if (morph) {
	    const auto stream = [&] (const size_t components) {
		const auto bytes = readValue<uint32_t> (data, offset, filename, end);
		if (bytes != components * sizeof (int16_t) || bytes > end - offset)
		    sLog.exception ("Invalid MDMP packed stream length in ", filename);
		std::vector<int16_t> values (components);
		if (bytes != 0) std::memcpy (values.data (), data.data () + offset, bytes);
		offset += bytes;
		return values;
	    };
	    for (auto& submesh : mesh.submeshes) {
		const auto count = readValue<uint16_t> (data, offset, filename, end);
		if (count == 0) continue;
		submesh.morphScale = readValue<float> (data, offset, filename, end);
		submesh.morphVertexCount = readValue<uint32_t> (data, offset, filename, end);
		if (count > (end - offset) / (sizeof (uint64_t) + 1 + sizeof (uint32_t)))
		    sLog.exception ("Invalid MDMP target count in ", filename);
		submesh.morphTargets.reserve (count);
		for (uint16_t index = 0; index < count; ++index) {
		    WallpaperEngine::Data::Model::MdlMorphTarget target;
		    target.id = readValue<uint64_t> (data, offset, filename, end);
		    target.name = readString (data, offset, filename, end);
		    // Native 1402657a0 clamps the declared count to each XYZ stream's size.
		    const auto positionBytes = readValue<uint32_t> (data, offset, filename, end);
		    offset -= sizeof (uint32_t);
		    submesh.morphVertexCount = std::min (submesh.morphVertexCount, positionBytes / 6);
		    const size_t components = static_cast<size_t> (submesh.morphVertexCount) * 3;
		    target.positionDeltas = stream (components);
		    if ((submesh.flags & 0x400) != 0) target.normalDeltas = stream (components);
		    if ((submesh.flags & 0x800) != 0) target.tangentDeltas = stream (components);
		    if ((submesh.flags & 0x1000) != 0) target.alphaValues = stream (submesh.morphVertexCount);
		    if ((submesh.flags & 0x2000) != 0) {
			WallpaperEngine::Data::Model::MdlMorphModifier modifier;
			modifier.boneIndex = readValue<uint32_t> (data, offset, filename, end);
			modifier.rule = readValue<uint32_t> (data, offset, filename, end);
			modifier.range.x = readValue<float> (data, offset, filename, end);
			modifier.range.y = readValue<float> (data, offset, filename, end);
			if (modifier.boneIndex >= boneCount)
			    sLog.exception ("MDMP modifier bone index exceeds skeleton in ", filename);
			target.modifier = modifier;
		    }
		    submesh.morphTargets.push_back (std::move (target));
		}
	    }
	}
	offset = end;
    }
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

    MdlMesh mesh {.version = version};

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
	submesh.flags = submeshFlags;
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
	const auto layout = getVertexLayout (tag, version);

	if (layout.strideBytes == 0 || vertexBytes % layout.strideBytes != 0) {
	    sLog.exception ("Unsupported MDLV vertex layout (tag ", tag, ") in ", filename);
	}

	submesh.vertexTag = tag;
	submesh.strideBytes = layout.strideBytes;
	submesh.blendIndicesOffset = layout.blendIndicesOffset;
	submesh.blendWeightsOffset = layout.blendWeightsOffset;
	submesh.uvOffset = layout.uvOffset;
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

    // Revisions 21 and newer append two independently optional blocks after the
    // final submesh and before the MDLS animation section.
    if (version >= 21) {
	const bool hasAuxiliaryPositions = readValue<uint8_t> (data, offset, filename) != 0;
	if (hasAuxiliaryPositions) {
	    mesh.auxiliaryHeader = readValue<uint32_t> (data, offset, filename);
	    const auto byteCount = readValue<uint32_t> (data, offset, filename);
	    constexpr uint32_t vec3Bytes = sizeof (float) * 3;

	    if (byteCount % vec3Bytes != 0 || byteCount > data.size () - offset) {
		sLog.exception ("Invalid MDLV auxiliary position block in ", filename);
	    }

	    mesh.auxiliaryPositions.reserve (byteCount / vec3Bytes);
	    const size_t end = offset + byteCount;
	    while (offset < end) {
		glm::vec3 position;
		position.x = readValue<float> (data, offset, filename);
		position.y = readValue<float> (data, offset, filename);
		position.z = readValue<float> (data, offset, filename);
		if (!std::isfinite (position.x) || !std::isfinite (position.y) || !std::isfinite (position.z)) {
		    sLog.exception ("MDLV auxiliary position contains a non-finite value in ", filename);
		}
		mesh.auxiliaryPositions.push_back (position);
	    }
	}

	const bool hasDrawRanges = readValue<uint8_t> (data, offset, filename) != 0;
	if (hasDrawRanges) {
	    const auto byteCount = readValue<uint32_t> (data, offset, filename);
	    constexpr uint32_t recordBytes = sizeof (uint32_t) * 4;

	    if (byteCount % recordBytes != 0 || byteCount > data.size () - offset) {
		sLog.exception ("Invalid MDLV draw range block in ", filename);
	    }

	    mesh.drawRanges.reserve (byteCount / recordBytes);
	    const size_t end = offset + byteCount;
	    while (offset < end) {
		MdlDrawRange range {
		    .partId = readValue<uint32_t> (data, offset, filename),
		    .depthOffset = readValue<int32_t> (data, offset, filename),
		    .firstIndex = readValue<uint32_t> (data, offset, filename),
		    .indexCount = readValue<uint32_t> (data, offset, filename),
		};

		const size_t submeshIndexCount = mesh.submeshes.front ().indices.size ();
		if (range.firstIndex > submeshIndexCount || range.indexCount > submeshIndexCount - range.firstIndex) {
		    sLog.exception ("MDLV draw range exceeds its submesh index buffer in ", filename);
		}

		mesh.drawRanges.push_back (range);
	    }
	}
    }

    if (version >= 23) {
	const auto descriptorCount = readValue<uint32_t> (data, offset, filename);
	// Every descriptor needs at least its id, an empty string terminator, flags,
	// and two zero list counts. This also bounds reserve() on malformed input.
	constexpr size_t minimumDescriptorBytes
	    = sizeof (uint64_t) + sizeof (char) + sizeof (uint32_t) * 3;
	if (descriptorCount > (data.size () - offset) / minimumDescriptorBytes) {
	    sLog.exception ("Invalid MDLV clipping descriptor count in ", filename);
	}

	mesh.clippingDescriptors.reserve (descriptorCount);
	for (uint32_t descriptorIndex = 0; descriptorIndex < descriptorCount; descriptorIndex++) {
	    MdlClippingDescriptor descriptor;
	    descriptor.opaqueId = readValue<uint64_t> (data, offset, filename);
	    descriptor.maskAsset = readString (data, offset, filename);
	    descriptor.flags = readValue<uint32_t> (data, offset, filename);
	    descriptor.targets
		= readRangeReferences (data, offset, filename, mesh.drawRanges.size (), "target");
	    descriptor.sources
		= readRangeReferences (data, offset, filename, mesh.drawRanges.size (), "source");
	    mesh.clippingDescriptors.push_back (std::move (descriptor));
	}
    }

    readMorphTargets (data, offset, filename, mesh);
    return mesh;
}
