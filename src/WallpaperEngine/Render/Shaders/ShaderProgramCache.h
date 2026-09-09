#pragma once

#include <GL/glew.h>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace WallpaperEngine::Render::Shaders {
/**
 * Reuses linked executables without sharing mutable program uniforms between passes.
 * Owned by a RenderContext and used on its render thread with a current GL context.
 * Only CPU binary data is retained; returned GL programs belong to the caller.
 */
class ShaderProgramCache {
public:
    explicit ShaderProgramCache (size_t budgetBytes = 64ULL * 1024 * 1024) : m_budgetBytes (budgetBytes) { }
    [[nodiscard]] GLuint createProgram (const std::string& vertex, const std::string& fragment);

private:
    struct Entry {
	GLenum format;
	std::vector<char> binary;
	size_t lastUsed;
    };
    size_t m_budgetBytes;
    size_t m_bytes = 0;
    size_t m_useCounter = 0;
    std::unordered_map<std::string, Entry> m_entries;
};
} // namespace WallpaperEngine::Render::Shaders
