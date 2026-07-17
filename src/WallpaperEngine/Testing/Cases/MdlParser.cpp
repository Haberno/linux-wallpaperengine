#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Parsers/MdlParser.h"

using WallpaperEngine::Data::Parsers::MdlParser;

namespace {
template <typename T> void appendValue (std::vector<char>& data, const T& value) {
    const auto* bytes = reinterpret_cast<const char*> (&value);
    data.insert (data.end (), bytes, bytes + sizeof (T));
}

std::vector<char> makeModel (uint32_t submeshFlags, uint32_t vertexTag = 15, uint32_t vertexStride = 48) {
    std::vector<char> data;
    const char marker[] = "MDLV0023";
    data.insert (data.end (), marker, marker + sizeof (marker));

    appendValue<uint32_t> (data, 15);
    appendValue<uint32_t> (data, 1);
    appendValue<uint32_t> (data, 1);

    const std::string material = "materials/test.json";
    data.insert (data.end (), material.begin (), material.end ());
    data.push_back ('\0');
    appendValue (data, submeshFlags);

    for (size_t i = 0; i < 6; i++) {
	appendValue (data, 0.0f);
    }

    const uint32_t vertexBytes = 3 * vertexStride;
    appendValue (data, vertexTag);
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

TEST_CASE ("MDLV parser rejects unknown submesh flags") {
    CHECK_THROWS_AS (MdlParser::parse (makeModel (0x800), "test-unknown.mdl"), std::runtime_error);
}
