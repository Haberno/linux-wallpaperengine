#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Parsers/MdlParser.h"

using WallpaperEngine::Data::Model::MdlMesh;
using WallpaperEngine::Data::Parsers::MdlParser;

namespace {
template <typename T> void appendValue (std::vector<char>& data, const T& value) {
    const auto* bytes = reinterpret_cast<const char*> (&value);
    data.insert (data.end (), bytes, bytes + sizeof (T));
}

std::vector<char> makeModel (
    uint32_t submeshFlags, uint32_t vertexTag = 15, uint32_t vertexStride = 48,
    const std::array<float, 6>& bounds = {}, uint32_t version = 23, uint32_t materialCount = 1,
    bool appendEmptyTail = true
) {
    std::vector<char> data;
    const std::string marker =
	"MDLV" + std::string (2, '0') + static_cast<char> ('0' + version / 10) + static_cast<char> ('0' + version % 10);
    data.insert (data.end (), marker.begin (), marker.end ());
    data.push_back ('\0');

    appendValue<uint32_t> (data, vertexTag);
    appendValue<uint32_t> (data, materialCount);
    appendValue<uint32_t> (data, 1);

    for (uint32_t material = 0; material < materialCount; material++) {
	const std::string path = "materials/test" + std::to_string (material) + ".json";
	data.insert (data.end (), path.begin (), path.end ());
	data.push_back ('\0');
    }

    appendValue (data, submeshFlags);

    // flag bit one prefixes the bounds with one more uint
    if ((submeshFlags & 0x2) != 0) {
	appendValue<uint32_t> (data, 1);
    }

    // per-submesh bounds appear in revision 17, the per-submesh vertex tag in revision 16
    if (version >= 17) {
	for (const float value : bounds) {
	    appendValue (data, value);
	}
    }

    const uint32_t vertexBytes = 3 * vertexStride;
    if (version >= 16) {
	appendValue (data, vertexTag);
    }
    appendValue (data, vertexBytes);
    data.resize (data.size () + vertexBytes, '\0');

    const bool wideIndices = (submeshFlags & 1) != 0;
    const uint32_t indexBytes = 3 * (wideIndices ? sizeof (uint32_t) : sizeof (uint16_t));
    appendValue (data, indexBytes);
    for (uint32_t index = 0; index < 3; index++) {
	if (wideIndices) {
	    appendValue (data, index);
	} else {
	    appendValue (data, static_cast<uint16_t> (index));
	}
    }

    if (appendEmptyTail && version >= 21) {
	appendValue<uint8_t> (data, 0);
	appendValue<uint8_t> (data, 0);
    }
    if (appendEmptyTail && version >= 23) {
	appendValue<uint32_t> (data, 0);
    }

    return data;
}

/**
 * Puppet models pair a skinned body with a channelmap overlay: a second submesh using a
 * different vertex tag, an extra header field and a padding run that is not all zeros.
 * Mirrors Kirby_puppet.mdl, whose padding carries stray control bytes.
 */
std::vector<char> makeTwoLayoutModel () {
    std::vector<char> data;
    const std::string marker = "MDLV0023";
    data.insert (data.end (), marker.begin (), marker.end ());
    data.push_back ('\0');

    appendValue<uint32_t> (data, 0x0180000f);
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 2);

    const std::array<std::pair<std::string, uint32_t>, 2> submeshes {
	std::pair {std::string ("materials/body.json"), 0x0180000fu},
	std::pair {std::string ("materials/overlay_channelmap.json"), 0x00800021u},
    };

