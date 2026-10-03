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
} // namespace WallpaperEngine::Data::Model
