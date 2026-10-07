#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace WallpaperEngine::Render::Objects {
/** Native conversion lifts raw attributes into VS_INPUT before D3D preprocessing.
 * Wider aliases, generated inputs and unknown declarations remain gated. */
inline std::optional<uint32_t> modelInputSemantics (
    std::string_view source, const std::function<std::string (const std::string&)>& includeSource
) {
    struct Input { const char* name; const char* type; uint32_t semantic; };
    static constexpr std::array<Input, 7> inputs {{
        { "a_Position", "vec3", 1 }, { "a_Normal", "vec3", 2 }, { "a_Tangent4", "vec4", 4 },
        { "a_TexCoord", "vec2", 8 }, { "a_Color", "vec4", 0x8000 },
        { "a_BlendIndices", "uvec4", 0x800000 }, { "a_BlendWeights", "vec4", 0x1000000 }
    }};
    static const std::regex include (R"inc(^#include[^"]*"([^"]+)")inc");
    static const std::regex declaration (R"(^attribute\s+(\w+)\s+(\w+)\s*;)");
    uint32_t semantics = 0;
    std::set<std::string> includes;
    std::set<std::string> attributes;
    std::function<bool (std::string_view)> collect = [&] (std::string_view content) {
        std::istringstream lines { std::string (content) };
        std::string line;
        while (std::getline (lines, line)) {
            std::smatch match;
            if (line.starts_with ("#include")) {
                if (!std::regex_search (line, match, include)) return false;
                const std::string name = match[1];
                if (includes.insert (name).second && !collect (includeSource (name))) return false;
            } else if (line.starts_with ("attribute")) {
                if (!std::regex_search (line, match, declaration)) return false;
                const auto found = std::find_if (inputs.begin (), inputs.end (), [&] (const Input& input) {
                    return match[2] == input.name && match[1] == input.type;
                });
                if (found == inputs.end () || !attributes.insert (match[2]).second) return false;
                semantics |= found->semantic;
            } else {
                const auto first = line.find_first_not_of (" \t");
                if (line.find ("gl_VertexID") != std::string::npos || line.find ("gl_InstanceID") != std::string::npos) return false;
                if (first != std::string::npos && (line.compare (first, 9, "attribute") == 0
                    || line.compare (first, 3, "in ") == 0 || line.compare (first, 8, "#require") == 0
                    || line.compare (first, 8, "#version") == 0)) return false;
            }
        }
        return true;
    };
    return collect (source) ? std::optional<uint32_t> (semantics) : std::nullopt;
}
} // namespace WallpaperEngine::Render::Objects
