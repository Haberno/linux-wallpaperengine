#pragma once

#include <string>
#include <vector>

#include <GL/glew.h>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

// Forward-declare FreeType types to avoid leaking the header into users.
struct FT_LibraryRec_;
struct FT_FaceRec_;
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render {
class CFBO;
class FBOProvider;
}

namespace WallpaperEngine::Render::Objects::Effects {
class CPass;
}

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Data::Model;

/** Decodes the UTF-8 codepoint starting at @p offset and advances it past the sequence.
 *  Malformed bytes decode as U+FFFD one byte at a time. No overlong/range validation —
 *  the result only feeds glyph lookup, where a bogus codepoint just misses the charmap. */
uint32_t nextUtf8Codepoint (const std::string& text, size_t& offset);

/**
 * Phase 1 text renderer.
 *
 * Renders static text objects as a single FreeType-rasterized RGBA texture
 * drawn on a textured quad with its own minimal GLSL shader. Does NOT go
 * through CRenderable / materials / passes — Phase 1 does not need effects.
 *
 * Phase 2 (scripted/dynamic text, alignment from properties, effect passes)
 * is intentionally not implemented here. When the scene provides a dynamic
 * `text: { script: "..." }` object this class captures the script source in
 * the data model but renders an empty string — the Wallpaper Engine JS
 * runtime required to evaluate it is out of scope for Phase 1.
 */
class CText final : virtual public CObject, public Scripting::ScriptableObject {
public:
    CText (Wallpapers::CScene& scene, const Text& text);
    ~CText () override;

    void setup () override;
    void render () override;

private:
    // Rebuilds the glyph texture (and matching quad VBO) from the given string.
    // Reuses existing GL handles if already allocated, so this is safe to call
    // every time the rendered text changes.
    void rebuildTextureFrom (const std::string& text);
    void buildShader ();
    void uploadQuadVertices ();

    // setup() helpers (kept small to keep the setup flow linear).
    bool initFreeType ();
    bool loadEmbeddedFont ();
    bool loadSystemFont ();
    unsigned int computeEffectivePixelSize () const;
    void initScriptLayer ();

    // text-effect chain (built once at setup; FBOs sized by computeEffectSurface)
    void setupEffectChain ();
    void destroyEffectChain ();
    void renderEffectChain (const glm::mat4& mvp, float brightness, float alpha);
    /** chain surface: covers the authored box and the current ink quad plus blur headroom */
    [[nodiscard]] glm::vec2 computeEffectSurface () const;

    const Text& m_text;
    std::string m_lastRenderedText;
    unsigned int m_lastPixelSize = 0;
    Scripting::ScriptLayerHandle m_layerHandle = Scripting::kInvalidLayerHandle;

    FT_Library m_ftLibrary = nullptr;
    FT_Face m_ftFace = nullptr;
    std::vector<uint8_t> m_fontData;

    GLuint m_texture = 0;
    GLuint m_program = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_uMVP = -1;
    GLint m_uColor = -1;
    GLint m_uTexture = -1;

    glm::ivec2 m_textureSize = { 0, 0 };
    glm::vec2 m_quadSize = { 0.0f, 0.0f };
    /** ink bbox center relative to the authored box center, local +y-down space */
    glm::vec2 m_quadOffset = { 0.0f, 0.0f };

    // ---- text-effect chain state (only used when the object authors effects) ----
    /** satisfies the CRenderable contract for CPass (uniform sources, FBO resolution) */
    std::unique_ptr<class CTextEffectHost> m_effectHost;
    Data::Model::MaterialUniquePtr m_effectMaterial;
    std::vector<Effects::CPass*> m_effectPasses;
    std::vector<std::shared_ptr<const FBOProvider>> m_effectProviders;
    /** destinations written by the chain, cleared each frame before the passes run */
    std::vector<std::shared_ptr<const CFBO>> m_effectClears;
    std::shared_ptr<CFBO> m_fboA;
    std::shared_ptr<CFBO> m_fboB;
    std::shared_ptr<const CFBO> m_effectResult;
    GLuint m_ndcPosition = 0;
    GLuint m_passTexCoord = 0;
    GLuint m_compositeVao = 0;
    GLuint m_compositeVbo = 0;
    GLuint m_compositeProgram = 0;
    GLint m_cuMVP = -1;
    GLint m_cuTexture = -1;
    glm::mat4 m_baseMVP = glm::mat4 (1.0f);
    glm::vec2 m_effectSurface = { 0.0f, 0.0f };
    bool m_effectsEnabled = false;

    bool m_valid = false;
};
} // namespace WallpaperEngine::Render::Objects
