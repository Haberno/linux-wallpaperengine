#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

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

std::pair<std::string, std::string>
compileLinked (const std::string& vertexSource, const std::string& fragmentSource, const ComboMap& combos) {
    const auto assets = shaderAssets ("");
    const ShaderConstantMap constants;
    const TextureMap textures;
    ShaderUnit vertex (
	GLSLContext::UnitType_Vertex, "test_linked.vert", vertexSource, *assets, constants, textures, textures, combos,
	combos
    );
    ShaderUnit fragment (
	GLSLContext::UnitType_Fragment, "test_linked.frag", fragmentSource, *assets, constants, textures, textures,
	combos, combos
    );
    vertex.linkToUnit (&fragment);
    fragment.linkToUnit (&vertex);
    return { vertex.compile (), fragment.compile () };
}
} // namespace

TEST_CASE ("vector shader defaults accept numbers without dropping the layer", "[shader][parameter][regression]") {
    const auto assets = shaderAssets ("");
    const ShaderConstantMap constants;
    const TextureMap textures;
    const ComboMap combos;
    for (const std::string value : { "0", "0.375", "-2" }) {
	CAPTURE (value);
	// Deformer Simulation on 3761619125 declares a numeric vec2 default
	// inside an inactive option. Metadata is still read before preprocessing.
	const std::string source
	    = "#if 0\nuniform vec2 u_Pos; // {\"material\":\"pos\",\"default\":" + value + "}\n#endif\n"
	      "uniform vec3 u_Color; // {\"material\":\"color\",\"default\":" + value + "}\n"
	      "uniform vec4 u_Depth; // {\"material\":\"depth\",\"default\":" + value + "}\n"
	      "uniform vec4 u_Components; // {\"material\":\"components\",\"default\":\"0.5 1 2 3\"}\n"
	      "void main() { gl_FragColor = vec4(u_Color, 1.0) * u_Depth * u_Components; }\n";
	ShaderUnit unit (
	    GLSLContext::UnitType_Fragment, "vector_defaults.frag", source, *assets, constants, textures, textures,
	    combos, combos
	);
	const auto& parameters = unit.getParameters ();
	REQUIRE (parameters.size () == 4);
	CHECK (parameters[0]->getIdentifierName () == "pos");
	CHECK (parameters[0]->getVec2 () == glm::vec2 (std::stof (value)));
	CHECK (parameters[1]->getVec3 () == glm::vec3 (std::stof (value)));
	CHECK (parameters[2]->getVec4 () == glm::vec4 (std::stof (value)));
	CHECK (parameters[3]->getVec4 () == glm::vec4 (0.5f, 1.0f, 2.0f, 3.0f));
	const auto translated = GLSLContext::get ().toGlsl (
	    "#version 330\nvoid main() { gl_Position = vec4(0.0); }\n", unit.compile ()
	);
	CHECK_FALSE (translated.second.empty ());
    }
}

TEST_CASE ("continued shader lines are joined before metadata and GLSL parsing", "[shader][continuation]") {
    for (const std::string newline : { "\n", "\r\n" }) {
	const std::string continued = "\\" + newline;
	const auto fragment = compileFragment (
	    "#define INCLUDED_VALUE (1 " + continued + "+ 2)\n",
	    "#include \"test_ordering.h\"\n"
	    "// continued comment " + continued + "#include \"does_not_exist.h\"\n"
	    "#define COMBINED to" + continued + "ken\n"
	    "#if INCLUDED_VALUE != 3\n#error broken macro continuation\n#endif\n"
	    "uniform vec2 u_Pos; // {\"material\":\"pos\", " + continued + "\"default\":\"0 0\"}\n"
	    "void main() {\nfloat token = 1.0 " + continued + "+ 2.0;\n"
	    "gl_FragColor = vec4(COMBINED + u_Pos.x);\n}\n"
	);
	CHECK (fragment.find ("#define COMBINED token") != std::string::npos);
	CHECK (fragment.find ("\\\n") == std::string::npos);
	CHECK (fragment.find ("\\\r\n") == std::string::npos);
	const auto translated = GLSLContext::get ().toGlsl (
	    "#version 330\nvoid main() { gl_Position = vec4(0.0); }\n", fragment
	);
	CHECK_FALSE (translated.second.empty ());
    }
}

