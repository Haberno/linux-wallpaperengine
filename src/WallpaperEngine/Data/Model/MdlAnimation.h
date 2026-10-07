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

/** IK rope settings remain active without spring mode (native 140265c30). */
struct MdlBoneRope {
    bool gravity = false;
    float mass = 0.0f;
    float friction = 0.0f; // clamp((tf / 100)^2, 0, 1)
    bool stretch = false; // ikse
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
    float ikAimDistance = 50.0f;
    bool ikAngleLimits = false;
    float ikMinAngle = 0.0f; // ikrminl, radians
    float ikMaxAngle = 0.0f; // ikrmaxl, radians
    std::optional<MdlBoneSpring> spring;
    std::optional<MdlBoneRope> rope;
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
    std::vector<uint32_t> dependencyFlags;
    /** MDLA0002+ implicit scalar tracks, indexed by the unfiltered MDLS dependency count. */
    std::vector<std::vector<float>> dependencyTracks;
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
    uint32_t dependencyCount = 0;
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

/** Native general-model state is independent for every submesh. */
struct MdlModelMorphState {
    std::vector<float> weights;
    uint64_t mask = 0;
};

struct MdlPose {
    std::vector<glm::mat4> worldBones = {};
    std::vector<glm::mat4> skinBones = {};
    /** Model-space +340 snapshots; rope owners publish before solving their own group. */
    std::vector<glm::mat4> publishedWorldBones;
    /** Interpolated blend track values of the active clips, indexed by track row. */
    std::vector<float> blendWeights = {};
    /** Native puppet runtime evaluates the first submesh, with a 64-target mask. */
    std::vector<float> puppetMorphWeights;
    uint64_t puppetMorphMask = 0;
    /** Empty when no active depth track changed the native ordering state. */
    std::vector<float> depths;
};

/** Physics state one model instance carries between frames. */
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
    struct RopePoint {
	glm::vec3 current = glm::vec3 (0.0f);
	glm::vec3 previous = glm::vec3 (0.0f);
	bool initialized = false;
    };
    std::vector<RopePoint> ropePoints;
    /** Native working poses retained when solver-owned bones skip ordinary traversal. */
    std::vector<glm::mat4> ropeWorldBones;
    std::vector<Bone> bones;
    /** Last frame's scene-space bone matrices after physics; empty before the first step. */
    std::vector<glm::mat4> previousWorld;
};

struct MdlRopeStep {
    glm::vec3 gravity = glm::vec3 (0.0f);
    float deltaTime = 0.0f;
    glm::vec3 windDirection = glm::vec3 (0.0f);
    float windStrength = 0.0f;
    float windTime = 0.0f;
    float windWidth = 1.0f;
    /** Offset from the renderer's centered scene coordinates to authored world X. */
    float windOriginX = 0.0f;
};

struct MdlPhysicsStep {
    MdlPhysicsState& state;
    /** Model space to y-up scene space, including layer motion such as parallax. */
    glm::mat4 model = glm::mat4 (1.0f);
    float deltaTime = 0.0f;
    /** Only supplied for a supported puppet scene; setup skinning does not advance ropes. */
    std::optional<MdlRopeStep> rope;
};

class MdlAnimationEvaluator {
public:
    [[nodiscard]] static std::vector<MdlModelMorphState> evaluateModelMorphs (
        const std::vector<MdlActiveAnimation>& activeAnimations
    );
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
