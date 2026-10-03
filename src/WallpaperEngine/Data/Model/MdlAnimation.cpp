#include "MdlAnimation.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
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
    // Native 1401f89a0 uses shortest-hemisphere nlerp between authored frames,
    // including the baked closing frame of a match-loop clip.
    const auto nextRotation = glm::dot (current.rotation, next.rotation) < 0.0f ? -next.rotation : next.rotation;
    return {
	.translation = glm::mix (current.translation, next.translation, blend),
	.rotation = glm::normalize (current.rotation * (1.0f - blend) + nextRotation * blend),
	.scale = glm::mix (current.scale, next.scale, blend),
    };
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

float wrapAngle (const float angle) {
    constexpr float pi = glm::pi<float> ();
    return angle >= 0.0f ? std::fmod (angle + pi, 2.0f * pi) - pi : std::fmod (angle - pi, 2.0f * pi) + pi;
}

/**
 * One spring bone step from native 1401fdf90, returning the local physics transform that
 * follows the animated bone. `animatedWorld` already carries the parent's physics.
 * ponytail: the "lt" torque limit is not applied; no installed spring rig enables it.
 */
glm::mat4 stepSpring (
    const MdlBoneSpring& spring, MdlPhysicsStep& physics, MdlPhysicsState::Bone& state, const glm::mat4& previous,
    const glm::mat4& animatedWorld
) {
    const glm::quat identity (1.0f, 0.0f, 0.0f, 0.0f);
    const float dt = physics.deltaTime;
    const float scale = (glm::length (glm::vec3 (physics.model[0])) + glm::length (glm::vec3 (physics.model[1]))
			 + glm::length (glm::vec3 (physics.model[2]))) / 3.0f;
    const glm::mat4 current = physics.model * animatedWorld;
    const glm::vec3 position (current[3]);
    const float determinant = glm::determinant (glm::mat3 (current));
    if (determinant == 0.0f || !std::isfinite (determinant)) {
	state = {};
	return glm::mat4 (1.0f);
    }
    const glm::mat3 toLocal = glm::inverse (glm::mat3 (current));

    // Where the tip was last frame, and where the carried physics puts it now, in bone space.
    const glm::vec3 lagging = toLocal * (glm::vec3 (previous * glm::vec4 (spring.tip, 1.0f)) - position);
    glm::vec3 tip = glm::mat3_cast (glm::quat (state.angles)) * spring.tip + toLocal * state.offset;
    if (spring.gravity) {
	const glm::vec3 axis = directionOr (tip, glm::vec3 (1.0f, 0.0f, 0.0f));
	const glm::vec3 pull = toLocal * spring.gravityDirection * (spring.mass * scale);
	tip -= pull - glm::dot (axis, pull) * axis;
	if (spring.translation) state.velocity += spring.gravityDirection * (spring.mass * dt * 1000.0f);
    }
    // Native caps the tip's travel at 900 units per second.
    const float drag = std::min (glm::length (tip - lagging), 900.0f * dt);
    const glm::quat follow = glm::rotation (
	directionOr (tip, glm::vec3 (1.0f, 0.0f, 0.0f)), directionOr (lagging, glm::vec3 (1.0f, 0.0f, 0.0f))
    );

    if (spring.rotation) {
	const float degree = glm::radians (1.0f);
	auto& velocity = state.angularVelocity;
	velocity *= glm::slerp (identity, follow, std::min (drag * spring.rotationInertia * degree, 1.0f));
	velocity *= glm::slerp (identity, glm::quat (-state.angles), std::min (spring.rotationStiffness * degree * dt, 1.0f));
	// The velocity is one 60 Hz step; integrate it on top of the carried orientation.
	const glm::quat step = glm::slerp (identity, velocity, std::min (dt * 60.0f, 1.0f));
	glm::vec3 angles;
	glm::extractEulerAngleZYX (
	    glm::mat4 (glm::mat3_cast (step) * glm::mat3_cast (glm::quat (state.angles))), angles.z, angles.y, angles.x
	);
	for (int axis = 0; axis < 3; axis++) angles[axis] = spring.rotationAxes[axis] ? wrapAngle (angles[axis]) : 0.0f;
	if (spring.limitAngles) {
	    const glm::vec3 limited = glm::clamp (angles, spring.minAngles, spring.maxAngles);
	    // A clamped rotation also stops the velocity pushing against the limit.
	    velocity = glm::conjugate (glm::quat (angles - limited)) * velocity;
	    angles = limited;
	}
	state.angles = angles;
	velocity = glm::slerp (velocity, identity, std::min (dt * spring.rotationFriction, 1.0f));
    }

    if (spring.translation) {
	auto& offset = state.offset;
	offset -= (position + offset - glm::vec3 (previous[3])) * spring.translationInertia;
	state.velocity -= dt * spring.translationStiffness * offset;
	offset += dt * state.velocity;
	for (int axis = 0; axis < 3; axis++) {
	    const glm::vec3 direction (current[axis]);
	    if (!spring.translationAxes[axis]) offset -= glm::dot (offset, direction) / glm::dot (direction, direction) * direction;
	}
	const float limit = scale * spring.maxDistance;
	if (limit > 0.0f && glm::length (offset) > limit) offset *= limit / glm::length (offset);
	state.velocity -= std::min (dt * spring.translationFriction, 1.0f) * state.velocity;
    }

    return glm::translate (glm::mat4 (1.0f), toLocal * state.offset) * glm::mat4_cast (glm::quat (state.angles));
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
    const MdlAnimationData& animationData, const std::vector<MdlActiveAnimation>& activeAnimations,
    MdlPhysicsStep* physics
) {
    MdlPose pose;
    pose.worldBones.resize (animationData.bones.size ());
    pose.skinBones.resize (animationData.bones.size ());
    std::vector<glm::mat4> locals;
    if (!animationData.ikGroups.empty ()) locals.resize (animationData.bones.size ());
    if (physics != nullptr && physics->state.bones.size () != animationData.bones.size ()) {
	physics->state = { .bones = std::vector<MdlPhysicsState::Bone> (animationData.bones.size ()) };
    }
    // Native skips the step until it has a previous frame to measure motion against.
    const bool simulate = physics != nullptr && !physics->state.previousWorld.empty ();

    for (size_t bone = 0; bone < animationData.bones.size (); bone++) {
	// Native 1401fdf90 initializes the shared blend reference from MDLS's
	// optional reference matrices, falling back to the skeleton bind pose.
	// A clip's first frame can already reposition atlas parts; subtracting it
	// instead makes an idle additive layer displace the dragon's eye (3233141951).
	const auto& skeletonBone = animationData.bones[bone];
	std::optional<MdlBoneFrame> reference;
	std::optional<MdlBoneFrame> local;
	const auto referencePose = [&] () -> const MdlBoneFrame& {
	    if (!reference) reference = matrixPose (skeletonBone.referenceLocal.value_or (skeletonBone.bindLocal));
	    return *reference;
	};

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

	    const auto sampled = samplePose (layer, frames);
	    if (!layer.additive && layer.weight >= 1.0f) {
		// A full replacement needs neither the reference decomposition nor a
		// second spherical interpolation whose result is already the sample.
		local = sampled;
		local->rotation = glm::normalize (local->rotation);
	    } else {
		if (!local) local = referencePose ();
		// Every partial/additive layer uses the same reference and separate T/R/S.
		composeLayer (*local, referencePose (), sampled, layer);
	    }
	}

	const auto parent = animationData.bones[bone].parent;
	const glm::mat4 parentWorld = parent >= 0 ? pose.worldBones[parent] : glm::mat4 (1.0f);
	glm::mat4 localMatrix = poseMatrix (local ? *local : referencePose ());
	if (simulate && skeletonBone.spring) {
	    localMatrix *= stepSpring (
		*skeletonBone.spring, *physics, physics->state.bones[bone], physics->state.previousWorld[bone],
		parentWorld * localMatrix
	    );
	}
	if (!locals.empty ()) locals[bone] = localMatrix;
	pose.worldBones[bone] = parentWorld * localMatrix;
	pose.skinBones[bone] = pose.worldBones[bone] * animationData.bones[bone].inverseBindWorld;
    }

    // The next step measures motion against these, taken before IK like native's 0x340 matrices.
    if (physics != nullptr) {
	physics->state.previousWorld.resize (pose.worldBones.size ());
	for (size_t bone = 0; bone < pose.worldBones.size (); bone++)
	    physics->state.previousWorld[bone] = physics->model * pose.worldBones[bone];
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

    // Native 1401fdf90 resets to the MDLS defaults and floor-samples the late
    // MDLA depth stream. An untouched result keeps the previous draw order.
    for (const auto& layer : activeAnimations) {
	if (!layer.animation || layer.weight <= 0.0f) continue;
	const auto& clip = *layer.animation;
	const auto frame = static_cast<size_t> (sampleFrame (layer));
	for (size_t bone = 0; bone < animationData.defaultDepths.size () && bone < clip.depthTracks.size (); ++bone) {
	    if (clip.depthTracks[bone].empty ()
		|| (bone < clip.boneFlags.size () && (clip.boneFlags[bone] & 3) == 1)) continue;
	    if (pose.depths.empty ())
		pose.depths.assign (animationData.defaultDepths.begin (), animationData.defaultDepths.end ());
	    const auto& samples = clip.depthTracks[bone];
	    const float sample = samples[std::min (frame, samples.size () - 1)];
	    float& value = pose.depths[bone];
	    const float weight = std::clamp (layer.weight, 0.0f, 1.0f);
	    if (layer.additive) {
		value = std::clamp (value + (sample - animationData.defaultDepths[bone]) * weight,
		    std::min (value, sample), std::max (value, sample));
	    } else {
		value = glm::mix (value, sample, weight);
	    }
	}
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

std::optional<std::string> MdlAnimationEvaluator::hitBox (
    const MdlAnimationData& animationData, const std::vector<glm::mat4>& worldBones,
    const glm::vec3& rayOrigin, const glm::vec3& rayDirection
) {
    for (auto entry = animationData.hitBoxOrder.rbegin (); entry != animationData.hitBoxOrder.rend (); ++entry) {
	const auto bone = *entry;
	if (bone >= animationData.hitBoxes.size () || bone >= worldBones.size ()) continue;
	const auto& box = animationData.hitBoxes[bone];
	const glm::mat4 transform = worldBones[bone] * box.local;
	const float determinant = glm::determinant (transform);
	if (!std::isfinite (determinant) || determinant == 0.0f) continue;
	const glm::mat4 inverse = glm::inverse (transform);
	const glm::vec3 origin = glm::vec3 (inverse * glm::vec4 (rayOrigin, 1.0f));
	const glm::vec3 direction = glm::vec3 (inverse * glm::vec4 (rayDirection, 0.0f));
	float near = -std::numeric_limits<float>::infinity ();
	float far = std::numeric_limits<float>::infinity ();
	bool intersects = true;
	for (int axis = 0; axis < 3; ++axis) {
	    if (!std::isfinite (origin[axis]) || !std::isfinite (direction[axis])) {
		intersects = false;
		break;
	    }
	    if (direction[axis] == 0.0f) {
		if (std::abs (origin[axis]) > box.halfExtents[axis]) intersects = false;
		continue;
	    }
	    const float first = (-box.halfExtents[axis] - origin[axis]) / direction[axis];
	    const float second = (box.halfExtents[axis] - origin[axis]) / direction[axis];
	    near = std::max (near, std::min (first, second));
	    far = std::min (far, std::max (first, second));
	}
	// Native selects the first intersecting box in authored order, not the
	// nearest triangle or bone. It tests the whole cursor line in this space.
	if (intersects && near <= far) {
	    const auto& name = animationData.bones[bone].name;
	    return name.empty () ? std::to_string (bone) : name;
	}
    }
    return std::nullopt;
}
