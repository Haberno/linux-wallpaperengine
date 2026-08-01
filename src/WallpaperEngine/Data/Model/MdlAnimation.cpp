#include "MdlAnimation.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

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

MdlBoneFrame sampleBone (const MdlActiveAnimation& layer, const size_t bone) {
    if (layer.animation == nullptr || bone >= layer.animation->boneFrames.size ()) {
	return {};
    }

    const auto& frames = layer.animation->boneFrames[bone];
    if (frames.empty ()) {
	return {};
    }

    const float frame = sampleFrame (layer);
    const auto firstFrame = static_cast<size_t> (frame);
    const float blend = frame - static_cast<float> (firstFrame);
    const auto& current = frames[std::min (firstFrame, frames.size () - 1)];
    const auto& next = frames[std::min (firstFrame + 1, frames.size () - 1)];
    return blendPose (current, next, blend);
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

    for (size_t bone = 0; bone < animationData.bones.size (); bone++) {
	const MdlBoneFrame bind = matrixPose (animationData.bones[bone].bindLocal);
	MdlBoneFrame local = bind;

	for (size_t layerIndex = 0; layerIndex < activeAnimations.size (); layerIndex++) {
	    const auto& layer = activeAnimations[layerIndex];
	    if (layer.animation == nullptr || bone >= layer.animation->boneFrames.size ()) {
		continue;
	    }
	    const auto& frames = layer.animation->boneFrames[bone];
	    if (frames.empty () || layer.weight <= 0.0f) {
		continue;
	    }

	    const MdlBoneFrame sampled = sampleBone (layer, bone);
	    const float weight = std::clamp (layer.weight, 0.0f, 1.0f);
	    if (layer.additive) {
		// The native evaluator keeps translation, rotation, and scale as separate
		// arrays. Additive layers subtract the skeleton bind pose component-wise,
		// then accumulate the weighted delta into the current pose. Multiplying
		// local matrices instead makes a preceding layer's scale/rotation distort
		// later translation deltas (notably Gojo, workshop 3100265648).
		local.translation += (sampled.translation - bind.translation) * weight;
		const glm::quat delta = glm::normalize (glm::inverse (bind.rotation) * sampled.rotation);
		local.rotation = glm::normalize (
		    local.rotation * glm::slerp (glm::quat (1.0f, 0.0f, 0.0f, 0.0f), delta, weight)
		);
		local.scale += (sampled.scale - bind.scale) * weight;
	    } else {
		local = blendPose (local, sampled, weight);
	    }
	}

	const auto parent = animationData.bones[bone].parent;
	const glm::mat4 localMatrix = poseMatrix (local);
	pose.worldBones[bone] = parent >= 0 ? pose.worldBones[parent] * localMatrix : localMatrix;
	pose.skinBones[bone] = pose.worldBones[bone] * animationData.bones[bone].inverseBindWorld;
    }

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
