#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include "PropertyAnimation.h"

namespace WallpaperEngine::Data::Model {
/** MDLS "spring physics" bone constraint (se), as read by native 140265c30. */
struct MdlBoneSpring {
    bool rotation = false; // r
    bool translation = false; // t
    float rotationStiffness = 0.0f; // rs
    float rotationFriction = 0.0f; // rf
    /** Native stores 1 - ri / 100. */
    float rotationInertia = 1.0f;
    float translationStiffness = 0.0f; // ts
    float translationFriction = 0.0f; // tf
    float translationInertia = 1.0f; // 1 - ti / 100
    bool gravity = false; // ge
    glm::vec3 gravityDirection = glm::vec3 (0.0f); // gd
    float mass = 0.0f; // m
    /** Bone-local point whose lag drives the rotation (tp). */
    glm::vec3 tip = glm::vec3 (0.0f);
    /** Translation offset limit in scene units before object scale (tm). */
    float maxDistance = 0.0f;
    bool limitTorque = false; // lt
    /** Per-axis quaternion rotation cap in degrees per 60 Hz velocity step (ltmax). */
    float maxTorque = 0.0f;
    bool limitAngles = false; // la
    glm::vec3 minAngles = glm::vec3 (0.0f);
    glm::vec3 maxAngles = glm::vec3 (0.0f);
    glm::bvec3 rotationAxes = glm::bvec3 (true); // rax/ray/raz
    glm::bvec3 translationAxes = glm::bvec3 (true); // tax/tay/taz
};

struct MdlBone {
    std::string name = {};
    uint32_t type = 0;
    int32_t parent = -1;
    glm::mat4 bindLocal = glm::mat4 (1.0f);
    glm::mat4 inverseBindWorld = glm::mat4 (1.0f);
    /** Optional MDLS reference pose used for layer blending, separate from skinning bind data. */
    std::optional<glm::mat4> referenceLocal;
    bool ikFollowEnd = false;
    bool ikAimToTarget = false;
    float ikAimDistance = 1.0f;
    bool ikAngleLimits = false;
    std::optional<MdlBoneSpring> spring;
};

struct MdlAttachment {
    uint16_t bone = 0;
    glm::mat4 local = glm::mat4 (1.0f);
};

struct MdlHitBox {
    glm::vec3 halfExtents = glm::vec3 (0.0f);
    glm::mat4 local = glm::mat4 (1.0f);
};

struct MdlBoneFrame {
    glm::vec3 translation = glm::vec3 (0.0f);
    glm::quat rotation = glm::quat (1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3 (1.0f);
};

/** An authored world-space bone control (0: end transform, 1: IK pole). */
struct MdlBoneControl {
    std::string name;
    uint32_t bone = 0;
    uint32_t type = 0;
    glm::mat4 bindWorld = glm::mat4 (1.0f);
    std::optional<glm::mat4> referenceWorld;
};

struct MdlIkChain {
    uint32_t flags = 0;
    float maxLength = 0.0f;
    float minLength = 0.0f;
    /** Ordered from the chain root to its controlled end bone. */
    std::vector<uint32_t> bones;
};

struct MdlIkNode {
    uint32_t root = 0;
    std::vector<MdlIkChain> branches;
};

struct MdlIkGroup {
    uint32_t root = 0;
    bool hasDependencies = false;
    std::vector<MdlIkNode> nodes;
};

struct MdlMorphTrack {
    uint16_t target = 0;
    std::vector<float> frames;
};

/** MDLA0005 whole-clip volume; older clips initialize both corners to zero. */
struct MdlAnimationBounds {
    glm::vec3 minimum = glm::vec3 (0.0f);
    glm::vec3 maximum = glm::vec3 (0.0f);
};

struct MdlAnimationClip {
    uint32_t id = 0;
    std::string name = {};
    std::string mode = {};
    float fps = 0.0f;
    uint32_t frameCount = 0;
    uint32_t flags = 0;
    std::vector<PropertyAnimation::Event> events;
    MdlAnimationBounds bounds;
    /** Bit zero disables the track, preserving the pose from earlier layers. */
    std::vector<uint32_t> boneFlags = {};
    /** boneFrames[bone][frame], commonly frameCount + 1 entries for loop interpolation. */
    std::vector<std::vector<MdlBoneFrame>> boneFrames = {};
    std::vector<uint32_t> controlFlags;
    /** MDLA0002+ stores one additional pose track per MDLS bone control. */
    std::vector<std::vector<MdlBoneFrame>> controlFrames;
    /**
     * blendTracks[row][frame], the per-frame scalars a clip drives alongside its bones.
     * They become g_BlendMap in the puppettexturechannels shader, fading a channelmap
     * overlay in and out; authored blinks live here, not in the bone tracks.
     */
    std::vector<std::vector<float>> blendTracks = {};
    /** MDLA0004+ sparse target weights, grouped by MDLV submesh in file order. */
    std::vector<std::vector<MdlMorphTrack>> morphTracks;
    /** MDLA0006 late per-bone depth stream, sampled without frame interpolation. */
    std::vector<std::vector<float>> depthTracks;
};

struct MdlAnimationData {
    std::vector<MdlBone> bones = {};
    std::vector<MdlHitBox> hitBoxes;
    /** Native tests the serialized bone indices from back to front. */
    std::vector<uint32_t> hitBoxOrder;
    /** Signed MDLS0004 depth defaults, separate from bone transforms. */
    std::vector<int32_t> defaultDepths;
    std::vector<MdlBoneControl> controls;
    std::vector<float> boneLengths;
    std::vector<std::map<uint32_t, glm::vec3>> boneDirections;
    std::vector<MdlIkGroup> ikGroups;
    std::map<std::string, MdlAttachment> attachments = {};
    std::vector<MdlAnimationClip> animations = {};
};

struct MdlActiveAnimation {
    const MdlAnimationClip* animation = nullptr;
    float time = 0.0f;
    float weight = 1.0f;
    bool additive = false;
    /** An explicit playhead from a script-controlled timeline, already wrapped/clamped. */
    std::optional<float> frame;
    /** Disabled layers differ from enabled layers whose authored weight is zero. */
    bool enabled = true;
};

struct MdlPose {
    std::vector<glm::mat4> worldBones = {};
    std::vector<glm::mat4> skinBones = {};
    /** Interpolated blend track values of the active clips, indexed by track row. */
    std::vector<float> blendWeights = {};
    /** Native puppet runtime evaluates the first submesh, with a 64-target mask. */
    std::vector<float> puppetMorphWeights;
    uint64_t puppetMorphMask = 0;
    /** Empty when no active depth track changed the native ordering state. */
    std::vector<float> depths;
};

/** Spring state one model instance carries between frames. */
struct MdlPhysicsState {
    struct Bone {
	/** Per-step rotation; inertia and stiffness add to it, friction decays it. */
	glm::quat angularVelocity = glm::quat (1.0f, 0.0f, 0.0f, 0.0f);
	/** ZYX Euler rotation applied on top of the animated bone. */
	glm::vec3 angles = glm::vec3 (0.0f);
	/** Scene-space translation offset and its velocity. */
	glm::vec3 offset = glm::vec3 (0.0f);
	glm::vec3 velocity = glm::vec3 (0.0f);
    };
    std::vector<Bone> bones;
    /** Last frame's scene-space bone matrices after physics; empty before the first step. */
    std::vector<glm::mat4> previousWorld;
};

struct MdlPhysicsStep {
    MdlPhysicsState& state;
    /** Model space to y-up scene space, including layer motion such as parallax. */
    glm::mat4 model = glm::mat4 (1.0f);
    float deltaTime = 0.0f;
};

class MdlAnimationEvaluator {
public:
    [[nodiscard]] static MdlPose evaluate (
	const MdlAnimationData& animationData, const std::vector<MdlActiveAnimation>& activeAnimations,
	MdlPhysicsStep* physics = nullptr
    );
    [[nodiscard]] static std::optional<glm::mat4> attachmentTransform (
	const MdlAnimationData& animationData, const std::vector<glm::mat4>& worldBones, const std::string& name
    );
    [[nodiscard]] static std::optional<std::string> hitBox (
	const MdlAnimationData& animationData, const std::vector<glm::mat4>& worldBones,
	const glm::vec3& rayOrigin, const glm::vec3& rayDirection
    );
};
} // namespace WallpaperEngine::Data::Model
