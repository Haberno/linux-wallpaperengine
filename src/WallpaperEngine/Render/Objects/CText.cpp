#include "CText.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "WallpaperEngine/Data/Model/DynamicValue.h"
#include "WallpaperEngine/Data/Model/Effect.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Parsers/MaterialParser.h"
#include "WallpaperEngine/Render/CFBO.h"
#include "WallpaperEngine/Render/FBOProvider.h"
#include "WallpaperEngine/Render/Objects/CRenderable.h"
#include "WallpaperEngine/Render/Objects/Effects/CPass.h"
#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Data/Model/UserSetting.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Camera.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

using namespace WallpaperEngine::Render::Objects;

namespace {
// TODO: Phase 2 – load font from wallpaper's materials/fonts/ using AssetLocator
// Phase 1 uses a system font instead of the font shipped by the wallpaper.
// Wallpaper Engine bundles .ttf files in `materials/fonts/`; wiring those in
// is deferred to Phase 2 along with dynamic/scripted text.
const std::vector<std::string> kFontCandidates = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
};

const char* kVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* kFragmentShader = R"glsl(
#version 330 core
in vec2 vUV;
uniform sampler2D uTexture;
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    float coverage = texture(uTexture, vUV).r;
    FragColor = vec4(uColor.rgb, uColor.a * coverage);
}
)glsl";

// composite shader for the effect path: the chain result is already colored RGBA;
// uColor modulates it (brightness on rgb, object alpha on a)
const char* kFragmentShaderRGBA = R"glsl(
#version 330 core
in vec2 vUV;
uniform sampler2D uTexture;
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    FragColor = texture(uTexture, vUV) * uColor;
}
)glsl";

GLuint compileShader (GLenum type, const char* source) {
    GLuint shader = glCreateShader (type);
    glShaderSource (shader, 1, &source, nullptr);
    glCompileShader (shader);

    GLint status = GL_FALSE;
    glGetShaderiv (shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
	char log[1024];
	glGetShaderInfoLog (shader, sizeof (log), nullptr, log);
	sLog.error ("CText shader compile failed: ", log);
	glDeleteShader (shader);
	return 0;
    }
    return shader;
}
} // namespace

uint32_t WallpaperEngine::Render::Objects::nextUtf8Codepoint (const std::string& text, size_t& offset) {
    const auto lead = static_cast<uint8_t> (text[offset]);
    int continuations;
    uint32_t code;

    if (lead < 0x80) {
	continuations = 0;
	code = lead;
    } else if ((lead & 0xE0) == 0xC0) {
	continuations = 1;
	code = lead & 0x1Fu;
    } else if ((lead & 0xF0) == 0xE0) {
	continuations = 2;
	code = lead & 0x0Fu;
    } else if ((lead & 0xF8) == 0xF0) {
	continuations = 3;
	code = lead & 0x07u;
    } else {
	// stray continuation byte or invalid lead
	offset++;
	return 0xFFFD;
    }

    if (offset + continuations >= text.size ()) {
	// truncated sequence at end of string
	offset++;
	return 0xFFFD;
    }

    for (int i = 1; i <= continuations; i++) {
	const auto cont = static_cast<uint8_t> (text[offset + i]);
	if ((cont & 0xC0) != 0x80) {
	    offset++;
	    return 0xFFFD;
	}
	code = (code << 6) | (cont & 0x3Fu);
    }

    offset += continuations + 1;
    return code;
}

namespace WallpaperEngine::Render::Objects {
/**
 * CPass needs a CRenderable owner for its uniform sources (brightness/alpha/color) and the
 * texture-0 fallback. Text effect passes run value-neutral — brightness and object alpha are
 * applied once at composite time (the blur-style shaders don't read them) — so every getter
 * returns a constant and the texture is the rasterized-text FBO.
 */
class CTextEffectHost final : public CRenderable {
public:
    CTextEffectHost (Wallpapers::CScene& scene, const Text& text, const Material& material) :
	CObject (scene, text), CRenderable (scene, text, material) { }

    void setTexture (std::shared_ptr<const TextureProvider> texture) { this->m_texture = std::move (texture); }

    [[nodiscard]] const float& getBrightness () const override { return m_one; }
    [[nodiscard]] const float& getUserAlpha () const override { return m_one; }
    [[nodiscard]] const float& getAlpha () const override { return m_one; }
    [[nodiscard]] const glm::vec3& getColor () const override { return m_white3; }
    [[nodiscard]] const glm::vec4& getColor4 () const override { return m_white4; }
    [[nodiscard]] const glm::vec3& getCompositeColor () const override { return m_white3; }

private:
    const float m_one = 1.0f;
    const glm::vec3 m_white3 = glm::vec3 (1.0f);
    const glm::vec4 m_white4 = glm::vec4 (1.0f);
};
} // namespace WallpaperEngine::Render::Objects