    bool first = true;
    for (const auto& [path, tag] : submeshes) {
	if (!first) {
	    // the padding between submeshes, control bytes and all
	    for (const uint8_t byte : { 0x00, 0x01, 0x10, 0x00, 0x20, 0x07, 0x00, 0x00, 0x00 }) {
		data.push_back (static_cast<char> (byte));
	    }
	}

	data.insert (data.end (), path.begin (), path.end ());
	data.push_back ('\0');

	const uint32_t flags = first ? 0x0 : 0x2;
	appendValue (data, flags);
	if ((flags & 0x2) != 0) {
	    appendValue<uint32_t> (data, 1);
	}

	for (int bound = 0; bound < 6; bound++) {
	    appendValue (data, 0.0f);
	}

	const uint32_t stride = first ? 80 : 44;
	const uint32_t vertexBytes = 3 * stride;
	appendValue (data, tag);
	appendValue (data, vertexBytes);
	data.resize (data.size () + vertexBytes, '\0');

	appendValue<uint32_t> (data, 3 * sizeof (uint16_t));
	for (uint16_t index = 0; index < 3; index++) {
	    appendValue (data, index);
	}

	first = false;
    }

    appendValue<uint8_t> (data, 0);
    appendValue<uint8_t> (data, 0);
    appendValue<uint32_t> (data, 0);

    return data;
}

std::vector<char> makeClippingModel () {
    auto data = makeModel (0, 15, 48, {}, 23, 1, false);

    appendValue<uint8_t> (data, 1);
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 3 * 3 * sizeof (float));
    for (const glm::vec3 position :
	 { glm::vec3 (1.0f, 2.0f, 3.0f), glm::vec3 (4.0f, 5.0f, 6.0f), glm::vec3 (7.0f, 8.0f, 9.0f) }) {
	appendValue (data, position.x);
	appendValue (data, position.y);
	appendValue (data, position.z);
    }

    appendValue<uint8_t> (data, 1);
    appendValue<uint32_t> (data, 2 * 4 * sizeof (uint32_t));
    for (const uint32_t value : { 17u, 0u, 0u, 3u, 18u, 0u, 0u, 3u }) {
	appendValue (data, value);
    }

    appendValue<uint32_t> (data, 1);
    appendValue<uint64_t> (data, 0x1122334455667788ull);
    const std::string asset = "masks/clipping_mask_test";
    data.insert (data.end (), asset.begin (), asset.end ());
    data.push_back ('\0');
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 0);

    return data;
}

TEST_CASE ("MDLV parser exposes skinned vertex attributes") {
    const auto mesh = MdlParser::parse (makeModel (0, 0x0180000f, 80), "test-skinned.mdl");

    CHECK (mesh.skinned);
    CHECK (mesh.strideBytes == 80);
    CHECK (mesh.blendIndicesOffset == 40);
    CHECK (mesh.blendWeightsOffset == 56);
    CHECK (mesh.uvOffset == 72);
}

TEST_CASE ("MDLV parser exposes model bounds for SceneScript") {
    const auto mesh = MdlParser::parse (
	makeModel (0, 15, 48, { -1.0f, -2.0f, -3.0f, 4.0f, 5.0f, 6.0f }), "test-bounds.mdl"
    );

    REQUIRE (mesh.hasBoundingBox);
    CHECK (mesh.boundingBoxMin.x == -1.0f);
    CHECK (mesh.boundingBoxMin.y == -2.0f);
    CHECK (mesh.boundingBoxMin.z == -3.0f);
    CHECK (mesh.boundingBoxMax.x == 4.0f);
    CHECK (mesh.boundingBoxMax.y == 5.0f);
    CHECK (mesh.boundingBoxMax.z == 6.0f);
}

TEST_CASE ("MDLV parser reads auxiliary positions, draw ranges and clipping descriptors") {
    const auto mesh = MdlParser::parse (makeClippingModel (), "test-clipping.mdl");

    CHECK (mesh.version == 23);
    CHECK (mesh.auxiliaryHeader == 1);
    REQUIRE (mesh.auxiliaryPositions.size () == 3);
    CHECK (mesh.auxiliaryPositions[2] == glm::vec3 (7.0f, 8.0f, 9.0f));

    REQUIRE (mesh.drawRanges.size () == 2);
    CHECK (mesh.drawRanges[0].partId == 17);
    CHECK (mesh.drawRanges[0].submeshIndex == 0);
    CHECK (mesh.drawRanges[0].firstIndex == 0);
    CHECK (mesh.drawRanges[0].indexCount == 3);

    REQUIRE (mesh.clippingDescriptors.size () == 1);
    const auto& descriptor = mesh.clippingDescriptors[0];
    CHECK (descriptor.opaqueId == 0x1122334455667788ull);
    CHECK (descriptor.maskAsset == "masks/clipping_mask_test");
    CHECK (descriptor.flags == 1);
    CHECK (descriptor.targets == std::vector<uint32_t> { 1 });
    CHECK (descriptor.sources == std::vector<uint32_t> { 0 });
}

