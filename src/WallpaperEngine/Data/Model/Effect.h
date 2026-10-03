#pragma once

#include <glm/vec4.hpp>
#include <cstdint>
#include <optional>
#include <string>

#include "Types.h"

namespace WallpaperEngine::Data::Model {
enum PassCommandType { Command_Copy = 0, Command_Swap = 1 };

struct FBO {
    std::string name;
    std::string format;
    float scale;
    bool unique;
    /** Named effect targets clamp by default; uvs=repeat enables wrapping. */
    bool repeat = false;
    /** Native maximum edge length; 0xffff leaves the source extents unchanged. */
    uint16_t fit = 0xffff;
    /** Native explicit extents; values above 4096 inherit the source dimension. */
    uint16_t width = 0xffff;
    uint16_t height = 0xffff;
    /** Color used by this target's clear action. */
    glm::vec4 clear = glm::vec4 (0.0f);
    /** Native initial clearing requires a complete or empty clear-color string. */
    bool clearOnCreate = false;
};

struct EffectPass {
    /** Disabled passes retain their original instance-override index. */
    bool enabled = true;
    /** The material to use for this effect's pass */
    std::optional<MaterialUniquePtr> material;
    /** Texture bindings for this effect's pass */
    TextureMap binds;
    /** The command this material executes (if specified) */
    std::optional<PassCommandType> command;
    /** The source this material renders from (if specified) */
    std::optional<std::string> source;
    /** The target this material renders to (if specified) */
    std::optional<std::string> target;
};

struct Effect {
    /** Effect's name for the UI */
    std::string name;
    /** Effect's description for the UI */
    std::string description;
    /** Effect's group for the UI */
    std::string group;
    /** Effect's preview project */
    std::string preview;
    /** Effect's dependencies */
    std::vector<std::string> dependencies;
    /** The different passes for this effect */
    std::vector<EffectPassUniquePtr> passes;
    /** The fbos declared by this effect */
    std::vector<FBOUniquePtr> fbos;
    /** Clear functions retain their resolved, effect-local authored target names. */
    std::map<std::string, std::vector<std::string>> functions;
};
} // namespace WallpaperEngine::Data::Model