CText::CText (Wallpapers::CScene& scene, const Text& text) :
    CObject (scene, text), ScriptableObject (scene, text), m_text (text) {
    this->registerProperty ("color", *text.color->value);
    this->registerProperty ("alpha", *text.alpha->value);
    this->registerProperty ("origin", *text.origin->value);
    this->registerProperty ("scale", *text.scale->value);
    this->registerProperty ("visible", *text.visible->value);
    this->registerProperty ("pointSize", *text.pointSize->value);
    this->registerProperty ("text", *text.text->value);
    this->registerProperty ("pointSize", *text.pointSize->value);
}

CText::~CText () {
    if (m_layerHandle != Scripting::kInvalidLayerHandle) {
	this->getScene ().getScriptEngine ().destroyLayer (m_layerHandle);
	m_layerHandle = Scripting::kInvalidLayerHandle;
    }
    destroyEffectChain ();
    if (m_vbo != 0) {
	glDeleteBuffers (1, &m_vbo);
    }
    if (m_vao != 0) {
	glDeleteVertexArrays (1, &m_vao);
    }
    if (m_program != 0) {
	glDeleteProgram (m_program);
    }
    if (m_texture != 0) {
	glDeleteTextures (1, &m_texture);
    }
    if (m_ftFace != nullptr) {
	FT_Done_Face (m_ftFace);
    }
    if (m_ftLibrary != nullptr) {
	FT_Done_FreeType (m_ftLibrary);
    }
}

void CText::setup () {
    const bool scripted = m_text.text->value->getScriptSource ().has_value ();
    const auto& text = m_text.text->value->getString ();

    // Nothing to render and no script to produce text later → bail.
    if (text.empty () && !scripted) {
	return;
    }

    if (!initFreeType ()) {
	return;
    }

    if (!loadEmbeddedFont () && !loadSystemFont ()) {
	return;
    }

    m_lastPixelSize = computeEffectivePixelSize ();
    FT_Set_Pixel_Sizes (m_ftFace, 0, static_cast<FT_UInt> (m_lastPixelSize));

    buildShader ();
    // Scripted text may have an empty placeholder; use a single space so the
    // glyph texture has non-zero dimensions until the script produces a value.
    rebuildTextureFrom (text.empty () ? std::string (" ") : text);

    if (scripted) {
	initScriptLayer ();
    }

    if (!m_text.effects.empty ()) {
	try {
	    setupEffectChain ();
	} catch (const std::exception& e) {
	    // a failed effect (missing asset, shader translation) degrades to plain text
	    // instead of killing the object like CImage's all-or-nothing setup does
	    sLog.error ("CText: effect chain failed for '", m_text.name, "', rendering without effects: ", e.what ());
	    destroyEffectChain ();
	}
    }

    m_valid = m_texture != 0 && m_program != 0 && m_vao != 0;
}

glm::vec2 CText::computeEffectSurface () const {
    // Half-extents around the box center: whichever reaches farther, the authored box or
    // the current ink quad (its center is m_quadOffset in box-center space). WE text
    // overflows its box when limitwidth is off, so the box alone is NOT a safe raster
    // surface. The margin gives blur-style effects bleed room past the ink.
    const glm::vec2 margin = glm::max (m_text.padding, glm::vec2 (32.0f));
    const float hx = std::max (
	m_text.size.x * 0.5f, std::abs (m_quadOffset.x) + m_quadSize.x * 0.5f + margin.x
    );
    const float hy = std::max (
	m_text.size.y * 0.5f, std::abs (m_quadOffset.y) + m_quadSize.y * 0.5f + margin.y
    );
    return { hx * 2.0f, hy * 2.0f };
}

