#include "PuppetClippingPlan.h"

#include <algorithm>
#include <bit>
#include <numeric>
#include <stdexcept>

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Render::Objects;

namespace {
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
    const MdlMesh& mesh, const uint32_t submeshIndex, const std::vector<uint32_t>* rangeOrder
) {
    if (submeshIndex >= mesh.submeshes.size ()) {
	throw std::invalid_argument ("puppet clipping plan references a missing submesh");
    }

    PuppetClippingPlan plan;
    std::vector<std::vector<size_t>> rangeMasks (mesh.drawRanges.size ());
    for (size_t descriptorIndex = 0; descriptorIndex < mesh.clippingDescriptors.size (); descriptorIndex++) {
	const auto& descriptor = mesh.clippingDescriptors[descriptorIndex];
	for (const uint32_t source : descriptor.sources) {
	    if (source >= mesh.drawRanges.size ())
		throw std::invalid_argument ("puppet clipping plan references a missing source range");
	}
	for (const uint32_t target : descriptor.targets) {
	    if (target >= mesh.drawRanges.size ())
		throw std::invalid_argument ("puppet clipping plan references a missing target range");
	    rangeMasks[target].push_back (descriptorIndex);
	}
	plan.masks.push_back (PuppetClipMask {
	    .descriptorIndex = descriptorIndex,
	    .sourceRanges = descriptor.sources,
	});
    }

    // Native resolves the graph incrementally and breaks only the current cycle edge.
    std::vector<int32_t> parents (plan.masks.size (), -1);
    for (size_t index = 0; index < plan.masks.size (); index++) {
	const auto& sources = mesh.clippingDescriptors[index].sources;
	if (sources.empty ()) continue;
	for (size_t other = 0; other < plan.masks.size (); other++) {
	    if (other == index) continue;
	    const auto& targets = mesh.clippingDescriptors[other].targets;
	    if (std::all_of (sources.begin (), sources.end (), [&] (auto source) {
		return std::find (targets.begin (), targets.end (), source) != targets.end ();
	    })) {
		parents[index] = static_cast<int32_t> (other);
		break;
	    }
	}
	std::vector<bool> visited (parents.size ());
	for (int32_t ancestor = static_cast<int32_t> (index); ancestor >= 0; ancestor = parents[ancestor]) {
	    if (visited[ancestor]) { parents[index] = -1; break; }
	    visited[ancestor] = true;
	}
    }
    for (size_t index = 0; index < plan.masks.size (); index++) {
	for (int32_t ancestor = parents[index]; ancestor >= 0; ancestor = parents[ancestor])
	    plan.masks[index].ancestors.push_back (static_cast<size_t> (ancestor));
	plan.requiresComposition |= !plan.masks[index].ancestors.empty ();
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

    // Preserve the existing gap-filling route for incomplete authored tables.
    size_t covered = 0;
    for (const auto index : ranges) covered += mesh.drawRanges[index].indexCount;
    const bool unsupported = std::any_of (mesh.clippingDescriptors.begin (), mesh.clippingDescriptors.end (), [] (const auto& descriptor) {
	return (descriptor.flags & ~1u) != 0;
    });
    const bool foreignRanges = std::any_of (mesh.drawRanges.begin (), mesh.drawRanges.end (), [submeshIndex] (const auto& range) {
	return range.submeshIndex != submeshIndex;
    });
    if (covered != submeshIndexCount || unsupported || foreignRanges) {
	if (covered != submeshIndexCount)
	    for (const auto& masks : rangeMasks) plan.requiresComposition |= masks.size () > 1;
	if (rangeOrder && !rangeOrder->empty () && covered == submeshIndexCount) {
	    plan.draws.clear ();
	    for (const uint32_t rangeIndex : *rangeOrder) {
		const auto& range = mesh.drawRanges.at (rangeIndex);
		if (range.submeshIndex == submeshIndex)
		    appendDraw (plan, submeshIndex, range.firstIndex, range.indexCount, rangeMasks[rangeIndex]);
	    }
	}
	plan.unsupportedComposition = plan.requiresComposition;
	return plan;
    }

    std::vector<uint32_t> orderedRanges;
    if (rangeOrder && !rangeOrder->empty ()) orderedRanges = *rangeOrder;
    else {
	for (uint32_t index = 0; index < mesh.drawRanges.size (); index++)
	    if (mesh.drawRanges[index].submeshIndex == submeshIndex) orderedRanges.push_back (index);
	std::stable_sort (orderedRanges.begin (), orderedRanges.end (), [&] (auto left, auto right) {
	    return mesh.drawRanges[left].firstIndex < mesh.drawRanges[right].firstIndex;
	});
    }
    std::vector<int32_t> ordinals (mesh.drawRanges.size (), -1);
    for (size_t ordinal = 0; ordinal < orderedRanges.size (); ordinal++)
	ordinals.at (orderedRanges[ordinal]) = static_cast<int32_t> (ordinal);

    struct DescriptorOrder { int32_t minTarget = INT32_MAX, lastSource = -1, emissionOrdinal = -1; };
    std::vector<DescriptorOrder> descriptorOrder (plan.masks.size ());
    std::vector<size_t> descriptors (plan.masks.size ());
    std::iota (descriptors.begin (), descriptors.end (), 0u);
    for (size_t index = 0; index < plan.masks.size (); index++) {
	const auto& descriptor = mesh.clippingDescriptors[index];
	auto& state = descriptorOrder[index];
	int32_t firstSource = INT32_MAX;
	for (auto target : descriptor.targets) state.minTarget = std::min (state.minTarget, ordinals[target]);
	for (auto source : descriptor.sources) {
	    firstSource = std::min (firstSource, ordinals[source]);
	    state.lastSource = std::max (state.lastSource, ordinals[source]);
	}
	// Native spans the ordered source endpoints, excluding this descriptor's targets.
	auto& sources = plan.masks[index].sourceRanges;
	sources.clear ();
	for (int32_t ordinal = firstSource; ordinal <= state.lastSource; ordinal++) {
	    const uint32_t range = orderedRanges[ordinal];
	    if (std::find (descriptor.targets.begin (), descriptor.targets.end (), range) == descriptor.targets.end ())
		sources.push_back (range);
	}
    }
    for (size_t index = 0; index < plan.masks.size (); index++) {
	auto& state = descriptorOrder[index];
	state.emissionOrdinal = state.lastSource;
	for (const auto ancestor : plan.masks[index].ancestors)
	    state.emissionOrdinal = std::max (state.emissionOrdinal, descriptorOrder[ancestor].lastSource);
    }
    std::stable_sort (descriptors.begin (), descriptors.end (), [&] (auto left, auto right) {
	return descriptorOrder[left].minTarget < descriptorOrder[right].minTarget;
    });

    plan.draws.clear ();
    std::vector<bool> emitted (plan.masks.size ());
    for (size_t ordinal = 0; ordinal < orderedRanges.size (); ordinal++) {
	const uint32_t rangeIndex = orderedRanges[ordinal];
	const auto& range = mesh.drawRanges.at (rangeIndex);
	if (range.submeshIndex != submeshIndex) continue;
	if (rangeMasks[rangeIndex].empty ())
	    appendDraw (plan, submeshIndex, range.firstIndex, range.indexCount, {});
	// Native flushes ordinary geometry, then emits descriptors at their last source.
	for (const size_t descriptorIndex : descriptors) {
	    if (descriptorOrder[descriptorIndex].emissionOrdinal != static_cast<int32_t> (ordinal)) continue;
	    for (const auto ancestor : plan.masks[descriptorIndex].ancestors)
		plan.unsupportedComposition |= !emitted[ancestor];
	    emitted[descriptorIndex] = true;
	    const auto& targets = mesh.clippingDescriptors[descriptorIndex].targets;
	    for (const uint32_t targetIndex : orderedRanges) {
		if (std::find (targets.begin (), targets.end (), targetIndex) == targets.end ()) continue;
		const auto& target = mesh.drawRanges[targetIndex];
		appendDraw (plan, submeshIndex, target.firstIndex, target.indexCount, { descriptorIndex });
	    }
	}
    }
    if (plan.unsupportedComposition) {
	// Missing native source-group lookup would use group0. Retain an explicit
	// un-clipped draw instead, including when animated ordering enters this case.
	plan.draws.clear ();
	for (const uint32_t rangeIndex : orderedRanges) {
	    const auto& range = mesh.drawRanges[rangeIndex];
	    appendDraw (plan, submeshIndex, range.firstIndex, range.indexCount, {});
	}
    }
    return plan;
}

bool WallpaperEngine::Render::Objects::updatePuppetDrawOrder (
    const MdlMesh& mesh, const std::vector<int32_t>& defaults, const std::vector<float>& depths,
    std::vector<uint32_t>& order
) {
    if (depths.empty () || mesh.drawRanges.empty () || mesh.submeshes.empty ()
	|| (mesh.submeshes.front ().flags & 8) == 0) return false;
    if (std::any_of (mesh.drawRanges.begin (), mesh.drawRanges.end (), [&] (const auto& range) {
	return range.partId >= depths.size () || range.partId >= defaults.size ();
    })) return false;
    const bool initialized = order.empty ();
    if (order.empty ()) {
	order.resize (mesh.drawRanges.size ());
	std::iota (order.begin (), order.end (), 0u);
    }
    // Native 14020ae00 sets base = MDLS depth + signed range offset;
    // 1401fdf90 truncates base + sampled depth to an integer before stable sorting.
    const auto key = [&] (const uint32_t index) {
	const auto& range = mesh.drawRanges[index];
	const auto base = std::bit_cast<int32_t> (
	    static_cast<uint32_t> (defaults[range.partId]) + static_cast<uint32_t> (range.depthOffset)
	);
	const float value = static_cast<float> (base) + depths[range.partId];
	// Match the native x86 float-to-signed conversion for out-of-range values.
	return value >= -2147483648.0f && value < 2147483648.0f ? static_cast<int32_t> (value) : INT32_MIN;
    };
    const auto less = [&] (const uint32_t left, const uint32_t right) { return key (left) < key (right); };
    if (std::is_sorted (order.begin (), order.end (), less)) return initialized;
    std::stable_sort (order.begin (), order.end (), less);
    return true;
}