TEST_CASE ("malformed includes fail at their own line", "[shader][include][regression]") {
    for (const std::string directive : { "#include", "#include test_ordering.h", "#include \"\"",
					 "#include \"test_ordering.h", "#include \"test_ordering.h\" junk" }) {
	CAPTURE (directive);
	CHECK_THROWS_WITH (
	    compileFragment (
		"#define INCLUDED_SCALE 2.0\n", directive + "\n#include \"test_ordering.h\"\nvoid main() {}\n"
	    ),
	    Catch::Matchers::ContainsSubstring ("Malformed #include")
	);
	CHECK_THROWS_WITH (
	    compileFragment (directive, "#include \"test_ordering.h\"\nvoid main() {}\n"),
	    Catch::Matchers::ContainsSubstring ("Malformed #include")
	);
    }
}

TEST_CASE ("commented includes are ignored and spaced includes are expanded", "[shader][include][regression]") {
    const auto fragment = compileFragment (
	"#define LIVE_INCLUDE 2.0\n# include \"test_nested.h\"",
	"// #include \"missing.h\"\n/*\n#include \"missing.h\"\n*/\n"
	"# include \"test_ordering.h\" /* a comment continues\n#include \"missing.h\"\n*/\n"
	"void main() { gl_FragColor = vec4(LIVE_INCLUDE * NESTED_INCLUDE); }\n",
	"#define NESTED_INCLUDE 3.0\n"
    );
    CHECK (fragment.find ("#define LIVE_INCLUDE 2.0") != std::string::npos);
    CHECK (fragment.find ("#define NESTED_INCLUDE 3.0") != std::string::npos);
    CHECK (fragment.find ("tried including file missing.h") == std::string::npos);
    const auto translated
	= GLSLContext::get ().toGlsl ("#version 330\nvoid main() { gl_Position = vec4(0.0); }\n", fragment);
    CHECK_FALSE (translated.first.empty ());
    CHECK_FALSE (translated.second.empty ());
}

TEST_CASE ("combo editor requirements do not enable other shader options", "[shader][combo][regression]") {
    // The reference metadata reader stores only combo/default for [COMBO].
    // Deformer Simulation leaves these child settings on while SHOW_LOCATOR
    // is off; forcing the parent on replaces the image with an editor grid.
    const std::string vertexSource
	= "// [COMBO] {\"combo\":\"SHOW_LOCATOR\",\"default\":0}\n"
	  "// [COMBO] {\"combo\":\"DEFORMER_LOCATOR\",\"default\":0,\"require\":{\"SHOW_LOCATOR\":1}}\n"
	  "// [COMBO] {\"combo\":\"OVERLAY_LOCATOR\",\"default\":1,\"require\":{\"SHOW_LOCATOR\":1,\"DEFORMER_LOCATOR\":1}}\n"
	  "void main() { gl_Position = vec4(float(SHOW_LOCATOR)); }\n";
    const std::string fragmentSource
	= "// [COMBO] {\"combo\":\"LIGHTING\",\"default\":1}\n"
	  "// [COMBO] {\"combo\":\"RIMLIGHTING\",\"default\":0,\"require\":{\"LIGHTING\":1}}\n"
	  "void main() { gl_FragColor = vec4(float(SHOW_LOCATOR + LIGHTING)); }\n";
    for (const ComboMap combos : {
	     ComboMap { { "DEFORMER_LOCATOR", 1 }, { "RIMLIGHTING", 1 }, { "LIGHTING", 0 } },
	     ComboMap { { "DEFORMER_LOCATOR", 1 }, { "SHOW_LOCATOR", 0 }, { "LIGHTING", 0 } },
	     ComboMap { { "SHOW_LOCATOR", 1 }, { "RIMLIGHTING", 1 }, { "LIGHTING", 1 } },
	 }) {
	const auto [vertex, fragment] = compileLinked (vertexSource, fragmentSource, combos);
	const int locator = combos.contains ("SHOW_LOCATOR") ? combos.at ("SHOW_LOCATOR") : 0;
	for (const auto& source : { vertex, fragment }) {
	    CHECK (source.find ("#define SHOW_LOCATOR " + std::to_string (locator) + "\n") != std::string::npos);
	    CHECK (source.find ("#define LIGHTING " + std::to_string (combos.at ("LIGHTING")) + "\n")
		   != std::string::npos);
	}
	const auto translated = GLSLContext::get ().toGlsl (vertex, fragment);
	CHECK_FALSE (translated.first.empty ());
	CHECK_FALSE (translated.second.empty ());
    }
}