void CText::setupEffectChain () {
    // The chain renders on a fixed surface sized to cover BOTH the authored box and the
    // current ink (WE text overflows its box when limitwidth is off — MyGO 3558034522's
    // day/date line is ~3.5x its box), plus blur headroom (the authored padding, floored
    // at 32, exists exactly for that). The surface is centered on the box center, so the
    // quad's box-relative placement carries over unchanged. Text changes only re-rasterize
    // glyphs; if scripted text later outgrows the surface, render() degrades to plain text.
    m_effectSurface = computeEffectSurface ();
    const glm::vec2 surface = m_effectSurface;

    m_effectMaterial
	= Data::Parsers::MaterialParser::load (this->getScene ().getScene ().project, "materials/util/effectpassthrough.json");
    m_effectHost = std::make_unique<CTextEffectHost> (this->getScene (), m_text, *m_effectMaterial);

    m_fboA = std::make_shared<CFBO> (
	"_text_raster_" + std::to_string (this->getId ()), TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1, surface.x,
	surface.y, surface.x, surface.y
    );
    m_fboB = std::make_shared<CFBO> (
	"_text_pingpong_" + std::to_string (this->getId ()), TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1,
	surface.x, surface.y, surface.x, surface.y
    );
    m_effectHost->setTexture (m_fboA);

    // NDC quad + texcoords for the intermediate passes, same layout as CImage's pass buffers
    constexpr GLfloat passPosition[]
	= { -1.0, 1.0, 0.0f, -1.0, -1.0, 0.0f, 1.0, 1.0, 0.0f, 1.0, 1.0, 0.0f, -1.0, -1.0, 0.0f, 1.0, -1.0, 0.0f };
    constexpr GLfloat passTexcoord[] = { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };

    glGenBuffers (1, &m_ndcPosition);
    glBindBuffer (GL_ARRAY_BUFFER, m_ndcPosition);
    glBufferData (GL_ARRAY_BUFFER, sizeof (passPosition), passPosition, GL_STATIC_DRAW);
    glGenBuffers (1, &m_passTexCoord);
    glBindBuffer (GL_ARRAY_BUFFER, m_passTexCoord);
    glBufferData (GL_ARRAY_BUFFER, sizeof (passTexcoord), passTexcoord, GL_STATIC_DRAW);

    // build the passes, mirroring the material-pass subset of CImage::setup/setupPasses
    // (no copy commands, no scene writeback — the final result composites in render())
    std::shared_ptr<const TextureProvider> asInput = m_fboA;
    std::shared_ptr<const CFBO> drawTo = m_fboB;

    for (const auto& effect : m_text.effects) {
	if (!effect->visible->value->getBool ()) {
	    continue;
	}

	const auto fboProvider = std::make_shared<FBOProvider> (nullptr);
	m_effectProviders.push_back (fboProvider);

	for (const auto& fbo : effect->effect->fbos) {
	    m_effectClears.push_back (fboProvider->create (*fbo, TextureFlags_ClampUVs, surface));
	}

	auto curOverride = effect->passOverrides.begin ();
	const auto endOverride = effect->passOverrides.end ();

	for (const auto& effectPass : effect->effect->passes) {
	    if (!effectPass->material.has_value ()) {
		sLog.error ("CText: command passes are not supported on text effects, object ", this->getId ());
		continue;
	    }

	    for (const auto& pass : effectPass->material.value ()->passes) {
		const auto override = curOverride != endOverride
		    ? **curOverride
		    : std::optional<std::reference_wrapper<const ImageEffectPassOverride>> (std::nullopt);
		const auto target = effectPass->target.has_value ()
		    ? *effectPass->target
		    : std::optional<std::reference_wrapper<std::string>> (std::nullopt);

		auto* cpass = new Effects::CPass (*m_effectHost, fboProvider, *pass, override, effectPass->binds, target);

		const std::shared_ptr<const CFBO> prevDrawTo = drawTo;
		bool writesToTarget = false;

		if (cpass->getTarget ().has_value ()) {
		    const std::string& targetName = cpass->getTarget ().value ();
		    if (auto resolved = fboProvider->find (targetName); resolved != nullptr) {
			drawTo = resolved;
			writesToTarget = true;
		    } else {
			sLog.error ("CText: pass target FBO '", targetName, "' not found, object ", this->getId ());
		    }
		}

		cpass->setDestination (drawTo);
		cpass->setInput (asInput);
		cpass->setPosition (m_ndcPosition);
		cpass->setTexCoord (m_passTexCoord);
		// matrices keep CPass' shared identity defaults: intermediate passes are 1:1 blits

		m_effectPasses.push_back (cpass);

		if (writesToTarget) {
		    asInput = drawTo;
		    drawTo = prevDrawTo;
		} else {
		    // pingpong between the A/B buffers
		    const auto nextDraw = (drawTo == m_fboA) ? m_fboB : m_fboA;
		    asInput = drawTo;
		    drawTo = nextDraw;
		}
	    }

	    if (curOverride != endOverride) {
		++curOverride;
	    }
	}
    }

    if (m_effectPasses.empty ()) {
	destroyEffectChain ();
	return;
    }

    m_effectResult = std::dynamic_pointer_cast<const CFBO> (asInput);

    // composite program: same vertex shader, RGBA fragment
    GLuint vs = compileShader (GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compileShader (GL_FRAGMENT_SHADER, kFragmentShaderRGBA);
    if (vs == 0 || fs == 0 || m_effectResult == nullptr) {
	if (vs) {
	    glDeleteShader (vs);
	}
	if (fs) {
	    glDeleteShader (fs);
	}
	destroyEffectChain ();
	return;
    }

    m_compositeProgram = glCreateProgram ();
    glAttachShader (m_compositeProgram, vs);
    glAttachShader (m_compositeProgram, fs);
    glLinkProgram (m_compositeProgram);
    glDeleteShader (vs);
    glDeleteShader (fs);

    GLint status = GL_FALSE;
    glGetProgramiv (m_compositeProgram, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
	destroyEffectChain ();
	return;
    }

    m_cuMVP = glGetUniformLocation (m_compositeProgram, "uMVP");
    m_cuTexture = glGetUniformLocation (m_compositeProgram, "uTexture");

    // raster MVP: maps the quad's box-center-relative y-down coordinates onto the box FBO,
    // top of the box (y=-H/2) at texture row 0 so the standard v=0-at-top UVs stay valid
    m_baseMVP = glm::ortho (-surface.x * 0.5f, surface.x * 0.5f, -surface.y * 0.5f, surface.y * 0.5f);

    // composite quad: the whole box centered on the object origin, same y-down local
    // convention and v=0-at-top UV layout as the glyph quad, so the world MVP of the
    // direct path positions it identically
    const float hx = surface.x * 0.5f;
    const float hy = surface.y * 0.5f;
    const float quad[] = {
	// pos      // uv
	-hx, -hy, 0.0f, 0.0f, hx, -hy, 1.0f, 0.0f, hx,	hy, 1.0f, 1.0f,
	-hx, -hy, 0.0f, 0.0f, hx, hy,  1.0f, 1.0f, -hx, hy, 0.0f, 1.0f,
    };

    glGenVertexArrays (1, &m_compositeVao);
    glGenBuffers (1, &m_compositeVbo);
    glBindVertexArray (m_compositeVao);
    glBindBuffer (GL_ARRAY_BUFFER, m_compositeVbo);
    glBufferData (GL_ARRAY_BUFFER, sizeof (quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray (0);
    glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), reinterpret_cast<void*> (0));
    glEnableVertexAttribArray (1);
    glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), reinterpret_cast<void*> (2 * sizeof (float)));
    glBindVertexArray (0);

    m_effectsEnabled = true;
}

