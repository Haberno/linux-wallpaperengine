#pragma once

#include <algorithm>
#include <cmath>
#include <span>

#include "MdlMesh.h"

namespace WallpaperEngine::Data::Model {
/** Native genericimage4 modifier, evaluated before applying any morph deltas.
 * inverseModifierBoneWorld is the inverse current worldBones[modifier.boneIndex],
 * not an inverse bind/skin matrix. Compute it once per selected target.
 */
inline float evaluatePuppetMorphModifier (
    const MdlMorphModifier& modifier, const glm::vec3& unmorphedSkinnedPosition,
    const glm::mat4& inverseModifierBoneWorld
) {
    const glm::vec3 local = inverseModifierBoneWorld * glm::vec4 (unmorphedSkinnedPosition, 1.0f);
    const float coordinate = (modifier.rule & 2) != 0 ? local.x : glm::length (glm::vec2 (local));
    return glm::smoothstep (modifier.range.x, modifier.range.y, coordinate);
}

struct MdlPuppetMorphTarget {
    const MdlMorphTarget* target;
    float weight;
    glm::mat4 inverseModifierBoneWorld { 1.0f };
};

/** Native uploads the first eleven masked targets, including targets with zero weight. */
inline std::vector<MdlPuppetMorphTarget> preparePuppetMorphTargets (
    const MdlSubmesh& body, std::span<const float> weights, uint64_t mask,
    std::span<const glm::mat4> worldBones
) {
    std::vector<MdlPuppetMorphTarget> targets;
    // This CPU path evaluates native's packed position/alpha format.
    if ((body.vertexTag & 0x10001) != 0x10000 || (body.flags & 0x1000) == 0) return targets;
    for (size_t index = 0; index < body.morphTargets.size () && index < weights.size ()
	 && index < 64 && targets.size () < 11; ++index) {
	if ((mask & (uint64_t { 1 } << index)) == 0) continue;
	const auto& target = body.morphTargets[index];
	MdlPuppetMorphTarget selected { .target = &target, .weight = weights[index] };
	if ((body.flags & 0x2000) != 0) {
	    if (!target.modifier || target.modifier->boneIndex >= worldBones.size ()) return {};
	    selected.inverseModifierBoneWorld = glm::inverse (worldBones[target.modifier->boneIndex]);
	}
	targets.push_back (selected);
    }
    return targets;
}

/** Returns the model-space XYZ offset and native multiplicative vertex alpha. */
inline glm::vec4 evaluatePuppetMorph (
    const MdlSubmesh& body, std::span<const MdlPuppetMorphTarget> targets, float morphIndex,
    const glm::vec3& unmorphedSkinnedPosition, float* maskAlpha = nullptr
) {
    if (maskAlpha) *maskAlpha = 1.0f;
    if (!std::isfinite (morphIndex) || morphIndex < 1.0f || morphIndex >= body.morphVertexCount + 1.0f)
	return glm::vec4 (0, 0, 0, 1);
    const size_t component = (static_cast<size_t> (morphIndex) - 1) * 3;
    glm::vec4 delta (0, 0, 0, 1);
    for (const auto& selected : targets) {
	const auto& target = *selected.target;
	if (component + 2 >= target.positionDeltas.size ()) continue;
	float weight = selected.weight;
	if ((body.flags & 0x2000) != 0)
	    weight *= evaluatePuppetMorphModifier (
		*target.modifier, unmorphedSkinnedPosition, selected.inverseModifierBoneWorld
	    );
	for (int axis = 0; axis < 3; ++axis)
	    delta[axis] += std::max (target.positionDeltas[component + axis] / 32767.0f, -1.0f) * weight;
	const size_t vertex = component / 3;
	if (vertex < target.alphaValues.size ()) {
	    const float alpha = std::max (target.alphaValues[vertex] / 32767.0f, -1.0f);
	    delta.w *= alpha * weight + 1.0f - weight;
	    // clippingmaskimage4 uses the raw clip weight for alpha, even when
	    // its position and the ordinary color shader use spatial modifiers.
	    if (maskAlpha) *maskAlpha *= alpha * selected.weight + 1.0f - selected.weight;
	}
    }
    return delta * glm::vec4 (glm::vec3 (body.morphScale), 1.0f);
}
inline glm::vec3 evaluatePuppetMorphOffset (
    const MdlSubmesh& body, std::span<const MdlPuppetMorphTarget> targets, float morphIndex,
    const glm::vec3& unmorphedSkinnedPosition
) {
    return glm::vec3 (evaluatePuppetMorph (body, targets, morphIndex, unmorphedSkinnedPosition));
}
/** The general model shader indexes packed XYZ/normal deltas by vertex ID.
 * 1401d7760 uploads RGBA16_SNORM; model_vertex_v1.h applies them before skinning.
 * Tangent-bearing maps use a separate unsupported native upload branch.
 */
inline bool deformModelMorphVertices (
    const MdlMesh& mesh, const MdlSubmesh& body, const std::vector<float>& weights,
    uint64_t mask, std::vector<float>& vertices
) {
    if (mask == 0 || body.morphTargets.empty () || (body.flags & 0x800) != 0
        || body.vertexTag != mesh.vertexTag || (mesh.vertexTag & 0x10001) != 1
        || mesh.strideBytes == 0 || body.morphVertexCount != body.vertices.size () * sizeof (float) / mesh.strideBytes)
        return false;
    const bool normals = (body.flags & 0x400) != 0 && mesh.normalOffset != MdlMesh::AttributeAbsent;
    std::vector<size_t> selected;
    for (size_t index = 0; index < body.morphTargets.size () && index < weights.size () && index < 64; ++index) {
        if ((mask & (uint64_t { 1 } << index)) == 0) continue;
        const auto& target = body.morphTargets[index];
        if (target.positionDeltas.size () != body.morphVertexCount * 3
            || (normals && target.normalDeltas.size () != body.morphVertexCount * 3)) return false;
        selected.push_back (index);
        if (selected.size () == 11) break;
    }
    if (selected.empty ()) return false;
    // Native selects the first eleven active indices, then orders them by weight.
    std::stable_sort (selected.begin (), selected.end (), [&] (size_t left, size_t right) {
        return weights[left] > weights[right];
    });
    vertices = body.vertices;
    const size_t stride = mesh.strideBytes / sizeof (float);
    const auto snorm = [] (int16_t value) { return std::max (float (value) / 32767.0f, -1.0f); };
    for (size_t vertex = 0; vertex < body.morphVertexCount; ++vertex) {
        glm::vec3 positionDelta (0.0f), normalDelta (0.0f);
        for (const auto index : selected) {
            const auto& target = body.morphTargets[index];
            for (size_t axis = 0; axis < 3; ++axis) {
                positionDelta[axis] += snorm (target.positionDeltas[vertex * 3 + axis]) * weights[index];
                if (normals) normalDelta[axis] += snorm (target.normalDeltas[vertex * 3 + axis]) * weights[index];
            }
        }
        const size_t position = vertex * stride + mesh.positionOffset / sizeof (float);
        for (size_t axis = 0; axis < 3; ++axis) vertices[position + axis] += positionDelta[axis] * body.morphScale;
        if (normals) {
            const size_t offset = vertex * stride + mesh.normalOffset / sizeof (float);
            const auto normal = glm::normalize (glm::vec3 (vertices[offset], vertices[offset + 1], vertices[offset + 2])
                + normalDelta * 3.465f);
            for (size_t axis = 0; axis < 3; ++axis) vertices[offset + axis] = normal[axis];
        }
    }
    return true;
}
} // namespace WallpaperEngine::Data::Model