TEST_CASE ("invalid combo names do not corrupt shader definitions", "[shader][combo][regression]") {
    for (const std::string metadata : { "{}", "{\"combo\":null}", "{\"combo\":false}", "{\"combo\":\"\"}",
					"{\"combo\":\"1INVALID\"}", "{\"combo\":\"HAS SPACE\"}" }) {
	CAPTURE (metadata);
	const auto fragment = compileFragment (
	    "",
	    "// [COMBO] " + metadata
		+ "\n// [COMBO] {\"combo\":\"VALID_DEFAULT\"}\n"
		  "void main() { gl_FragColor = vec4(float(VALID_DEFAULT)); }\n"
	);
	CHECK (fragment.find ("#define VALID_DEFAULT 0") != std::string::npos);
	const auto translated
	    = GLSLContext::get ().toGlsl ("#version 330\nvoid main() { gl_Position = vec4(0.0); }\n", fragment);
	CHECK_FALSE (translated.first.empty ());
	CHECK_FALSE (translated.second.empty ());
    }
    const auto [vertex, fragment] = compileLinked (
	"void main() { gl_Position = vec4(float(VALID)); }\n", "void main() { gl_FragColor = vec4(float(VALID)); }\n",
	{ { "", 1 }, { "HAS SPACE", 1 }, { "1INVALID", 1 }, { "VALID", 1 } }
    );
    CHECK (vertex.find ("#define VALID 1") != std::string::npos);
    CHECK (fragment.find ("#define VALID 1") != std::string::npos);
    CHECK (fragment.find ("#define  1") == std::string::npos);
    CHECK (fragment.find ("#define HAS SPACE") == std::string::npos);
    const auto translated = GLSLContext::get ().toGlsl (vertex, fragment);
    CHECK_FALSE (translated.first.empty ());
    CHECK_FALSE (translated.second.empty ());
}

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
    CHECK (define < includedHelper);
    CHECK (nestedDefine < includedHelper);
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

TEST_CASE ("matching conditional TexCoord widths stay synchronized", "[shader][compat]") {
    const std::string vertexSource =
	"uniform float g_Test;\n"
	"#if LIGHTMAP\n"
	"attribute vec4 a_TexCoordVec4;\n"
	"varying vec4 v_TexCoord;\n"
	"#else\n"
	"attribute vec2 a_TexCoord;\n"
	"varying vec2 v_TexCoord;\n"
	"#endif\n"
	"void main() {\n"
	"#if LIGHTMAP\n"
	"    v_TexCoord = a_TexCoordVec4;\n"
	"#else\n"
	"    v_TexCoord = a_TexCoord;\n"
	"#endif\n"
	"    gl_Position = vec4(0.0);\n"
	"}\n";
    const std::string fragmentSource =
	"uniform float g_Test;\n"
	"#if LIGHTMAP\n"
	"varying vec4 v_TexCoord;\n"
	"#else\n"
	"varying vec2 v_TexCoord;\n"
	"#endif\n"
	"void main() { out_FragColor = vec4(v_TexCoord.xy, 0.0, 1.0); }\n";
    const ComboMap combos { { "LIGHTMAP", 0 } };

    const auto [vertex, fragment] = compileLinked (vertexSource, fragmentSource, combos);
    CHECK (vertex.find ("varying vec2 v_TexCoord;") != std::string::npos);
    CHECK (fragment.find ("varying vec2 v_TexCoord;") != std::string::npos);

    const auto translated = GLSLContext::get ().toGlsl (vertex, fragment);
    REQUIRE_FALSE (translated.first.empty ());
    REQUIRE_FALSE (translated.second.empty ());
    CHECK (translated.first.find ("out vec2 v_TexCoord;") != std::string::npos);
    CHECK (translated.second.find ("in vec2 v_TexCoord;") != std::string::npos);
}