void CText::destroyEffectChain () {
    for (const auto* pass : m_effectPasses) {
	delete pass;
    }
    m_effectPasses.clear ();
    m_effectProviders.clear ();
    m_effectClears.clear ();
    m_effectResult = nullptr;
    m_fboA = nullptr;
    m_fboB = nullptr;
    m_effectHost = nullptr;
    m_effectMaterial = nullptr;
    if (m_compositeProgram != 0) {
	glDeleteProgram (m_compositeProgram);
	m_compositeProgram = 0;
    }
    if (m_compositeVbo != 0) {
	glDeleteBuffers (1, &m_compositeVbo);
	m_compositeVbo = 0;
    }
    if (m_compositeVao != 0) {
	glDeleteVertexArrays (1, &m_compositeVao);
	m_compositeVao = 0;
    }
    if (m_ndcPosition != 0) {
	glDeleteBuffers (1, &m_ndcPosition);
	m_ndcPosition = 0;
    }
    if (m_passTexCoord != 0) {
	glDeleteBuffers (1, &m_passTexCoord);
	m_passTexCoord = 0;
    }
    m_effectsEnabled = false;
}

void CText::renderEffectChain (const glm::mat4& mvp, const float brightness, const float alpha) {
    const glm::vec4 color = m_text.color->value->getVec4 ();

    // the scene sets its clear color once at setup, not per frame — leaking ours would
    // make every subsequent scene clear transparent black (same pattern as CFBO's ctor)
    GLfloat previousClearColor[4];
    glGetFloatv (GL_COLOR_CLEAR_VALUE, previousClearColor);
    const GLboolean depthWasEnabled = glIsEnabled (GL_DEPTH_TEST);

    // 1. rasterize the colored text into the surface FBO (single quad over a cleared target:
    //    blending off writes the exact texel data — rgb keeps the text color even where
    //    coverage is 0, so blurred edges keep their hue)
    glBindFramebuffer (GL_FRAMEBUFFER, m_fboA->getFramebuffer ());
    glViewport (0, 0, m_fboA->getRealWidth (), m_fboA->getRealHeight ());
    glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
    glClear (GL_COLOR_BUFFER_BIT);
    glDisable (GL_BLEND);
    glDisable (GL_DEPTH_TEST);

    glUseProgram (m_program);
    glUniformMatrix4fv (m_uMVP, 1, GL_FALSE, glm::value_ptr (m_baseMVP));
    glUniform4f (m_uColor, color.r, color.g, color.b, color.a);
    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, m_texture);
    glUniform1i (m_uTexture, 0);
    glBindVertexArray (m_vao);
    glDrawArrays (GL_TRIANGLES, 0, 6);
    glBindVertexArray (0);

    // 2. clear the chain's other destinations — nothing in the chain samples last frame
    for (const auto& fbo : m_effectClears) {
	glBindFramebuffer (GL_FRAMEBUFFER, fbo->getFramebuffer ());
	glClear (GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer (GL_FRAMEBUFFER, m_fboB->getFramebuffer ());
    glClear (GL_COLOR_BUFFER_BIT);

    // 3. run the passes (each binds its own destination FBO and viewport)
    for (auto* pass : m_effectPasses) {
	pass->render ();
    }

    // 4. composite the result onto the scene with the object's world transform
    glBindFramebuffer (GL_FRAMEBUFFER, this->getScene ().getWallpaperFramebuffer ());
    glViewport (0, 0, this->getScene ().getFBO ()->getRealWidth (), this->getScene ().getFBO ()->getRealHeight ());

    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable (GL_CULL_FACE);

    glUseProgram (m_compositeProgram);
    glUniformMatrix4fv (m_cuMVP, 1, GL_FALSE, glm::value_ptr (mvp));
    const GLint uColor = glGetUniformLocation (m_compositeProgram, "uColor");
    glUniform4f (uColor, brightness, brightness, brightness, alpha);
    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, m_effectResult->getTextureID (0));
    glUniform1i (m_cuTexture, 0);
    glBindVertexArray (m_compositeVao);
    glDrawArrays (GL_TRIANGLES, 0, 6);
    glBindVertexArray (0);

    // restore the state this chain touched that nothing downstream re-sets per draw
    glClearColor (previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
    if (depthWasEnabled == GL_TRUE) {
	glEnable (GL_DEPTH_TEST);
    }
}

bool CText::initFreeType () {
    if (FT_Init_FreeType (&m_ftLibrary) == 0) {
	return true;
    }
    sLog.error ("CText: FT_Init_FreeType failed for object ", m_text.name);
    return false;
}

bool CText::loadEmbeddedFont () {
    // Wallpapers packed in .pkg don't expose physical paths, so we read the font
    // into memory and use FT_New_Memory_Face. m_fontData must outlive the face.
    // `systemfont_*` references signal "use a system font"; let the fallback handle them.
    if (m_text.font.empty () || m_text.font.rfind ("systemfont_", 0) == 0) {
	return false;
    }

    try {
	auto stream = getAssetLocator ().read (m_text.font);
	stream->seekg (0, std::ios::end);
	const auto size = stream->tellg ();
	stream->seekg (0, std::ios::beg);
	m_fontData.resize (static_cast<size_t> (size));
	stream->read (reinterpret_cast<char*> (m_fontData.data ()), size);

	if (FT_New_Memory_Face (
		m_ftLibrary, m_fontData.data (), static_cast<FT_Long> (m_fontData.size ()), 0, &m_ftFace
	    )
	    == 0) {
	    return true;
	}

	sLog.error ("CText: FT_New_Memory_Face failed for '", m_text.font, "', falling back to system font");
    } catch (const std::exception& e) {
	sLog.error ("CText: cannot read font '", m_text.font, "': ", e.what (), ", falling back to system font");
    }

    m_fontData.clear ();
    return false;
}

bool CText::loadSystemFont () {
    std::string fontPath;
    for (const auto& candidate : kFontCandidates) {
	if (std::filesystem::exists (candidate)) {
	    fontPath = candidate;
	    break;
	}
    }
    if (fontPath.empty ()) {
	sLog.error ("CText: no usable system font found");
	return false;
    }
    if (FT_New_Face (m_ftLibrary, fontPath.c_str (), 0, &m_ftFace) != 0) {
	sLog.error ("CText: FT_New_Face failed for ", fontPath);
	return false;
    }
    return true;
}

unsigned int CText::computeEffectivePixelSize () const {
    // Wallpaper Engine defines pointsize as "Size of the font in points for 300 DPI"
    // (ui/dist/monaco/autocomplete/lib.sceneScript.d.ts, ITextLayer.pointsize), so the
    // glyph EM square is pointsize * 300/72 pixels in scene units. The object's scale
    // (own and inherited, via the world matrix) then scales the rasterized quad like
    // WE does — which is also why WE's editor warns that scaling text reduces quality.
    constexpr float kTextDPI = 300.0f;
    constexpr float kPointsPerInch = 72.0f;
    return std::max<unsigned int> (
	1u, static_cast<unsigned int> (m_text.pointSize->value->getFloat () * kTextDPI / kPointsPerInch)
    );
}

void CText::initScriptLayer () {
    const auto& script = m_text.text->value->getScriptSource ();

    if (!script.has_value ()) {
	return;
    }

    m_layerHandle = this->getScene ().getScriptEngine ().createLayerScript (
	*script, m_text.text->value->getProperties (), m_text.text->value->getString ()
    );

    if (m_layerHandle == Scripting::kInvalidLayerHandle) {
	sLog.error ("CText: createLayerScript failed for '", m_text.name, "'");
    }
}

void CText::rebuildTextureFrom (const std::string& text) {
    // Lays the text out like Wallpaper Engine: lines split on \n are aligned inside the
    // authored bounding box (horizontalalign/verticalalign, inset by padding), the box
    // being centered on the object origin. Text may overflow the box — limitwidth and
    // limitrows are not implemented. The texture covers the ink bounding box only and
    // the quad is offset from the box center accordingly (see uploadQuadVertices).
    //
    // Safe to call repeatedly: GL handles (texture, VAO, VBO) are reused when
    // already allocated, so dynamic/scripted text can regenerate the glyph
    // bitmap every time the rendered string changes without leaking.
    FT_GlyphSlot slot = m_ftFace->glyph;

    // metrics-based line boxes: stable across glyph mixes and meaningful for empty lines
    const int lineHeight = static_cast<int> (m_ftFace->size->metrics.height >> 6);
    const int ascender = static_cast<int> (m_ftFace->size->metrics.ascender >> 6);

    // \n splitting is UTF-8 safe: continuation bytes are always >= 0x80
    struct Line {
	std::string text;
	int width = 0;
	int x = 0;
    };
    std::vector<Line> lines;
    for (size_t start = 0, i = 0; i <= text.size (); i++) {
	if (i == text.size () || text[i] == '\n') {
	    lines.push_back ({ .text = text.substr (start, i - start) });
	    start = i + 1;
	}
    }

    int maxLineWidth = 0;
    for (auto& line : lines) {
	for (size_t offset = 0; offset < line.text.size ();) {
	    if (FT_Load_Char (m_ftFace, static_cast<FT_ULong> (nextUtf8Codepoint (line.text, offset)), FT_LOAD_RENDER)
		!= 0) {
		continue;
	    }
	    line.width += slot->advance.x >> 6;
	}
	maxLineWidth = std::max (maxLineWidth, line.width);
    }

    const int blockHeight = static_cast<int> (lines.size ()) * lineHeight;

    // authored box centered on the origin; scenes without one get the ink extents
    glm::vec2 box = m_text.size;
    if (box.x <= 0.0f || box.y <= 0.0f) {
	box = { static_cast<float> (maxLineWidth), static_cast<float> (blockHeight) };
    }

    // content rect after padding, in box-local raster coordinates (top-left origin, +y down —
    // the same convention the quad's local space uses)
    const glm::vec2 padding = m_text.padding;
    const float contentX0 = padding.x;
    const float contentX1 = box.x - padding.x;
    const float contentY0 = padding.y;
    const float contentY1 = box.y - padding.y;

    float blockTop;
    if (m_text.verticalalign == "top") {
	blockTop = contentY0;
    } else if (m_text.verticalalign == "bottom") {
	blockTop = contentY1 - static_cast<float> (blockHeight);
    } else {
	blockTop = contentY0 + ((contentY1 - contentY0) - static_cast<float> (blockHeight)) * 0.5f;
    }

    float inkX0 = std::numeric_limits<float>::max ();
    float inkX1 = std::numeric_limits<float>::lowest ();
    for (auto& line : lines) {
	float x;
	if (m_text.alignment == "left") {
	    x = contentX0;
	} else if (m_text.alignment == "right") {
	    x = contentX1 - static_cast<float> (line.width);
	} else {
	    x = contentX0 + ((contentX1 - contentX0) - static_cast<float> (line.width)) * 0.5f;
	}
	line.x = static_cast<int> (std::round (x));
	inkX0 = std::min (inkX0, x);
	inkX1 = std::max (inkX1, x + static_cast<float> (line.width));
    }

    // ink bounding box, with a margin for glyphs poking past the font's ascender/descender
    constexpr int kInkMargin = 4;
    const int x0 = static_cast<int> (std::floor (inkX0)) - kInkMargin;
    const int y0 = static_cast<int> (std::floor (blockTop)) - kInkMargin;
    const int width = std::max (1, static_cast<int> (std::ceil (inkX1 - inkX0)) + kInkMargin * 2);
    const int height = std::max (1, blockHeight + kInkMargin * 2);
    std::vector<uint8_t> pixels (static_cast<size_t> (width) * height, 0);

    for (size_t i = 0; i < lines.size (); i++) {
	const auto& line = lines[i];
	int penX = line.x - x0;
	const int baseline
	    = static_cast<int> (std::round (blockTop)) - y0 + ascender + static_cast<int> (i) * lineHeight;

	for (size_t offset = 0; offset < line.text.size ();) {
	    if (FT_Load_Char (m_ftFace, static_cast<FT_ULong> (nextUtf8Codepoint (line.text, offset)), FT_LOAD_RENDER)
		!= 0) {
		continue;
	    }

	    const auto& bmp = slot->bitmap;
	    const int originX = penX + slot->bitmap_left;
	    const int originY = baseline - slot->bitmap_top;

	    for (unsigned int row = 0; row < bmp.rows; ++row) {
		for (unsigned int col = 0; col < bmp.width; ++col) {
		    const int dstX = originX + static_cast<int> (col);
		    const int dstY = originY + static_cast<int> (row);
		    if (dstX < 0 || dstX >= width || dstY < 0 || dstY >= height) {
			continue;
		    }
		    pixels[static_cast<size_t> (dstY) * width + dstX] = bmp.buffer[row * bmp.pitch + col];
		}
	    }

	    penX += slot->advance.x >> 6;
	}
    }

    const bool firstUpload = (m_texture == 0);
    if (firstUpload) {
	glGenTextures (1, &m_texture);
    }
    glBindTexture (GL_TEXTURE_2D, m_texture);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data ());
    if (firstUpload) {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    m_textureSize = { width, height };
    m_quadSize = { static_cast<float> (width), static_cast<float> (height) };
    // offset from the object origin (= the authored box center) to the ink bbox center,
    // in the quad's local +y-down space
    m_quadOffset = {
	(static_cast<float> (x0) + static_cast<float> (width) * 0.5f) - box.x * 0.5f,
	(static_cast<float> (y0) + static_cast<float> (height) * 0.5f) - box.y * 0.5f,
    };
    m_lastRenderedText = text;

    uploadQuadVertices ();
}

void CText::buildShader () {
    GLuint vs = compileShader (GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compileShader (GL_FRAGMENT_SHADER, kFragmentShader);
    if (vs == 0 || fs == 0) {
	if (vs) {
	    glDeleteShader (vs);
	}
	if (fs) {
	    glDeleteShader (fs);
	}
	return;
    }

    m_program = glCreateProgram ();
    glAttachShader (m_program, vs);
    glAttachShader (m_program, fs);
    glLinkProgram (m_program);
    glDeleteShader (vs);
    glDeleteShader (fs);

    GLint status = GL_FALSE;
    glGetProgramiv (m_program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
	char log[1024];
	glGetProgramInfoLog (m_program, sizeof (log), nullptr, log);
	sLog.error ("CText program link failed: ", log);
	glDeleteProgram (m_program);
	m_program = 0;
	return;
    }

    m_uMVP = glGetUniformLocation (m_program, "uMVP");
    m_uColor = glGetUniformLocation (m_program, "uColor");
    m_uTexture = glGetUniformLocation (m_program, "uTexture");
}

void CText::uploadQuadVertices () {
    // Quad in local raster-space coordinates (+y down, matching the texture rows; render()'s
    // mirror pair maps this into the y-up world so the glyphs stay upright). The quad covers
    // the ink bounding box, offset from the object origin (the authored box center) so the
    // authored alignment inside the box is preserved. VBO contents are re-uploaded whenever
    // the glyph bitmap is rebuilt so the quad always matches the current texture.
    const float hx = m_quadSize.x * 0.5f;
    const float hy = m_quadSize.y * 0.5f;
    const float x0 = m_quadOffset.x - hx;
    const float x1 = m_quadOffset.x + hx;
    const float y0 = m_quadOffset.y - hy;
    const float y1 = m_quadOffset.y + hy;
    // UV.v=0 = texture top row = the visual top of the text
    const float verts[] = {
	// pos      // uv
	x0, y0, 0.0f, 0.0f, x1, y0, 1.0f, 0.0f, x1, y1, 1.0f, 1.0f,
	x0, y0, 0.0f, 0.0f, x1, y1, 1.0f, 1.0f, x0, y1, 0.0f, 1.0f,
    };

    const bool firstUpload = (m_vao == 0);
    if (firstUpload) {
	glGenVertexArrays (1, &m_vao);
	glGenBuffers (1, &m_vbo);
    }
    glBindVertexArray (m_vao);
    glBindBuffer (GL_ARRAY_BUFFER, m_vbo);
    glBufferData (GL_ARRAY_BUFFER, sizeof (verts), verts, GL_DYNAMIC_DRAW);
    if (firstUpload) {
	glEnableVertexAttribArray (0);
	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), reinterpret_cast<void*> (0));
	glEnableVertexAttribArray (1);
	glVertexAttribPointer (
	    1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), reinterpret_cast<void*> (2 * sizeof (float))
	);
    }
    glBindVertexArray (0);
}