TEST_CASE ("MDLV parser rejects malformed draw range blocks") {
    auto data = makeModel (0, 15, 48, {}, 23, 1, false);
    appendValue<uint8_t> (data, 0);
    appendValue<uint8_t> (data, 1);
    appendValue<uint32_t> (data, 15);
    data.resize (data.size () + 15);

    CHECK_THROWS_AS (MdlParser::parse (data, "test-bad-range-size.mdl"), std::runtime_error);
}

TEST_CASE ("MDLV parser rejects draw ranges outside their submesh") {
    auto data = makeModel (0, 15, 48, {}, 23, 1, false);
    appendValue<uint8_t> (data, 0);
    appendValue<uint8_t> (data, 1);
    appendValue<uint32_t> (data, 4 * sizeof (uint32_t));
    for (const uint32_t value : { 17u, 0u, 2u, 3u }) {
	appendValue (data, value);
    }

    CHECK_THROWS_AS (MdlParser::parse (data, "test-bad-range-bounds.mdl"), std::runtime_error);
}

TEST_CASE ("MDLV parser rejects clipping descriptors with invalid range references") {
    auto data = makeModel (0, 15, 48, {}, 23, 1, false);
    appendValue<uint8_t> (data, 0);
    appendValue<uint8_t> (data, 0);
    appendValue<uint32_t> (data, 1);
    appendValue<uint64_t> (data, 42);
    data.push_back ('\0');
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 0);
    appendValue<uint32_t> (data, 0);

    CHECK_THROWS_AS (MdlParser::parse (data, "test-bad-clipping-reference.mdl"), std::runtime_error);
}
} // namespace

TEST_CASE ("MDLV auxiliary submesh flag keeps 16-bit indices") {
    const auto mesh = MdlParser::parse (makeModel (0x400), "test-auxiliary.mdl");

    REQUIRE (mesh.submeshes.size () == 1);
    CHECK (mesh.strideBytes == 48);
    CHECK (mesh.submeshes[0].indices == std::vector<uint32_t> { 0, 1, 2 });
}

TEST_CASE ("MDLV auxiliary submesh flag composes with wide indices") {
    const auto mesh = MdlParser::parse (makeModel (0x401), "test-wide.mdl");

    REQUIRE (mesh.submeshes.size () == 1);
    CHECK (mesh.submeshes[0].indices == std::vector<uint32_t> { 0, 1, 2 });
}

TEST_CASE ("MDLV parser reads through descriptive submesh flags") {
    // only bit zero changes the payload; rejecting the rest costs a dozen wallpapers that
    // set purely descriptive bits, and a genuine desync still trips the structural checks
    const auto mesh = MdlParser::parse (makeModel (0x800), "test-descriptive.mdl");

    REQUIRE (mesh.submeshes.size () == 1);
    CHECK (mesh.strideBytes == 48);
    CHECK (mesh.submeshes[0].indices == std::vector<uint32_t> { 0, 1, 2 });
}

TEST_CASE ("MDLV parser rejects vertex tags it cannot size") {
    CHECK_THROWS_AS (MdlParser::parse (makeModel (0, 0x40000000), "test-unknown-tag.mdl"), std::runtime_error);
}

TEST_CASE ("MDLV revision 16 carries no per-submesh bounds") {
    const auto mesh = MdlParser::parse (makeModel (0, 15, 48, {}, 16), "test-r16.mdl");

    REQUIRE (mesh.submeshes.size () == 1);
    CHECK_FALSE (mesh.hasBoundingBox);
    CHECK (mesh.strideBytes == 48);
    CHECK (mesh.submeshes[0].indices == std::vector<uint32_t> { 0, 1, 2 });
}

