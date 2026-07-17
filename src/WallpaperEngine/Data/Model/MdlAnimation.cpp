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

glm::mat4 blendMatrix (const glm::mat4& from, const glm::mat4& to, const float weight) {
    if (weight <= 0.0f) {
	return from;
    }
    if (weight >= 1.0f) {
	return to;
    }
    return poseMatrix (blendPose (matrixPose (from), matrixPose (to), weight));
}

MdlBoneFrame sampleBone (const MdlActiveAnimation& layer, const size_t bone) {
    if (layer.animation == nullptr || bone >= layer.animation->boneFrames.size ()) {
	return {};
    }

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

    const auto& frames = animation.boneFrames[bone];
    if (frames.empty ()) {
	return {};
    }

    const auto firstFrame = static_cast<size_t> (frame);
    const float blend = frame - static_cast<float> (firstFrame);
    const auto& current = frames[std::min (firstFrame, frames.size () - 1)];
    const auto& next = frames[std::min (firstFrame + 1, frames.size () - 1)];
    return blendPose (current, next, blend);
}
} // namespace

MdlPose MdlAnimationEvaluator::evaluate (
    const MdlAnimationData& animationData, const std::vector<MdlActiveAnimation>& activeAnimations
) {
    MdlPose pose;
    pose.worldBones.resize (animationData.bones.size ());
    pose.skinBones.resize (animationData.bones.size ());

    for (size_t bone = 0; bone < animationData.bones.size (); bone++) {
	glm::mat4 local = animationData.bones[bone].bindLocal;
	size_t firstComposedLayer = 0;

	if (!activeAnimations.empty ()) {
	    const auto& baseLayer = activeAnimations.front ();
	    if (baseLayer.animation != nullptr && bone < baseLayer.animation->boneFrames.size ()) {
		const auto& baseFrames = baseLayer.animation->boneFrames[bone];
		if (!baseFrames.empty ()) {
		    const glm::mat4 reference = poseMatrix (baseFrames.front ());
		    const glm::mat4 sampled = poseMatrix (sampleBone (baseLayer, bone));
		    local = blendMatrix (reference, sampled, baseLayer.weight);
		}
	    }
	    firstComposedLayer = 1;
	}

	for (size_t layerIndex = firstComposedLayer; layerIndex < activeAnimations.size (); layerIndex++) {
	    const auto& layer = activeAnimations[layerIndex];
	    if (layer.animation == nullptr || bone >= layer.animation->boneFrames.size ()) {
		continue;
	    }
	    const auto& frames = layer.animation->boneFrames[bone];
	    if (frames.empty () || layer.weight <= 0.0f) {
		continue;
	    }

	    const glm::mat4 sampled = poseMatrix (sampleBone (layer, bone));
	    if (layer.additive) {
		const glm::mat4 reference = poseMatrix (frames.front ());
		const glm::mat4 delta = glm::inverse (reference) * sampled;
		local *= blendMatrix (glm::mat4 (1.0f), delta, layer.weight);
	    } else {
		local = blendMatrix (local, sampled, layer.weight);
	    }
	}

	const auto parent = animationData.bones[bone].parent;
	pose.worldBones[bone] = parent >= 0 ? pose.worldBones[parent] * local : local;
	pose.skinBones[bone] = pose.worldBones[bone] * animationData.bones[bone].inverseBindWorld;
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