void CText::render () {
    if (!m_valid) {
	return;
    }
    if (!m_text.visible->value->getBool ()) {
	return;
    }
    // a hidden container hides its whole subtree; MyGO's Clock/Date children carry no
    // visible of their own — the parent's user-property toggle must gate them too
    if (!this->isVisibleThroughParents ()) {
	return;
    }

#if !NDEBUG
    std::string str = "Text " + this->getObject ().name + " (" + std::to_string (this->getObject ().id) + ")";
    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, str.c_str ());
#endif /* DEBUG */
    std::string renderedText = m_lastRenderedText;
    if (m_layerHandle != Scripting::kInvalidLayerHandle) {
	auto& se = this->getScene ().getScriptEngine ();
	se.tickLayer (
	    m_layerHandle, static_cast<double> (getScene ().getTime ()),
	    static_cast<double> (getScene ().getDeltaTime ()), static_cast<double> (getScene ().getFps ())
	);
	const std::string current = se.layerText (m_layerHandle);
	renderedText = current.empty () ? std::string (" ") : current;
    }

    const unsigned int pixelSize = computeEffectivePixelSize ();
    if (pixelSize != m_lastPixelSize) {
	m_lastPixelSize = pixelSize;
	FT_Set_Pixel_Sizes (m_ftFace, 0, static_cast<FT_UInt> (m_lastPixelSize));
	rebuildTextureFrom (renderedText);
    } else if (renderedText != m_lastRenderedText) {
	rebuildTextureFrom (renderedText);
    }

    // scripted text can outgrow the chain surface sized at setup (the FBOs and pass texture
    // chains are fixed); degrade to plain rendering permanently rather than clipping
    if (m_effectsEnabled) {
	const glm::vec2 needed = computeEffectSurface ();
	if (needed.x > m_effectSurface.x || needed.y > m_effectSurface.y) {
	    sLog.error (
		"CText: text outgrew the effect surface for '", m_text.name, "' (", needed.x, "x", needed.y, " > ",
		m_effectSurface.x, "x", m_effectSurface.y, "), disabling its effects"
	    );
	    destroyEffectChain ();
	}
    }

    const glm::vec4 color = m_text.color->value->getVec4 ();
    const float alpha = m_text.alpha->value->getFloat ();
    const float brightness = m_text.brightness->value->getFloat ();

    // 3D scenes: no screen-space centering or parallax; the world matrix carries the
    // transform chain (origin, parents, text scale)
    if (getScene ().getScene ().camera.projection.isPerspective) {
	// the shared VBO bakes the 2D path's vflip into its UVs (glyph top at -y); world
	// space is y-up, so mirror the centered quad back around its own center
	const glm::mat4 model
	    = this->resolveWorldMatrix () * glm::scale (glm::mat4 (1.0f), glm::vec3 (1.0f, -1.0f, 1.0f));
	const glm::mat4 mvp
	    = getScene ().getCamera ().getProjection () * getScene ().getCamera ().getLookAt () * model;

	if (m_effectsEnabled) {
	    renderEffectChain (mvp, brightness, alpha);
#if !NDEBUG
	    glPopDebugGroup ();
#endif /* DEBUG */
	    return;
	}

	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	// the mirror flips the quad's winding, and the last material pass may have left
	// face culling on; text plates are double-sided anyway
	glDisable (GL_CULL_FACE);

	glUseProgram (m_program);
	glUniformMatrix4fv (m_uMVP, 1, GL_FALSE, glm::value_ptr (mvp));
	glUniform4f (m_uColor, color.r * brightness, color.g * brightness, color.b * brightness, color.a * alpha);

	glActiveTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, m_texture);
	glUniform1i (m_uTexture, 0);

	glBindVertexArray (m_vao);
	glDrawArrays (GL_TRIANGLES, 0, 6);
	glBindVertexArray (0);
