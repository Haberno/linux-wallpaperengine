#include "MdlAnimation.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

using namespace WallpaperEngine::Data::Model;

namespace {
glm::mat4 poseMatrix (const MdlBoneFrame& pose) {
    glm::mat4 matrix = glm::translate (glm::mat4 (1.0f), pose.translation);
    matrix *= glm::mat4_cast (pose.rotation);
    return glm::scale (matrix, pose.scale);
}

MdlBoneFrame matrixPose (const glm::mat4& matrix) {
    MdlBoneFrame pose;
    pose.translation = glm::vec3 (matrix[3]);
    pose.scale = {
	glm::length (glm::vec3 (matrix[0])),
	glm::length (glm::vec3 (matrix[1])),
	glm::length (glm::vec3 (matrix[2])),
    };

    glm::mat3 rotation (1.0f);
    for (int column = 0; column < 3; column++) {
	if (pose.scale[column] > 1e-8f) {
	    rotation[column] = glm::vec3 (matrix[column]) / pose.scale[column];
	}
    }
    if (glm::determinant (rotation) < 0.0f) {
	pose.scale.x = -pose.scale.x;
	rotation[0] = -rotation[0];
    }
    pose.rotation = glm::normalize (glm::quat_cast (rotation));
    return pose;
}

MdlBoneFrame blendPose (const MdlBoneFrame& from, const MdlBoneFrame& to, const float weight) {
    MdlBoneFrame result;
    result.translation = glm::mix (from.translation, to.translation, weight);
    result.rotation = glm::normalize (glm::slerp (from.rotation, to.rotation, weight));
    result.scale = glm::mix (from.scale, to.scale, weight);
    return result;
}

/** Playhead position in frames, wrapped according to the clip's play mode. */
float sampleFrame (const MdlActiveAnimation& layer) {
    const auto& animation = *layer.animation;
    const float frameCount = static_cast<float> (animation.frameCount);
    if (layer.frame.has_value ()) return std::clamp (*layer.frame, 0.0f, frameCount);
    float frame = layer.time * animation.fps;

    if (animation.mode == "single") {
	frame = std::clamp (frame, 0.0f, frameCount);
    } else if (animation.mode == "mirror" && frameCount > 0.0f) {
	frame = std::fmod (frame, frameCount * 2.0f);
	if (frame < 0.0f) {
	    frame += frameCount * 2.0f;
	}
	if (frame > frameCount) {
	    frame = frameCount * 2.0f - frame;
	}
    } else if (frameCount > 0.0f) {
	frame = std::fmod (frame, frameCount);
	if (frame < 0.0f) {
	    frame += frameCount;
	}
    } else {
	frame = 0.0f;
    }

    return frame;
}

MdlBoneFrame samplePose (const MdlActiveAnimation& layer, const std::vector<MdlBoneFrame>& frames) {
    const float frame = sampleFrame (layer);
    const auto firstFrame = static_cast<size_t> (frame);
    const float blend = frame - static_cast<float> (firstFrame);
    const auto& current = frames[std::min (firstFrame, frames.size () - 1)];
    const auto& next = frames[std::min (firstFrame + 1, frames.size () - 1)];
    return blendPose (current, next, blend);
}

void composeLayer (
    MdlBoneFrame& pose, const MdlBoneFrame& reference, const MdlBoneFrame& sampled,
    const MdlActiveAnimation& layer
) {
    const float weight = std::clamp (layer.weight, 0.0f, 1.0f);
    if (layer.additive) {
	pose.translation += (sampled.translation - reference.translation) * weight;
	const glm::quat delta = glm::normalize (glm::inverse (reference.rotation) * sampled.rotation);
	pose.rotation = glm::normalize (
	    pose.rotation * glm::slerp (glm::quat (1.0f, 0.0f, 0.0f, 0.0f), delta, weight)
	);
	pose.scale += (sampled.scale - reference.scale) * weight;
    } else {
	pose = blendPose (pose, sampled, weight);
    }
}

glm::vec3 directionOr (const glm::vec3& vector, const glm::vec3& fallback) {
    const float length = glm::length (vector);
    return length > 1e-6f ? vector / length : fallback;
}

/** Fixed-length, independent IK chains, as evaluated by the native 140271910 solver. */
void applyBoneControls (
    const MdlAnimationData& data, const std::vector<MdlActiveAnimation>& layers,
    const std::vector<glm::mat4>& locals, MdlPose& pose
) {
    std::vector<MdlBoneFrame> controls;
    for (size_t index = 0; index < data.controls.size (); ++index) {
	const auto& control = data.controls[index];
	const auto reference = matrixPose (control.referenceWorld.value_or (control.bindWorld));
	auto value = reference;
	for (const auto& layer : layers) {
	    if (!layer.animation || layer.weight <= 0.0f || index >= layer.animation->controlFrames.size ())
		continue;
	    if (index < layer.animation->controlFlags.size () && (layer.animation->controlFlags[index] & 1))
		continue;
	    const auto& frames = layer.animation->controlFrames[index];
	    if (!frames.empty ()) composeLayer (value, reference, samplePose (layer, frames), layer);
	}
	controls.push_back (value);
    }
    const auto findControl = [&] (const uint32_t bone, const uint32_t type) -> const MdlBoneFrame* {
	for (size_t index = 0; index < data.controls.size (); ++index)
	    if (data.controls[index].bone == bone && data.controls[index].type == type) return &controls[index];
	return nullptr;
    };
    std::vector<bool> solved (data.bones.size (), false);
    for (const auto& group : data.ikGroups) {
	// Coupled branches, scalar dependencies, joint limits and physics need their
	// own constraint pass. Do not approximate those rigs with an independent chain.
	if (group.hasDependencies || group.nodes.size () != 1 || group.nodes.front ().branches.size () != 1)
	    continue;
	const auto& chain = group.nodes.front ().branches.front ();
	if (chain.bones.size () < 2 || chain.flags != 1 || chain.bones.front () != group.root) continue;
	const auto tip = chain.bones.back ();
	const auto* target = findControl (tip, 0);
	if (!target) continue;
	bool valid = true;
	for (size_t index = 0; index < chain.bones.size (); ++index) {
	    const auto bone = chain.bones[index];
	    if (bone >= data.bones.size () || bone >= data.boneLengths.size ()
		|| bone >= data.boneDirections.size () || data.bones[bone].ikAngleLimits) {
		valid = false;
		break;
	    }
	    if (index && (data.bones[bone].parent != static_cast<int32_t> (chain.bones[index - 1])
		|| data.boneLengths[bone] <= 1e-6f
		|| !data.boneDirections[chain.bones[index - 1]].contains (bone))) valid = false;
	}
	if (!valid) continue;
	std::vector<glm::vec3> points;
	for (const auto bone : chain.bones) points.emplace_back (pose.worldBones[bone][3]);
	const auto root = points.front ();
	const auto* pole = findControl (tip, 1);
	const size_t last = points.size () - 1;
	for (int iteration = 0; iteration < 10; ++iteration) {
	    glm::vec3 end = target->translation;
	    const auto axis = directionOr (end - points.front (), glm::vec3 (1, 0, 0));
	    const float distance = glm::length (end - points.front ());
	    if (distance < chain.minLength) end += axis * (chain.minLength - distance);
	    glm::vec3 poleOffset = pole ? pole->translation - end : glm::vec3 (0.0f);
	    poleOffset -= axis * glm::dot (poleOffset, axis);
	    const auto normal = directionOr (glm::cross (axis, poleOffset), glm::vec3 (0.0f));
	    points.back () = end;
	    float travelled = 0.0f;
	    for (size_t index = last; index > 0; --index) {
		glm::vec3 direction = points[index - 1] - points[index];
		const float length = data.boneLengths[chain.bones[index]];
		if (distance >= chain.maxLength) {
		    direction = -axis;
		} else if (index == last || travelled + length < chain.maxLength * 0.5f) {
		    if (glm::dot (poleOffset, poleOffset) > 0.01f) direction -= normal * glm::dot (direction, normal);
		    const float side = glm::dot (direction, poleOffset);
		    if (side < 0.0f) direction -= 2.0f * poleOffset * side;
		}
		points[index - 1] = points[index] + directionOr (direction, -axis) * length;
		travelled += length;
	    }
	    // A root attached to another bone stays pinned; a top-level chain root
	    // follows its end control. Pinning every root stretches Miss Fortune's arm.
	    if (data.bones[group.root].parent >= 0) points.front () = root;
	    for (size_t index = 1; index <= last; ++index) {
		points[index] = points[index - 1]
		    + directionOr (points[index] - points[index - 1], axis) * data.boneLengths[chain.bones[index]];
	    }
	    const auto error = points.back () - target->translation;
	    if (glm::dot (error, error) <= 0.1f) break;
	}
	for (size_t index = 0; index < last; ++index) {
	    const auto bone = chain.bones[index];
	    const auto reference = data.boneDirections[bone].at (chain.bones[index + 1]);
	    const auto direction = directionOr (points[index + 1] - points[index], reference);
	    const auto rotation = glm::rotation (directionOr (reference, direction), direction);
	    pose.worldBones[bone] = glm::mat4_cast (rotation) * glm::inverse (data.bones[bone].inverseBindWorld);
	}
	const auto& endBone = data.bones[tip];
	if (!endBone.ikFollowEnd) {
	    auto rotation = target->rotation;
	    const auto error = target->translation - points.back ();
	    const float distance = glm::length (error);
	    if (endBone.ikAimToTarget && distance > 1.0f) {
		rotation = glm::slerp (rotation, glm::rotation (glm::vec3 (1, 0, 0), error / distance),
		    std::clamp ((distance - 1.0f) / endBone.ikAimDistance, 0.0f, 1.0f));
	    }
	    pose.worldBones[tip] = glm::mat4_cast (glm::normalize (rotation));
	} else if (last > 1) {
	    pose.worldBones[tip] = pose.worldBones[chain.bones[last - 1]];
	}
	for (size_t index = 0; index <= last; ++index) {
	    const auto bone = chain.bones[index];
	    pose.worldBones[bone][3] = glm::vec4 (points[index], 1.0f);
	    solved[bone] = true;
	}
	// Recompose descendants so the gun and its attachment follow the same wrist.
	for (size_t bone = 0; bone < data.bones.size (); ++bone) {
	    if (solved[bone]) continue;
	    const auto parent = data.bones[bone].parent;
	    pose.worldBones[bone] = parent >= 0 ? pose.worldBones[parent] * locals[bone] : locals[bone];
	}
    }
    for (size_t bone = 0; bone < data.bones.size (); ++bone)
	pose.skinBones[bone] = pose.worldBones[bone] * data.bones[bone].inverseBindWorld;
}

/** Same playhead and interpolation as the bones, on a scalar track instead of a pose. */
float sampleBlendTrack (const MdlActiveAnimation& layer, const size_t row) {
    const auto& track = layer.animation->blendTracks[row];
    const float frame = sampleFrame (layer);
    const auto firstFrame = static_cast<size_t> (frame);
    const float blend = frame - static_cast<float> (firstFrame);
    const float current = track[std::min (firstFrame, track.size () - 1)];
    const float next = track[std::min (firstFrame + 1, track.size () - 1)];
    return glm::mix (current, next, blend);
}
} // namespace

