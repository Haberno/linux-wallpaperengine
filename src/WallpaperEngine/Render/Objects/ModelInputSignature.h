#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace WallpaperEngine::Render::Objects {
/** Native emits selected declarations before compilation; unused inputs count. */
inline std::optional<uint32_t> modelInputSemantics (std::string_view selectedSource) {
    struct Input { const char* name; const char* type; uint32_t semantic; };
    static constexpr std::array<Input, 10> inputs {{
        { "a_Position", "vec3", 1 }, { "a_PositionVec4", "vec4", 1 },
        { "a_Normal", "vec3", 2 }, { "a_Tangent4", "vec4", 4 },
        { "a_TexCoord", "vec2", 8 }, { "a_TexCoordVec3", "vec3", 8 }, { "a_TexCoordVec4", "vec4", 8 },
        { "a_Color", "vec4", 0x8000 },
        { "a_BlendIndices", "uvec4", 0x800000 }, { "a_BlendWeights", "vec4", 0x1000000 }
    }};
    static const std::regex declaration (R"(^\s*(?:attribute|in)\s+(\w+)\s+(\w+)\s*;)");
    uint32_t semantics = 0;
    std::istringstream lines { std::string (selectedSource) };
    std::string line;
    while (std::getline (lines, line)) {
        std::smatch match;
        if (line.find ("gl_VertexID") != std::string::npos || line.find ("gl_InstanceID") != std::string::npos) return std::nullopt;
        if (std::regex_search (line, match, declaration)) {
            const auto found = std::find_if (inputs.begin (), inputs.end (), [&] (const Input& input) {
                return match[2] == input.name && match[1] == input.type;
            });
            // Simultaneously selected aliases would duplicate the native semantic.
            if (found == inputs.end ()) return std::nullopt;
            if ((semantics & found->semantic) != 0) throw std::runtime_error ("Duplicated model input semantic");
            semantics |= found->semantic;
        } else {
            const auto first = line.find_first_not_of (" \t");
            if (first != std::string::npos && (line.compare (first, 9, "attribute") == 0
                || line.compare (first, 3, "in ") == 0 || line.compare (first, 6, "layout") == 0
                || line.compare (first, 5, "flat ") == 0 || line.compare (first, 7, "smooth ") == 0
                || line.compare (first, 14, "noperspective ") == 0 || line.compare (first, 9, "centroid ") == 0)) return std::nullopt;
        }
    }
    return semantics;
}
} // namespace WallpaperEngine::Render::Objects