TEST_CASE ("MDLV revisions before 16 take the vertex tag from the header") {
    const auto mesh = MdlParser::parse (makeModel (0, 0x0180000f, 80, {}, 7), "test-r7.mdl");

    REQUIRE (mesh.submeshes.size () == 1);
    CHECK (mesh.skinned);
    CHECK (mesh.strideBytes == 80);
    CHECK (mesh.uvOffset == 72);
}

TEST_CASE ("MDLV revision 14 zero tag uses the legacy compact puppet layout") {
    const auto mesh = MdlParser::parse (makeModel (0, 0, 52, {}, 14), "test-r14-puppet.mdl");

    REQUIRE (mesh.submeshes.size () == 1);
    CHECK (mesh.skinned);
    CHECK (mesh.strideBytes == 52);
    CHECK (mesh.blendIndicesOffset == 12);
    CHECK (mesh.blendWeightsOffset == 28);
    CHECK (mesh.uvOffset == 44);
}

TEST_CASE ("MDLV vertex layout omits attributes the tag leaves out") {
    // position | uv | skin: no normal or tangent block, so uv sits right after the weights
    const auto mesh = MdlParser::parse (makeModel (0, 0x01800009, 52), "test-sparse.mdl");

    REQUIRE (mesh.submeshes.size () == 1);
    CHECK (mesh.strideBytes == 52);
    CHECK (mesh.blendIndicesOffset == 12);
    CHECK (mesh.uvOffset == 44);
    CHECK (mesh.normalOffset == MdlMesh::AttributeAbsent);
    CHECK (mesh.tangentOffset == MdlMesh::AttributeAbsent);
}

TEST_CASE ("MDLV channelmap overlay vertices carry a vec4 texcoord") {
    // the blink overlay layout: position, four blend indices and a vec4 texcoord, no weights
    const auto mesh = MdlParser::parse (makeModel (0x2, 0x00800021, 44), "test-channelmap.mdl");

    REQUIRE (mesh.submeshes.size () == 1);
    CHECK (mesh.strideBytes == 44);
    CHECK (mesh.submeshes[0].strideBytes == 44);
    CHECK (mesh.submeshes[0].blendIndicesOffset == 12);
    CHECK (mesh.submeshes[0].texCoordVec4Offset == 28);
    CHECK (mesh.submeshes[0].indices == std::vector<uint32_t> { 0, 1, 2 });
}

TEST_CASE ("MDLV submeshes may disagree on the vertex layout") {
    const auto mesh = MdlParser::parse (makeTwoLayoutModel (), "test-two-layouts.mdl");

    REQUIRE (mesh.submeshes.size () == 2);
    // the mesh-level layout describes the first submesh only
    CHECK (mesh.vertexTag == 0x0180000f);
    CHECK (mesh.strideBytes == 80);
    CHECK (mesh.submeshes[0].vertexTag == 0x0180000f);
    CHECK (mesh.submeshes[1].vertexTag == 0x00800021);
    CHECK (mesh.submeshes[1].strideBytes == 44);
    CHECK (mesh.submeshes[1].texCoordVec4Offset == 28);
    CHECK (mesh.submeshes[1].materialPath == "materials/overlay_channelmap.json");
    CHECK (mesh.submeshes[1].indices == std::vector<uint32_t> { 0, 1, 2 });
}

TEST_CASE ("MDLV submeshes carry as many material paths as the header announces") {
    // grid and prism models pair a base material with an auxiliary one; missing the second
    // path used to leave the reader sitting on ASCII where the submesh flags belong
    const auto mesh = MdlParser::parse (makeModel (0, 15, 48, {}, 23, 2), "test-materials.mdl");

    REQUIRE (mesh.submeshes.size () == 1);
    CHECK (mesh.submeshes[0].materialPath == "materials/test0.json");
    CHECK (mesh.strideBytes == 48);
    CHECK (mesh.submeshes[0].indices == std::vector<uint32_t> { 0, 1, 2 });
}
