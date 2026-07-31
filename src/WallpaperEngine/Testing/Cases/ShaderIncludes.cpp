#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Assets/AssetLocator.h"
#include "WallpaperEngine/Render/Shaders/GLSLContext.h"
#include "WallpaperEngine/Render/Shaders/ShaderUnit.h"

using WallpaperEngine::Assets::AssetLocator;
using WallpaperEngine::Data::Model::ComboMap;
using WallpaperEngine::Data::Model::ShaderConstantMap;
using WallpaperEngine::Data::Model::TextureMap;
using WallpaperEngine::FileSystem::Container;
using WallpaperEngine::Render::Shaders::GLSLContext;
using WallpaperEngine::Render::Shaders::ShaderUnit;

namespace {
std::unique_ptr<AssetLocator> shaderAssets (const std::string& header, const std::string& nestedHeader = "") {
    auto container = std::make_unique<Container> ();
    container->getVFS ().add ("shaders/test_ordering.h", header);
    if (!nestedHeader.empty ()) {
	container->getVFS ().add ("shaders/test_nested.h", nestedHeader);
    }
    return std::make_unique<AssetLocator> (std::move (container));
}

std::string
compileFragment (const std::string& header, const std::string& source, const std::string& nestedHeader = "") {
    const auto assets = shaderAssets (header, nestedHeader);
    const ShaderConstantMap constants;
    const TextureMap textures;
    const ComboMap combos;
    ShaderUnit unit (
	GLSLContext::UnitType_Fragment, "test_ordering.frag", source, *assets, constants, textures, textures, combos,
	combos
    );
    return unit.compile ();
}
} // namespace

TEST_CASE (
    "include macros reach authored helpers without moving header functions before uniforms", "[shader][include]"
) {
    const std::string fragment = compileFragment (
	"#define INCLUDED_SCALE 2.0\n"
	"#include \"test_nested.h\"\n"
	"vec4 sampleSource(vec2 uv) {\n"
	"    return texSample2D(g_Texture0, uv) * INCLUDED_SCALE * NESTED_SCALE;\n"
	"}\n",
	"#include \"test_ordering.h\"\n"
	"float authoredHelper() {\n"
	"    return INCLUDED_SCALE * NESTED_SCALE;\n"
	"}\n"
	"uniform sampler2D g_Texture0;\n"
	"void main() {\n"
	"    gl_FragColor = sampleSource(vec2(0.0)) * authoredHelper();\n"
	"}\n",
	"#define NESTED_SCALE 3.0\n"
    );

    const size_t define = fragment.find ("#define INCLUDED_SCALE");
    const size_t nestedDefine = fragment.find ("#define NESTED_SCALE");
    const size_t authoredHelper = fragment.find ("float authoredHelper");
    const size_t uniform = fragment.find ("uniform sampler2D g_Texture0");
    const size_t includedHelper = fragment.find ("vec4 sampleSource");

    REQUIRE (define != std::string::npos);
    REQUIRE (nestedDefine != std::string::npos);
    REQUIRE (authoredHelper != std::string::npos);
    REQUIRE (uniform != std::string::npos);
    REQUIRE (includedHelper != std::string::npos);
    CHECK (define < authoredHelper);
    CHECK (nestedDefine < authoredHelper);
    CHECK (uniform < includedHelper);

    const auto translated = GLSLContext::get ().toGlsl (
	"#version 330\n"
	"void main() {\n"
	"    gl_Position = vec4(0.0);\n"
	"}\n",
	fragment
    );
    CHECK_FALSE (translated.first.empty ());
    CHECK_FALSE (translated.second.empty ());
}

TEST_CASE ("conditional include macros keep their preprocessor scope", "[shader][include]") {
    const std::string fragment = compileFragment (
	"#if FEATURE_ENABLED\n"
	"#define CONDITIONAL_SCALE 2.0\n"
	"#endif\n"
	"vec4 sampleSource(vec2 uv) {\n"
	"    return texSample2D(g_Texture0, uv);\n"
	"}\n",
	"#include \"test_ordering.h\"\n"
	"uniform sampler2D g_Texture0;\n"
	"void main() {\n"
	"    gl_FragColor = sampleSource(vec2(0.0));\n"
	"}\n"
    );

    const size_t conditional = fragment.find ("#if FEATURE_ENABLED");
    const size_t define = fragment.find ("#define CONDITIONAL_SCALE");
    const size_t endif = fragment.find ("#endif", define);

    REQUIRE (conditional != std::string::npos);
    REQUIRE (define != std::string::npos);
    REQUIRE (endif != std::string::npos);
    CHECK (conditional < define);
    CHECK (define < endif);
}

TEST_CASE ("conditionally included headers do not contribute early macros", "[shader][include]") {
    const std::string fragment = compileFragment (
	"#define ROOT_CONDITIONAL_SCALE 2.0\n"
	"vec4 sampleSource(vec2 uv) {\n"
	"    return texSample2D(g_Texture0, uv);\n"
	"}\n",
	"#if FEATURE_ENABLED\n"
	"#include \"test_ordering.h\"\n"
	"#endif\n"
	"uniform sampler2D g_Texture0;\n"
	"void main() {\n"
	"    gl_FragColor = sampleSource(vec2(0.0));\n"
	"}\n"
    );

    const size_t rootEndif = fragment.find ("#endif");
    const size_t define = fragment.find ("#define ROOT_CONDITIONAL_SCALE");
    const size_t uniform = fragment.find ("uniform sampler2D g_Texture0");
    const size_t includedHelper = fragment.find ("vec4 sampleSource");

    REQUIRE (rootEndif != std::string::npos);
    REQUIRE (define != std::string::npos);
    REQUIRE (uniform != std::string::npos);
    REQUIRE (includedHelper != std::string::npos);
    CHECK (rootEndif < define);
    CHECK (uniform < includedHelper);
}