MdlPose MdlAnimationEvaluator::evaluate (
    const MdlAnimationData& animationData, const std::vector<MdlActiveAnimation>& activeAnimations
) {
    MdlPose pose;
    pose.worldBones.resize (animationData.bones.size ());
    pose.skinBones.resize (animationData.bones.size ());
    std::vector<glm::mat4> locals;
    if (!animationData.ikGroups.empty ()) locals.resize (animationData.bones.size ());

    for (size_t bone = 0; bone < animationData.bones.size (); bone++) {
	// Native 1401fdf90 initializes the shared blend reference from MDLS's
	// optional reference matrices, falling back to the skeleton bind pose.
	// A clip's first frame can already reposition atlas parts; subtracting it
	// instead makes an idle additive layer displace the dragon's eye (3233141951).
	const auto& skeletonBone = animationData.bones[bone];
	const MdlBoneFrame reference = matrixPose (skeletonBone.referenceLocal.value_or (skeletonBone.bindLocal));
	MdlBoneFrame local = reference;

	for (size_t layerIndex = 0; layerIndex < activeAnimations.size (); layerIndex++) {
	    const auto& layer = activeAnimations[layerIndex];
	    if (layer.animation == nullptr || bone >= layer.animation->boneFrames.size ()) {
		continue;
	    }
	    // MDLA bit zero masks this track out of the layer. Blink-only clips
	    // retain static bone samples which must not overwrite the moving body.
	    if (bone < layer.animation->boneFlags.size () && (layer.animation->boneFlags[bone] & 1)) {
		continue;
	    }
	    const auto& frames = layer.animation->boneFrames[bone];
	    if (frames.empty () || layer.weight <= 0.0f) {
		continue;
	    }

	    // Every layer uses the same reference and composes T/R/S separately.
	    composeLayer (local, reference, samplePose (layer, frames), layer);
	}

	const auto parent = animationData.bones[bone].parent;
	const glm::mat4 localMatrix = poseMatrix (local);
	if (!locals.empty ()) locals[bone] = localMatrix;
	pose.worldBones[bone] = parent >= 0 ? pose.worldBones[parent] * localMatrix : localMatrix;
	pose.skinBones[bone] = pose.worldBones[bone] * animationData.bones[bone].inverseBindWorld;
    }

    if (!animationData.ikGroups.empty ()) applyBoneControls (animationData, activeAnimations, locals, pose);

    // scalar blend tracks compose exactly like the bones do, one value per track row
    size_t blendRows = 0;
    for (const auto& layer : activeAnimations) {
	if (layer.animation != nullptr) {
	    blendRows = std::max (blendRows, layer.animation->blendTracks.size ());
	}
    }
    pose.blendWeights.assign (blendRows, 0.0f);

    for (size_t row = 0; row < blendRows; row++) {
	const auto hasRow = [row] (const MdlActiveAnimation& layer) {
	    return layer.animation != nullptr && row < layer.animation->blendTracks.size ()
		&& !layer.animation->blendTracks[row].empty ();
	};

	float value = 0.0f;
	size_t firstComposedLayer = 0;

	if (!activeAnimations.empty ()) {
	    const auto& baseLayer = activeAnimations.front ();
	    if (hasRow (baseLayer)) {
		value = glm::mix (
		    baseLayer.animation->blendTracks[row].front (), sampleBlendTrack (baseLayer, row),
		    std::clamp (baseLayer.weight, 0.0f, 1.0f)
		);
	    }
	    firstComposedLayer = 1;
	}

	for (size_t layerIndex = firstComposedLayer; layerIndex < activeAnimations.size (); layerIndex++) {
	    const auto& layer = activeAnimations[layerIndex];
	    if (!hasRow (layer) || layer.weight <= 0.0f) {
		continue;
	    }

	    const float sampled = sampleBlendTrack (layer, row);
	    if (layer.additive) {
		value += (sampled - layer.animation->blendTracks[row].front ()) * layer.weight;
	    } else {
		value = glm::mix (value, sampled, std::clamp (layer.weight, 0.0f, 1.0f));
	    }
	}

	pose.blendWeights[row] = value;
    }

    return pose;
}

std::optional<glm::mat4> MdlAnimationEvaluator::attachmentTransform (
    const MdlAnimationData& animationData, const std::vector<glm::mat4>& worldBones, const std::string& name
) {
    const auto attachment = animationData.attachments.find (name);
    if (attachment == animationData.attachments.end () || attachment->second.bone >= worldBones.size ()) {
	return std::nullopt;
    }
    return worldBones[attachment->second.bone] * attachment->second.local;
}
