#pragma once

#include <GL/glew.h>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace WallpaperEngine::Render::Shaders {
/**
 * Reuses linked binaries for private programs, with opt-in live program sharing
 * for callers that guarantee compatible uniform uploads before each draw.
 * Owned by a RenderContext and used on its render thread with a current GL context.
 * The cache retains only CPU data; callers own private and shared GL programs.
 */
class ShaderProgramCache {
public:
    struct SharedProgram {
	explicit SharedProgram (GLuint program) : id (program) { }
	~SharedProgram ();
	SharedProgram (const SharedProgram&) = delete;
	SharedProgram& operator= (const SharedProgram&) = delete;
	const GLuint id;
    };
    struct SharingGroup {
	// Weak references let the last material release the GL object immediately.
	std::unordered_map<std::string, std::weak_ptr<const SharedProgram>> programs;
    };
    explicit ShaderProgramCache (size_t budgetBytes = 64ULL * 1024 * 1024) : m_budgetBytes (budgetBytes) { }
    [[nodiscard]] GLuint createProgram (
	const std::string& vertex, const std::string& fragment, std::shared_ptr<SharingGroup>* group = nullptr
    );
    /**
     * Opt-in sharing for passes that upload the same uniform locations/types/counts
     * before EVERY draw. Layout must describe all writes, including array extents.
     * The private program stays untouched and owned by the caller. Returns null
     * when cloning is unsupported/rejected or the linked interface differs.
     */
    [[nodiscard]] static std::shared_ptr<const SharedProgram>
    shareProgram (GLuint privateProgram, const std::shared_ptr<SharingGroup>& group, const std::string& layout);

private:
    struct Entry {
	GLenum format;
	std::vector<char> binary;
	size_t lastUsed;
	std::shared_ptr<SharingGroup> group = std::make_shared<SharingGroup> ();
    };
    size_t m_budgetBytes;
    size_t m_bytes = 0;
    size_t m_useCounter = 0;
    std::unordered_map<std::string, Entry> m_entries;
};
} // namespace WallpaperEngine::Render::Shaders
