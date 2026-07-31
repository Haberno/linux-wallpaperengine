#include "PuppetClippingPlan.h"

#include <algorithm>
#include <stdexcept>

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Render::Objects;

namespace {
bool sameMask (
    const MdlClippingDescriptor& left, const MdlClippingDescriptor& right
) {
    return left.opaqueId == right.opaqueId && left.maskAsset == right.maskAsset && left.flags == right.flags
	&& left.sources == right.sources;
}

void appendDraw (
    PuppetClippingPlan& plan, const uint32_t submeshIndex, const uint32_t firstIndex, const uint32_t indexCount,
    const std::vector<size_t>& masks
) {
    if (indexCount == 0) {
	return;
    }

    if (!plan.draws.empty ()) {
	auto& previous = plan.draws.back ();
	if (previous.submeshIndex == submeshIndex && previous.firstIndex + previous.indexCount == firstIndex
	    && previous.masks == masks) {
	    previous.indexCount += indexCount;
	    return;
	}
    }

    plan.draws.push_back (PuppetClipDraw {
	.submeshIndex = submeshIndex,
	.firstIndex = firstIndex,
	.indexCount = indexCount,
	.masks = masks,
    });
}
}

PuppetClippingPlan WallpaperEngine::Render::Objects::buildPuppetClippingPlan (
    const MdlMesh& mesh, const uint32_t submeshIndex
) {
    if (submeshIndex >= mesh.submeshes.size ()) {
	throw std::invalid_argument ("puppet clipping plan references a missing submesh");
    }

    PuppetClippingPlan plan;
    std::vector<size_t> descriptorMasks (mesh.clippingDescriptors.size ());

    for (size_t descriptorIndex = 0; descriptorIndex < mesh.clippingDescriptors.size (); descriptorIndex++) {
	const auto& descriptor = mesh.clippingDescriptors[descriptorIndex];
	for (const uint32_t source : descriptor.sources) {
	    if (source >= mesh.drawRanges.size ()) {
		throw std::invalid_argument ("puppet clipping plan references a missing source range");
	    }
	}
	const auto existing = std::find_if (
	    plan.masks.begin (), plan.masks.end (), [&mesh, &descriptor] (const PuppetClipMask& mask) {
		return sameMask (mesh.clippingDescriptors[mask.descriptorIndex], descriptor);
	    }
	);

	if (existing != plan.masks.end ()) {
	    descriptorMasks[descriptorIndex] = static_cast<size_t> (existing - plan.masks.begin ());
	    continue;
	}

	descriptorMasks[descriptorIndex] = plan.masks.size ();
	plan.masks.push_back (PuppetClipMask {
	    .descriptorIndex = descriptorIndex,
	    .sourceRanges = descriptor.sources,
	});
    }

    std::vector<std::vector<size_t>> rangeMasks (mesh.drawRanges.size ());
    for (size_t descriptorIndex = 0; descriptorIndex < mesh.clippingDescriptors.size (); descriptorIndex++) {
	for (const uint32_t target : mesh.clippingDescriptors[descriptorIndex].targets) {
	    if (target >= rangeMasks.size ()) {
		throw std::invalid_argument ("puppet clipping plan references a missing target range");
	    }
	    auto& masks = rangeMasks[target];
	    const size_t mask = descriptorMasks[descriptorIndex];
	    if (std::find (masks.begin (), masks.end (), mask) == masks.end ()) {
		masks.push_back (mask);
	    }
	    plan.requiresComposition |= masks.size () > 1;
	}
    }

    std::vector<uint32_t> ranges;
    for (uint32_t rangeIndex = 0; rangeIndex < mesh.drawRanges.size (); rangeIndex++) {
	if (mesh.drawRanges[rangeIndex].submeshIndex == submeshIndex
	    && mesh.drawRanges[rangeIndex].indexCount != 0) {
	    ranges.push_back (rangeIndex);
	}
    }
    std::stable_sort (ranges.begin (), ranges.end (), [&mesh] (const uint32_t left, const uint32_t right) {
	return mesh.drawRanges[left].firstIndex < mesh.drawRanges[right].firstIndex;
    });

    uint32_t cursor = 0;
    const size_t submeshIndexCount = mesh.submeshes[submeshIndex].indices.size ();
    for (const uint32_t rangeIndex : ranges) {
	const auto& range = mesh.drawRanges[rangeIndex];
	if (range.firstIndex < cursor) {
	    throw std::invalid_argument ("overlapping puppet draw ranges");
	}
	if (range.firstIndex > cursor) {
	    appendDraw (plan, submeshIndex, cursor, range.firstIndex - cursor, {});
	}

	appendDraw (plan, submeshIndex, range.firstIndex, range.indexCount, rangeMasks[rangeIndex]);
	cursor = range.firstIndex + range.indexCount;
    }

    if (cursor < submeshIndexCount) {
	appendDraw (
	    plan, submeshIndex, cursor, static_cast<uint32_t> (submeshIndexCount - cursor), {}
	);
    }

    return plan;
}
