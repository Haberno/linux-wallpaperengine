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
    mesh.strideBytes = STATIC_VERTEX_STRIDE;
    mesh.positionOffset = 0;
    mesh.normalOffset = 12;
    mesh.tangentOffset = 24;
    mesh.uvOffset = 40;

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

	// 1 marks 32-bit indices (meshes with more than 65535 vertices), 0 marks 16-bit
	const auto wideIndices = readValue<uint32_t> (data, offset, filename);

	if (wideIndices > 1) {
	    sLog.exception ("Unsupported MDLV index width flag ", wideIndices, " in ", filename);
	}

	// bounding box min/max, unused
	offset += sizeof (float) * 6;

	const auto tag = readValue<uint32_t> (data, offset, filename);
	const auto vertexBytes = readValue<uint32_t> (data, offset, filename);

	// ponytail: only the tag-15 static layout is implemented; other tags (skinned
	// variants live in CImage's puppet loader) need their strides decoded first
	if (tag != STATIC_VERTEX_TAG || vertexBytes % STATIC_VERTEX_STRIDE != 0) {
	    sLog.exception ("Unsupported MDLV vertex layout (tag ", tag, ") in ", filename);
	}

	if (offset + vertexBytes > data.size ()) {
	    sLog.exception ("Unexpected end of MDLV vertex data in ", filename);
	}

	submesh.vertices.resize (vertexBytes / sizeof (float));
	std::memcpy (submesh.vertices.data (), data.data () + offset, vertexBytes);
	offset += vertexBytes;

	const auto indexBytes = readValue<uint32_t> (data, offset, filename);
	const uint32_t indexWidth = wideIndices == 1 ? sizeof (uint32_t) : sizeof (uint16_t);

	if (indexBytes % indexWidth != 0 || offset + indexBytes > data.size ()) {
	    sLog.exception ("Unexpected end of MDLV index data in ", filename);
	}

	const size_t indexCount = indexBytes / indexWidth;
	submesh.indices.resize (indexCount);

	if (wideIndices == 1) {
	    std::memcpy (submesh.indices.data (), data.data () + offset, indexBytes);
	} else {
	    for (size_t i = 0; i < indexCount; i++) {
		uint16_t narrow;
		std::memcpy (&narrow, data.data () + offset + i * sizeof (uint16_t), sizeof (uint16_t));
		submesh.indices[i] = narrow;
	    }
	}

	offset += indexBytes;

	const uint32_t vertexCount = vertexBytes / STATIC_VERTEX_STRIDE;
	for (const auto index : submesh.indices) {
	    if (index >= vertexCount) {
		sLog.exception ("MDLV index out of range in ", filename);
	    }
	}

	mesh.submeshes.push_back (std::move (submesh));
    }

    return mesh;
}
