#pragma once

#include <memory>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace WallpaperEngine::Render::Shaders {
class GLSLContext {
public:
    /**
     * Types of shaders
     */
    enum UnitType { UnitType_Vertex = 0, UnitType_Fragment = 1 };

    GLSLContext ();
    ~GLSLContext ();

    [[nodiscard]] std::pair<std::string, std::string> toGlsl (const std::string& vertex, const std::string& fragment);

    [[nodiscard]] static GLSLContext& get ();

private:
    static std::unique_ptr<GLSLContext> sInstance;
    /**
     * toGlsl is a pure function of both source strings; wallpapers share the stock
     * assets/shaders sources and switch-backs reuse identical units, so the
     * glslang -> SPIR-V -> SPIRV-Cross round trip is memoized here. Guarded by a
     * mutex so a future worker-thread caller stays safe.
     */
    std::mutex m_cacheMutex;
    std::unordered_map<std::string, std::pair<std::string, std::string>> m_cache;
};
} // namespace WallpaperEngine::Render::Shaders