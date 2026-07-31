#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Render/Objects/PuppetClippingPlan.h"

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Render::Objects;

namespace {
MdlMesh makeMesh (const uint32_t indexCount) {
    MdlMesh mesh;
    mesh.submeshes.emplace_back ();
    mesh.submeshes[0].indices.resize (indexCount);
    return mesh;
}

MdlClippingDescriptor descriptor (
    const uint64_t id, std::vector<uint32_t> targets, std::vector<uint32_t> sources, const uint32_t flags = 0
) {
    return MdlClippingDescriptor {
	.opaqueId = id,
	.maskAsset = "masks/test_" + std::to_string (id),
	.flags = flags,
	.targets = std::move (targets),
	.sources = std::move (sources),
    };
}
}

TEST_CASE ("Puppet clipping plan preserves order and shares identical masks") {
    auto mesh = makeMesh (3630);
    uint32_t first = 0;
    for (const uint32_t count : { 582u, 291u, 240u, 465u, 822u, 696u, 243u, 291u }) {
	mesh.drawRanges.push_back (MdlDrawRange {
	    .partId = static_cast<uint32_t> (mesh.drawRanges.size () + 1),
	    .submeshIndex = 0,
	    .firstIndex = first,
	    .indexCount = count,
	});
	first += count;
    }
    mesh.clippingDescriptors.push_back (descriptor (191, { 2 }, { 0, 1 }));
    mesh.clippingDescriptors.push_back (descriptor (191, { 3 }, { 0, 1 }));

    const auto plan = buildPuppetClippingPlan (mesh, 0);

    REQUIRE (plan.masks.size () == 1);
    CHECK (plan.masks[0].sourceRanges == std::vector<uint32_t> { 0, 1 });
    REQUIRE (plan.draws.size () == 3);
    CHECK (plan.draws[0].firstIndex == 0);
    CHECK (plan.draws[0].indexCount == 873);
    CHECK (plan.draws[0].masks.empty ());
    CHECK (plan.draws[1].firstIndex == 873);
    CHECK (plan.draws[1].indexCount == 705);
    CHECK (plan.draws[1].masks == std::vector<size_t> { 0 });
    CHECK (plan.draws[2].firstIndex == 1578);
    CHECK (plan.draws[2].indexCount == 2052);
    CHECK (plan.draws[2].masks.empty ());
    CHECK_FALSE (plan.requiresComposition);
}

TEST_CASE ("Puppet clipping plan retains index-buffer gaps") {
    auto mesh = makeMesh (12);
    mesh.drawRanges = {
	MdlDrawRange {.submeshIndex = 0, .firstIndex = 3, .indexCount = 3},
	MdlDrawRange {.submeshIndex = 0, .firstIndex = 9, .indexCount = 3},
    };
    mesh.clippingDescriptors.push_back (descriptor (1, { 0 }, { 1 }));

    const auto plan = buildPuppetClippingPlan (mesh, 0);

    REQUIRE (plan.draws.size () == 3);
    CHECK (plan.draws[0].firstIndex == 0);
    CHECK (plan.draws[0].indexCount == 3);
    CHECK (plan.draws[1].masks == std::vector<size_t> { 0 });
    CHECK (plan.draws[2].firstIndex == 6);
    CHECK (plan.draws[2].indexCount == 6);
}

TEST_CASE ("Puppet clipping plan identifies targets requiring mask composition") {
    auto mesh = makeMesh (6);
    mesh.drawRanges = {
	MdlDrawRange {.submeshIndex = 0, .firstIndex = 0, .indexCount = 3},
	MdlDrawRange {.submeshIndex = 0, .firstIndex = 3, .indexCount = 3},
    };
    mesh.clippingDescriptors.push_back (descriptor (1, { 1 }, { 0 }));
    mesh.clippingDescriptors.push_back (descriptor (2, { 1 }, { 0 }, 1));

    const auto plan = buildPuppetClippingPlan (mesh, 0);

    REQUIRE (plan.draws.size () == 2);
    CHECK (plan.draws[1].masks == std::vector<size_t> { 0, 1 });
    CHECK (plan.requiresComposition);
}

TEST_CASE ("Puppet clipping plan rejects overlapping serialized ranges") {
    auto mesh = makeMesh (9);
    mesh.drawRanges = {
	MdlDrawRange {.submeshIndex = 0, .firstIndex = 0, .indexCount = 6},
	MdlDrawRange {.submeshIndex = 0, .firstIndex = 3, .indexCount = 6},
    };

    CHECK_THROWS_AS (buildPuppetClippingPlan (mesh, 0), std::invalid_argument);
}

TEST_CASE ("Puppet clipping plan rejects missing descriptor ranges") {
    auto mesh = makeMesh (3);
    mesh.drawRanges = {
	MdlDrawRange {.submeshIndex = 0, .firstIndex = 0, .indexCount = 3},
    };

    mesh.clippingDescriptors.push_back (descriptor (1, { 1 }, { 0 }));
    CHECK_THROWS_AS (buildPuppetClippingPlan (mesh, 0), std::invalid_argument);

    mesh.clippingDescriptors.clear ();
    mesh.clippingDescriptors.push_back (descriptor (1, { 0 }, { 1 }));
    CHECK_THROWS_AS (buildPuppetClippingPlan (mesh, 0), std::invalid_argument);
}
