#pragma once

// ponytail: temporary switch-timing instrumentation, remove after measuring
// Global accumulators that split wallpaper glbuild time into coarse buckets so
// the switch freeze can be attributed to CPU work (movable to a plain worker
// thread) or GL work (needs a shared context or time-slicing).

#include <atomic>
#include <chrono>
#include <cstdint>

namespace WallpaperEngine::BuildTiming {
// texture file read + LZ4 decompression (CPU, Data::Parsers::TextureParser)
inline std::atomic<uint64_t> texLoadUs {0};
// stb_image decode of FIF-packed textures (CPU, Render::CTexture)
inline std::atomic<uint64_t> texDecodeUs {0};
// glTexImage2D / glCompressedTexImage2D uploads (GL, Render::CTexture)
inline std::atomic<uint64_t> texGlUs {0};
// shader source load + preprocessing to GLSL (CPU, CPass)
inline std::atomic<uint64_t> shPrepUs {0};
// shprep sub-buckets (CPU, Render::Shaders::ShaderUnit):
// #include fetch + splice from asset containers
inline std::atomic<uint64_t> shIncludeUs {0};
// #if/#endif regex scan when placing includes before main()
inline std::atomic<uint64_t> shIfdefUs {0};
// per-line uniform/[COMBO] discovery + JSON parsing
inline std::atomic<uint64_t> shVarsUs {0};
// regex varying/texcoord compatibility passes in compile()
inline std::atomic<uint64_t> shCompatUs {0};
// glCompileShader / glLinkProgram (GL driver, CPass + CWallpaper)
inline std::atomic<uint64_t> shGlUs {0};
// framebuffer + render target setup (GL, CFBO)
inline std::atomic<uint64_t> fboUs {0};

inline void add (std::atomic<uint64_t>& counter, const std::chrono::steady_clock::time_point& start) {
    counter += std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now () - start)
		   .count ();
}

inline void reset () {
    texLoadUs = 0;
    texDecodeUs = 0;
    texGlUs = 0;
    shPrepUs = 0;
    shIncludeUs = 0;
    shIfdefUs = 0;
    shVarsUs = 0;
    shCompatUs = 0;
    shGlUs = 0;
    fboUs = 0;
}

class Scope {
  public:
    explicit Scope (std::atomic<uint64_t>& counter) :
	m_counter (counter), m_start (std::chrono::steady_clock::now ()) {}
    ~Scope () { add (m_counter, m_start); }

    Scope (const Scope&) = delete;
    Scope& operator= (const Scope&) = delete;

  private:
    std::atomic<uint64_t>& m_counter;
    std::chrono::steady_clock::time_point m_start;
};
} // namespace WallpaperEngine::BuildTiming