TEST_CASE ("min max preserve native vector scalar overloads", "[shader][minmax]") {
    struct NumericType {
        const char* vectorPrefix;
        const char* scalar;
    };
    for (const NumericType type : { NumericType { "vec", "0.5" }, NumericType { "ivec", "-2" },
                                    NumericType { "uvec", "2u" } }) {
        for (const int width : { 2, 3, 4 }) {
            const std::string vectorType = std::string (type.vectorPrefix) + std::to_string (width);
            const std::string scalar = type.scalar;
            CAPTURE (vectorType);
            const std::string fragment = compileFragment (
                "", "uniform " + vectorType + " value;\nvoid main() {\n"
                    + vectorType + " a = min(value, " + scalar + ");\n"
                    + vectorType + " b = max(value, " + scalar + ");\n"
                    + vectorType + " c = max(a, b);\n"
                    + vectorType + " d = min(b, a);\n"
                    + "gl_FragColor = vec4(float(c.x + d.x));\n}\n"
            );
            const auto translated = GLSLContext::get ().toGlsl (
                "#version 330\nvoid main() { gl_Position = vec4(0.0); }\n", fragment
            );
            CHECK_FALSE (translated.first.empty ());
            CHECK_FALSE (translated.second.empty ());
        }
    }
}

TEST_CASE ("min max retain constant expressions and fluid vector clamps", "[shader][minmax]") {
    const std::string fragment = compileFragment (
        "",
        "const int low = min(-3, -2);\n"
        "const int high = max(-3, -2);\n"
        "const uint ulow = min(4294967293u, 4294967294u);\n"
        "const uint uhigh = max(4294967293u, 4294967294u);\n"
        // Native built-ins retain integer precision and compile-time evaluation.
        "float signedCheck[low == -3 && high == -2 ? 1 : -1];\n"
        "float unsignedCheck[ulow == 4294967293u && uhigh == 4294967294u ? 1 : -1];\n"
        "const float mixed = max(1, 0.5);\n"
        "float mixedCheck[mixed == 1.0 ? 1 : -1];\n"
        "const vec2 constantVector = max(vec2(-2.0, 2.0), 1);\n"
        "float vectorCheck[constantVector == vec2(1.0, 2.0) ? 1 : -1];\n"
        "uniform vec2 velocity;\n"
        "void main() {\n"
        // The stock vorticity shader clamps velocity in this order.
        "vec2 clamped = min(max(velocity, -1000.0), 1000.0);\n"
        "vec2 implicit = min(max(velocity, -1000), 1000);\n"
        "gl_FragColor = vec4(clamped, implicit);\n"
        "}\n"
    );
    const auto translated = GLSLContext::get ().toGlsl (
        "#version 330\nvoid main() { gl_Position = vec4(0.0); }\n", fragment
    );
    CHECK_FALSE (translated.first.empty ());
    CHECK_FALSE (translated.second.empty ());
}

TEST_CASE ("literal-first max keeps bloom vectors and nested calls", "[shader][minmax]") {
    const std::string fragment = compileFragment (
        "", "uniform sampler2D image;\n"
        "// max(0, not a call\n/* max(0, also not a call */\n"
        "void main() {\n"
        "vec4 bloom = max(0, (texture(image, vec2(0.5)) - 1.0) / 2.0 + 1.0);\n"
        "vec4 nested = max(0, max(-1.0, bloom));\n"
        "gl_FragColor = max(1e-3, nested);\n}\n"
    );
    const auto translated = GLSLContext::get ().toGlsl (
        "#version 330\nvoid main() { gl_Position = vec4(0.0); }\n", fragment
    );
    CHECK_FALSE (translated.first.empty ());
    CHECK_FALSE (translated.second.empty ());
}

TEST_CASE ("max compatibility leaves authored macros and function ownership intact", "[shader][minmax]") {
    const std::string macro = compileFragment (
        "", "#define max(a,b) ((a) - (b))\n"
        "const int result = max(3, 2);\nfloat check[result == 1 ? 1 : -1];\n"
        "void main() { gl_FragColor = vec4(float(result)); }\n"
    );
    const auto translated = GLSLContext::get ().toGlsl (
        "#version 330\nvoid main() { gl_Position = vec4(0.0); }\n", macro
    );
    CHECK_FALSE (translated.second.empty ());
    const std::string comments = compileFragment (
        "", "// max(0, this is not code\n/* max(0, nor is this */\n"
        "float maximum(float x) { return x; }\n"
        "float max(float a, float b) { return a - b; }\n"
        "void main() { gl_FragColor = vec4(max(3.0, maximum(2.0))); }\n"
    );
    CHECK_FALSE (GLSLContext::get ().toGlsl (
        "#version 330\nvoid main() { gl_Position = vec4(0.0); }\n", comments
    ).second.empty ());
}
