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

TEST_CASE ("Puppet clipping plan preserves independent identical descriptors") {
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

    REQUIRE (plan.masks.size () == 2);
    CHECK (plan.masks[0].sourceRanges == std::vector<uint32_t> { 0, 1 });
    CHECK (plan.masks[1].sourceRanges == std::vector<uint32_t> { 0, 1 });
    REQUIRE (plan.draws.size () == 4);
    CHECK (plan.draws[0].firstIndex == 0);
    CHECK (plan.draws[0].indexCount == 873);
    CHECK (plan.draws[0].masks.empty ());
    CHECK (plan.draws[1].firstIndex == 873);
    CHECK (plan.draws[1].indexCount == 240);
    CHECK (plan.draws[1].masks == std::vector<size_t> { 0 });
    CHECK (plan.draws[2].firstIndex == 1113);
    CHECK (plan.draws[2].indexCount == 465);
    CHECK (plan.draws[2].masks == std::vector<size_t> { 1 });
    CHECK (plan.draws[3].firstIndex == 1578);
    CHECK (plan.draws[3].indexCount == 2052);
    CHECK (plan.draws[3].masks.empty ());
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

TEST_CASE ("Puppet clipping plan repeats shared targets for independent masks") {
    auto mesh = makeMesh (6);
    mesh.drawRanges = {
	MdlDrawRange {.submeshIndex = 0, .firstIndex = 0, .indexCount = 3},
	MdlDrawRange {.submeshIndex = 0, .firstIndex = 3, .indexCount = 3},
    };
    mesh.clippingDescriptors.push_back (descriptor (1, { 1 }, { 0 }));
    mesh.clippingDescriptors.push_back (descriptor (2, { 1 }, { 0 }, 1));

    const auto plan = buildPuppetClippingPlan (mesh, 0);

    REQUIRE (plan.draws.size () == 3);
    CHECK (plan.draws[1].masks == std::vector<size_t> { 0 });
    CHECK (plan.draws[2].masks == std::vector<size_t> { 1 });
    CHECK_FALSE (plan.requiresComposition);
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

TEST_CASE ("Puppet animated depth preserves signed ordering, ties and clipping membership") {
    MdlMesh mesh;
    mesh.submeshes.resize (1);
    mesh.submeshes.front ().flags = 8;
    mesh.submeshes.front ().indices.resize (6);
    mesh.drawRanges = {
	{ .partId = 0, .firstIndex = 0, .indexCount = 3 },
	{ .partId = 1, .depthOffset = -30, .firstIndex = 3, .indexCount = 3 },
    };
    mesh.clippingDescriptors.push_back (descriptor (1, { 1 }, { 0 }));
    std::vector<uint32_t> order;
    CHECK (updatePuppetDrawOrder (mesh, { 10, 20 }, { 0, 0 }, order));
    CHECK (order == std::vector<uint32_t> { 1, 0 });
    // Both keys truncate to 10: stable ties retain the previous frame's order.
    CHECK_FALSE (updatePuppetDrawOrder (mesh, { 10, 20 }, { 0.2f, 20.8f }, order));
    CHECK_FALSE (updatePuppetDrawOrder (mesh, { 10, 20 }, {}, order));
    const auto plan = buildPuppetClippingPlan (mesh, 0, &order);
    REQUIRE (plan.draws.size () == 2);
    CHECK (plan.draws[0].firstIndex == 0);
    CHECK (plan.draws[0].masks.empty ());
    CHECK (plan.draws[1].firstIndex == 3);
    CHECK (plan.draws[1].masks == std::vector<size_t> { 0 });
    CHECK (plan.masks.front ().sourceRanges == std::vector<uint32_t> { 0 });
    mesh.submeshes.front ().indices.resize (9);
    const auto gaps = buildPuppetClippingPlan (mesh, 0, &order);
    REQUIRE (gaps.draws.size () == 3);
    CHECK (gaps.draws[0].firstIndex == 0);
    CHECK (gaps.draws[2].firstIndex == 6);
    CHECK (gaps.draws[2].indexCount == 3);
    mesh.drawRanges[0].depthOffset = 1;
    mesh.drawRanges[1].depthOffset = 0;
    CHECK (updatePuppetDrawOrder (mesh, { INT32_MAX, 0 }, { 0, 0 }, order));
    CHECK (order == std::vector<uint32_t> { 0, 1 });
}

TEST_CASE ("Puppet clipping independent descriptors emit at their last source") {
    // Mutsumi's six serialized relationships, with each range shortened to one triangle.
    // Native 14020b720 emits independent target draws; shared targets are not parents.
    auto mesh = makeMesh (51);
    for (uint32_t range = 0; range < 17; range++) {
	mesh.drawRanges.push_back (MdlDrawRange { .firstIndex = range * 3, .indexCount = 3 });
    }
    mesh.clippingDescriptors = {
	descriptor (623, { 15 }, { 0, 2 }, 1),
	descriptor (276, { 12 }, { 0, 1, 2, 3 }),
	descriptor (276, { 11 }, { 0, 1, 2, 3 }),
	descriptor (276, { 13 }, { 0, 1, 2, 3 }),
	descriptor (276, { 15 }, { 0, 1, 2, 3 }),
	descriptor (276, { 14 }, { 0, 1, 2, 3 }),
    };
    const auto plan = buildPuppetClippingPlan (mesh, 0);
    CHECK_FALSE (plan.requiresComposition);
    REQUIRE (plan.masks.size () == 6);
    CHECK (plan.masks[0].sourceRanges == std::vector<uint32_t> { 0, 1, 2 });
    CHECK (plan.masks[1].sourceRanges == std::vector<uint32_t> { 0, 1, 2, 3 });
    REQUIRE (plan.draws.size () == 10);
    std::vector<uint32_t> starts, counts;
    std::vector<std::vector<size_t>> masks;
    for (const auto& draw : plan.draws) {
	starts.push_back (draw.firstIndex);
	counts.push_back (draw.indexCount);
	masks.push_back (draw.masks);
    }
    CHECK (starts == std::vector<uint32_t> { 0, 45, 9, 33, 36, 39, 42, 45, 12, 48 });
    CHECK (counts == std::vector<uint32_t> { 9, 3, 3, 3, 3, 3, 3, 3, 21, 3 });
    CHECK (masks == std::vector<std::vector<size_t>> { {}, { 0 }, {}, { 2 }, { 1 }, { 3 }, { 5 }, { 4 }, {}, {} });
}

TEST_CASE ("Puppet clipping source spans follow animated order and exclude targets") {
    auto mesh = makeMesh (15);
    for (uint32_t range = 0; range < 5; range++) {
	mesh.drawRanges.push_back (MdlDrawRange { .firstIndex = range * 3, .indexCount = 3 });
    }
    mesh.clippingDescriptors = { descriptor (1, { 1, 4 }, { 0, 2 }) };
    const auto initial = buildPuppetClippingPlan (mesh, 0);
    CHECK (initial.masks[0].sourceRanges == std::vector<uint32_t> { 0, 2 });
    const std::vector<uint32_t> order { 0, 3, 2, 1, 4 };
    const auto animated = buildPuppetClippingPlan (mesh, 0, &order);
    CHECK (animated.masks[0].sourceRanges == std::vector<uint32_t> { 0, 3, 2 });
    REQUIRE (animated.draws.size () == 5);
    CHECK (animated.draws[3].firstIndex == 3);
    CHECK (animated.draws[3].indexCount == 3);
    CHECK (animated.draws[3].masks == std::vector<size_t> { 0 });
}

TEST_CASE ("Puppet clipping parent sources retain the nested composition boundary") {
    auto mesh = makeMesh (9);
    mesh.drawRanges = {
	{ .firstIndex = 0, .indexCount = 3 },
	{ .firstIndex = 3, .indexCount = 3 },
	{ .firstIndex = 6, .indexCount = 3 },
    };
    mesh.clippingDescriptors = { descriptor (1, { 1 }, { 0 }), descriptor (2, { 2 }, { 1 }) };
    CHECK (buildPuppetClippingPlan (mesh, 0).requiresComposition);
}
