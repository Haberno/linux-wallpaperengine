#pragma once

#include "WallpaperEngine/Data/Model/MdlMesh.h"

#include <bit>
#include <cmath>
#include <limits>

namespace WallpaperEngine::Render::Objects {
/** Conservative bounds for the position/skinning transform used by model shadows. */
class ModelBounds {
public:
    struct Box {
	glm::vec3 minimum = glm::vec3 (std::numeric_limits<float>::infinity ());
	glm::vec3 maximum = glm::vec3 (-std::numeric_limits<float>::infinity ());
	bool uncertain = false;

	void include (const glm::vec3& point) {
	    for (int axis = 0; axis < 3; ++axis) {
		if (!std::isfinite (point[axis])) uncertain = true;
	    }
	    minimum = glm::min (minimum, point);
	    maximum = glm::max (maximum, point);
	}

	[[nodiscard]] bool valid () const {
	    return !uncertain && glm::all (glm::lessThanEqual (minimum, maximum));
	}

	[[nodiscard]] bool intersects (const glm::mat4& clipFromLocal) const {
	    if (!valid ()) return true;
	    const auto center = minimum * 0.5f + maximum * 0.5f;
	    const auto extent = maximum * 0.5f - minimum * 0.5f;
	    // Reject only when every point is outside the same homogeneous clip plane.
	    for (int axis = 0; axis < 3; ++axis) {
		for (const float sign : { -1.0f, 1.0f }) {
		    glm::vec4 plane;
		    for (int column = 0; column < 4; ++column) {
			plane[column] = clipFromLocal[column][3] + sign * clipFromLocal[column][axis];
		    }
		    const float distance = glm::dot (glm::vec3 (plane), center) + plane.w;
		    const float radius = glm::dot (glm::abs (glm::vec3 (plane)), extent);
		    const float magnitude = glm::dot (glm::abs (glm::vec3 (plane)), glm::abs (center))
			+ std::abs (plane.w) + radius;
		    if (std::isfinite (magnitude) && distance + radius < -1e-5f * (1.0f + magnitude)) {
			return false;
		    }
		}
	    }
	    return true;
	}
    };

    ModelBounds () = default;
    ModelBounds (const Data::Model::MdlMesh& mesh, const size_t boneCount) : m_bones (boneCount) {
	const size_t stride = mesh.strideBytes / sizeof (float);
	const auto fits = [&] (uint32_t offset, size_t count) {
	    return offset % sizeof (float) == 0 && size_t (offset) + count * sizeof (float) <= mesh.strideBytes;
	};
	if (stride == 0 || mesh.strideBytes % sizeof (float) != 0 || !fits (mesh.positionOffset, 3)) return;
	if (boneCount != 0 && (!fits (mesh.blendIndicesOffset, 4) || !fits (mesh.blendWeightsOffset, 4))) return;
	for (const auto& part : mesh.submeshes) {
	    if (part.vertexTag != mesh.vertexTag || part.indices.empty ()) continue;
	    if (part.vertices.size () % stride != 0) return;
	    for (size_t vertex = 0; vertex < part.vertices.size (); vertex += stride) {
		const float* data = part.vertices.data () + vertex;
		const auto offset = mesh.positionOffset / sizeof (float);
		const glm::vec3 point (data[offset], data[offset + 1], data[offset + 2]);
		m_static.include (point);
		if (!m_static.valid ()) return;
		if (boneCount == 0) continue;
		float sum = 0.0f;
		for (size_t channel = 0; channel < 4; ++channel) {
		    const auto bone = std::bit_cast<uint32_t> (data[mesh.blendIndicesOffset / sizeof (float) + channel]);
		    const float weight = data[mesh.blendWeightsOffset / sizeof (float) + channel];
		    if (bone >= boneCount || !std::isfinite (weight) || weight < 0.0f) return;
		    if (weight > 0.0f) m_bones[bone].include (point);
		    sum += weight;
		}
		if (!std::isfinite (sum)) return;
		m_minWeight = std::min (m_minWeight, sum);
		m_maxWeight = std::max (m_maxWeight, sum);
	    }
	}
	m_valid = m_static.valid ();
    }

    [[nodiscard]] Box evaluate (const std::vector<glm::mat4x3>& bones) const {
	if (!m_valid) return {};
	if (m_bones.empty ()) return m_static;
	if (bones.size () != m_bones.size ()) return {};
	Box result;
	for (size_t index = 0; index < bones.size (); ++index) {
	    const auto& box = m_bones[index];
	    if (!box.valid ()) continue;
	    const auto& transform = bones[index];
	    const auto center = transform * glm::vec4 (box.minimum * 0.5f + box.maximum * 0.5f, 1.0f);
	    const auto extent = box.maximum * 0.5f - box.minimum * 0.5f;
	    const auto radius = glm::abs (transform[0]) * extent.x + glm::abs (transform[1]) * extent.y
		+ glm::abs (transform[2]) * extent.z;
	    result.include (center - radius);
	    result.include (center + radius);
	}
	if (m_maxWeight == 0.0f) {
	    result.include (glm::vec3 (0.0f));
	} else if (result.valid ()) {
	    // Positive weighted positions are convex combinations of these boxes,
	    // scaled by the weight sum (which need not be exactly one).
	    const auto low = glm::min (result.minimum * m_minWeight, result.minimum * m_maxWeight);
	    const auto high = glm::max (result.maximum * m_minWeight, result.maximum * m_maxWeight);
	    result.minimum = low;
	    result.maximum = high;
	}
	return result;
    }

private:
    Box m_static;
    std::vector<Box> m_bones;
    float m_minWeight = std::numeric_limits<float>::infinity ();
    float m_maxWeight = 0.0f;
    bool m_valid = false;
};
} // namespace WallpaperEngine::Render::Objects
