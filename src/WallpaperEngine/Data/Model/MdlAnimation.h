#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace WallpaperEngine::Data::Model {
struct MdlBone {
    std::string name = {};
    uint32_t type = 0;
    int32_t parent = -1;
    glm::mat4 bindLocal = glm::mat4 (1.0f);
    glm::mat4 inverseBindWorld = glm::mat4 (1.0f);
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

struct MdlAnimationClip {
    uint32_t id = 0;
    std::string name = {};
    std::string mode = {};
    float fps = 0.0f;
    uint32_t frameCount = 0;
    std::vector<uint32_t> boneFlags = {};
    /** boneFrames[bone][frame], commonly frameCount + 1 entries for loop interpolation. */
    std::vector<std::vector<MdlBoneFrame>> boneFrames = {};
};

struct MdlAnimationData {
    std::vector<MdlBone> bones = {};
    std::map<std::string, MdlAttachment> attachments = {};
    std::vector<MdlAnimationClip> animations = {};
};

struct MdlActiveAnimation {
    const MdlAnimationClip* animation = nullptr;
    float time = 0.0f;
    float weight = 1.0f;
    bool additive = false;
};

struct MdlPose {
    std::vector<glm::mat4> worldBones = {};
    std::vector<glm::mat4> skinBones = {};
};

class MdlAnimationEvaluator {
public:
    [[nodiscard]] static MdlPose
    evaluate (const MdlAnimationData& animationData, const std::vector<MdlActiveAnimation>& activeAnimations);
};
} // namespace WallpaperEngine::Data::Model
