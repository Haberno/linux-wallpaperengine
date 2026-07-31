#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "WallpaperEngine/Data/Model/MdlMesh.h"

namespace WallpaperEngine::Render::Objects {
struct PuppetClipMask {
    /** Descriptor whose asset and flags define this mask pass. */
    size_t descriptorIndex = 0;
    /** Range-table entries drawn into the mask. */
    std::vector<uint32_t> sourceRanges;
};

struct PuppetClipDraw {
    uint32_t submeshIndex = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    /**
     * Empty for an ordinary material draw. One entry selects a clipping mask.
     * Multiple entries require the intermediate composition path.
     */
    std::vector<size_t> masks;
};

struct PuppetClippingPlan {
    std::vector<PuppetClipMask> masks;
    std::vector<PuppetClipDraw> draws;
    bool requiresComposition = false;
};

/**
 * Converts the serialized range/descriptor tables into ordered index-buffer spans.
 * Uncovered gaps are retained as ordinary draws, descriptors sharing one authored
 * mask are deduplicated, and adjacent spans with identical state are coalesced.
 */
PuppetClippingPlan buildPuppetClippingPlan (
    const WallpaperEngine::Data::Model::MdlMesh& mesh, uint32_t submeshIndex
);
} // namespace WallpaperEngine::Render::Objects
