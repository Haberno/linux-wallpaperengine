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
struct MdlBone {
    std::string name = {};
    uint32_t type = 0;
    int32_t parent = -1;
    glm::mat4 bindLocal = glm::mat4 (1.0f);
    glm::mat4 inverseBindWorld = glm::mat4 (1.0f);
    bool ikFollowEnd = false;
    bool ikAimToTarget = false;
    float ikAimDistance = 1.0f;
    bool ikAngleLimits = false;
};

struct MdlAttachment {
    uint16_t bone = 0;
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

struct MdlAnimationClip {
    uint32_t id = 0;
    std::string name = {};
    std::string mode = {};
    float fps = 0.0f;
    uint32_t frameCount = 0;
    uint32_t flags = 0;
    std::vector<PropertyAnimation::Event> events;
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
};

struct MdlAnimationData {
    std::vector<MdlBone> bones = {};
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
};

struct MdlPose {
    std::vector<glm::mat4> worldBones = {};
    std::vector<glm::mat4> skinBones = {};
    /** Interpolated blend track values of the active clips, indexed by track row. */
    std::vector<float> blendWeights = {};
};

class MdlAnimationEvaluator {
public:
    [[nodiscard]] static MdlPose
    evaluate (const MdlAnimationData& animationData, const std::vector<MdlActiveAnimation>& activeAnimations);
    [[nodiscard]] static std::optional<glm::mat4> attachmentTransform (
	const MdlAnimationData& animationData, const std::vector<glm::mat4>& worldBones, const std::string& name
    );
};
} // namespace WallpaperEngine::Data::Model
