#pragma once

#include <GL/glew.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace WallpaperEngine::Render::Shaders {
/**
 * Reuses linked binaries for private programs, with opt-in live program sharing
 * for callers that guarantee compatible uniform values before each draw.
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
	/** Call in layout order before uploading; all users of this program share the snapshot. */
	bool updateUniform (size_t offset, const void* value, size_t bytes) const {
	    const bool initialized = offset + bytes <= uniformValues.size ();
	    if (!initialized) uniformValues.resize (offset + bytes);
	    auto* previous = uniformValues.data () + offset;
	    if (initialized && std::memcmp (previous, value, bytes) == 0) return false;
	    std::memcpy (previous, value, bytes);
	    return true;
	}

    private:
	mutable std::vector<std::byte> uniformValues;
    };
    struct SharingGroup {
	// Weak references let the last material release the GL object immediately.
	std::unordered_map<std::string, std::weak_ptr<const SharedProgram>> programs;
	// Location queries only: nobody draws with it, so its uniforms are never written.
	std::weak_ptr<const SharedProgram> interface;
    };
    explicit ShaderProgramCache (size_t budgetBytes = 64ULL * 1024 * 1024) : m_budgetBytes (budgetBytes) { }
    [[nodiscard]] GLuint createProgram (
	const std::string& vertex, const std::string& fragment, std::shared_ptr<SharingGroup>* group = nullptr
    );
    /**
     * Opt-in sharing for passes that supply the same uniform locations/types/counts
     * before EVERY draw. Layout must describe all writes, including array extents.
     * The private program stays untouched and owned by the caller. Returns null
     * when cloning is unsupported/rejected or the linked interface differs.
     */
    [[nodiscard]] static std::shared_ptr<const SharedProgram>
    shareProgram (GLuint privateProgram, const std::shared_ptr<SharingGroup>& group, const std::string& layout);
    /**
     * For callers that opt into sharing: one program per source pair for uniform and
     * attribute location queries, so later users skip loading their own copy. Never
     * draw with it or write its uniforms while `group` is set; draw through
     * shareProgram or a cloneProgram copy. A null group means the caller owns it alone.
     */
    [[nodiscard]] std::shared_ptr<const SharedProgram>
    interfaceProgram (const std::string& vertex, const std::string& fragment, std::shared_ptr<SharingGroup>& group);
    /** A caller-owned copy with an identical interface, or GL_NONE when the driver cannot copy it. */
    [[nodiscard]] static GLuint cloneProgram (GLuint program);
    /** Compile a private source fallback with the same interface, or throw if cached locations cannot be retained. */
    [[nodiscard]] static GLuint compileCompatibleProgram (
	GLuint program, const std::string& vertex, const std::string& fragment
    );

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