#if !NDEBUG
	glPopDebugGroup ();
#endif /* DEBUG */
	return;
    }

    const float scene_w = getScene ().getCamera ().getWidth ();
    const float scene_h = getScene ().getCamera ().getHeight ();

    // Scene coordinates are y-up (bottom-left origin) — the same convention CImage maps with
    // (x - w/2, h/2 - y) before the Wayland/GLFW vflip corrects the presentation on screen.
    // Mirror the composed world transform (origin, parent chain, text scale — via
    // resolveWorldMatrix) into that convention, then re-mirror the quad around its own center
    // so the glyph texture stays upright. Verified against MyGO 3558034522: the clock stack
    // sits at 0.93 of the canvas height (near the top) and its Date/Clock children hang at
    // negative offsets (below the container): day line, then date, then time.
    glm::vec3 parallaxShift = { 0.0f, 0.0f, 0.0f };

    // camera parallax translation in the mapped screen space, same signs as CImage's
    // updateScreenSpacePosition; locktransforms is only an editor-UI lock
    if (getScene ().getScene ().camera.parallax.enabled->value->getBool ()
	&& !getScene ().getContext ().getApp ().getContext ().settings.mouse.disableparallax) {
	const float amount = getScene ().getScene ().camera.parallax.amount->value->getFloat ();
	const glm::vec2 depth = this->resolveParallaxDepth ();
	const glm::vec2* displacement = getScene ().getParallaxDisplacement ();
	const float referenceSize
	    = static_cast<float> (getScene ().getWidth ()) * Wallpapers::CScene::PARALLAX_TRANSLATION_SPAN;

	parallaxShift.x = -depth.x * amount * displacement->x * referenceSize;
	parallaxShift.y = depth.y * amount * displacement->y * referenceSize;
    }

    const glm::mat4 model = glm::translate (glm::mat4 (1.0f), parallaxShift)
	* glm::scale (glm::mat4 (1.0f), glm::vec3 (1.0f, -1.0f, 1.0f))
	* glm::translate (glm::mat4 (1.0f), glm::vec3 (-scene_w * 0.5f, -scene_h * 0.5f, 0.0f))
	* this->resolveWorldMatrix () * glm::scale (glm::mat4 (1.0f), glm::vec3 (1.0f, -1.0f, 1.0f));

    const glm::mat4 mvp = getScene ().getCamera ().getProjection () * getScene ().getCamera ().getLookAt () * model;

    if (m_effectsEnabled) {
	renderEffectChain (mvp, brightness, alpha);
#if !NDEBUG
	glPopDebugGroup ();
#endif /* DEBUG */
	return;
    }

    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram (m_program);
    glUniformMatrix4fv (m_uMVP, 1, GL_FALSE, glm::value_ptr (mvp));
    glUniform4f (m_uColor, color.r * brightness, color.g * brightness, color.b * brightness, color.a * alpha);

    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, m_texture);
    glUniform1i (m_uTexture, 0);

    glBindVertexArray (m_vao);
    glDrawArrays (GL_TRIANGLES, 0, 6);
    glBindVertexArray (0);
#if !NDEBUG
    glPopDebugGroup ();
#endif /* DEBUG */
}
