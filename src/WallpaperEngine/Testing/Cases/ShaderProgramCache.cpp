#include "WallpaperEngine/Render/Shaders/ShaderProgramCache.h"
#include "WallpaperEngine/Render/Shaders/ShaderUnit.h"

#include <GLFW/glfw3.h>
#include <catch2/catch_test_macros.hpp>

using WallpaperEngine::Render::Shaders::ShaderProgramCache;

namespace {
struct GLContext {
    GLFWwindow* window = nullptr;
    GLContext () {
	REQUIRE (glfwInit ());
	glfwWindowHint (GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	window = glfwCreateWindow (32, 32, "Shader cache test", nullptr, nullptr);
	REQUIRE (window != nullptr);
	glfwMakeContextCurrent (window);
	glewExperimental = GL_TRUE;
	const GLenum result = glewInit ();
	// GLFW can select EGL/Wayland, where GL loads successfully without GLX.
	REQUIRE ((result == GLEW_OK || result == GLEW_ERROR_NO_GLX_DISPLAY));
	// GLEW probes removed legacy entry points on some core-profile drivers.
	while (glGetError () != GL_NO_ERROR) { }
    }
    ~GLContext () {
	glfwDestroyWindow (window);
	glfwTerminate ();
    }
};
const std::string vertex = "#version 330 core\nuniform float value;\n"
			   "void main() { gl_Position = vec4(value, 0, 0, 1); }\n";
const std::string fragment = "#version 330 core\nout vec4 color;\n"
			     "void main() { color = vec4(1, 0, 0, 1); }\n";

void checkLinked (GLuint program) {
    GLint linked = GL_FALSE;
    glGetProgramiv (program, GL_LINK_STATUS, &linked);
    REQUIRE (linked == GL_TRUE);
}
} // namespace

TEST_CASE ("Writable fragment copies retain interpolated input values", "[.][gl][varying]") {
    using namespace WallpaperEngine::Render::Shaders;
    GLContext context;
    ShaderProgramCache cache;
    auto container = std::make_unique<WallpaperEngine::FileSystem::Container> ();
    WallpaperEngine::Assets::AssetLocator assets (std::move (container));
    const ShaderConstantMap constants;
    const TextureMap textures;
    const std::string declarations
	= "varying vec2 v_Depth;\n#if WIDE\nvarying vec4 v_TexCoord;\n"
	  "#else\nvarying vec2 v_TexCoord;\n#endif\nvarying float v_ReadOnly;\n";
    GLuint vao = 0;
    glGenVertexArrays (1, &vao);
    glBindVertexArray (vao);
    glViewport (0, 0, 1, 1);
    for (const int wide : { 0, 1, 0 }) {
	const ComboMap combos = { { "WIDE", wide } };
	ShaderUnit vertexUnit (
	    GLSLContext::UnitType_Vertex, "writable.vert",
	    declarations
		+ "void main() {\n#if WIDE\nv_TexCoord = vec4(0.5);\n"
		  "#else\nv_TexCoord = vec2(0.5);\n#endif\n"
		  "v_Depth = vec2(0.125, 0.375); v_ReadOnly = 1.0;\n"
		  "gl_Position = vec4(float((gl_VertexID << 1) & 2) * 2.0 - 1.0, "
		  "float(gl_VertexID & 2) * 2.0 - 1.0, 0.0, 1.0);\n}\n",
	    assets, constants, textures, textures, combos, combos
	);
	ShaderUnit fragmentUnit (
	    GLSLContext::UnitType_Fragment, "writable.frag",
	    declarations + "void main() { v_Depth += vec2(0.125); v_TexCoord.xy *= 1.5;\n"
			   "gl_FragColor = vec4(v_Depth, v_TexCoord.x, v_ReadOnly); }\n",
	    assets, constants, textures, textures, combos, combos
	);
	vertexUnit.linkToUnit (&fragmentUnit);
	fragmentUnit.linkToUnit (&vertexUnit);
	const auto sources = GLSLContext::get ().toGlsl (vertexUnit.compile (), fragmentUnit.compile ());
	const GLuint program = cache.createProgram (sources.first, sources.second);
	checkLinked (program);
	glUseProgram (program);
	glDrawArrays (GL_TRIANGLES, 0, 3);
	unsigned char pixel[4] = {};
	glReadPixels (0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	const int expected[] = { 64, 128, 191, 255 };
	for (int i = 0; i < 4; ++i) {
	    CHECK (int (pixel[i]) >= expected[i] - 1);
	    CHECK (int (pixel[i]) <= expected[i] + 1);
	}
	glUseProgram (0);
	glDeleteProgram (program);
    }
    glDeleteVertexArrays (1, &vao);
    CHECK (glGetError () == GL_NO_ERROR);
}

// Explicit opt-in: build/output/tests '[gl]' requires a desktop graphics session.
TEST_CASE ("Cached programs keep independent uniforms and include both stages in their key", "[.][gl]") {
    GLContext context;
    ShaderProgramCache cache;
    const GLuint first = cache.createProgram (vertex, fragment);
    checkLinked (first);
    glUseProgram (first);
    const GLint firstValue = glGetUniformLocation (first, "value");
    REQUIRE (firstValue >= 0);
    glUniform1f (firstValue, 0.25f);

    const GLuint second = cache.createProgram (vertex, fragment);
    checkLinked (second);
    REQUIRE (second != first);
    const GLint secondValue = glGetUniformLocation (second, "value");
    float value = -1.0f;
    glGetUniformfv (second, secondValue, &value);
    CHECK (value == 0.0f);
    glUseProgram (second);
    glUniform1f (secondValue, 0.75f);
    glGetUniformfv (first, firstValue, &value);
    CHECK (value == 0.25f);
    glGetUniformfv (second, secondValue, &value);
    CHECK (value == 0.75f);

    const GLuint changedVertex = cache.createProgram (
	"#version 330 core\nuniform vec2 other;\nvoid main() { gl_Position = vec4(other, 0, 1); }\n", fragment
    );
    checkLinked (changedVertex);
    CHECK (glGetUniformLocation (changedVertex, "value") == -1);
    CHECK (glGetUniformLocation (changedVertex, "other") >= 0);
    const GLuint changedFragment = cache.createProgram (
	vertex, "#version 330 core\nuniform vec4 tint;\nout vec4 color;\nvoid main() { color = tint; }\n"
    );
    checkLinked (changedFragment);
    CHECK (glGetUniformLocation (changedFragment, "tint") >= 0);
    CHECK (glGetUniformLocation (first, "tint") == -1);
    glUseProgram (0);
    for (const GLuint program : { first, second, changedVertex, changedFragment }) {
	glDeleteProgram (program);
    }
    CHECK (glGetError () == GL_NO_ERROR);
}

TEST_CASE ("Shader cache falls back to source when binaries do not fit and recovers from errors", "[.][gl]") {
    GLContext context;
    ShaderProgramCache cache (1);
    for (int attempt = 0; attempt < 2; ++attempt) {
	const GLuint program = cache.createProgram (vertex, fragment);
	checkLinked (program);
	glDeleteProgram (program);
    }
    REQUIRE_THROWS (cache.createProgram (vertex, "invalid fragment shader"));
    REQUIRE_THROWS (cache.createProgram ("invalid vertex shader", fragment));
    // A stage pair that compiles individually but has incompatible varyings.
    REQUIRE_THROWS (cache.createProgram (
	"#version 330 core\nout vec3 uv;\nvoid main() { uv = vec3(1); gl_Position = vec4(1); }\n",
	"#version 330 core\nin vec2 uv;\nout vec4 color;\nvoid main() { color = vec4(uv,0,1); }\n"
    ));
    const GLuint recovered = cache.createProgram (vertex, fragment);
    checkLinked (recovered);
    glDeleteProgram (recovered);
    CHECK (glGetError () == GL_NO_ERROR);
}

TEST_CASE ("Live shader sharing isolates write layouts and releases programs with their materials", "[.][gl]") {
    GLContext context;
    ShaderProgramCache cache;
    const std::string screenVertex = "#version 330 core\nvoid main() {\n"
				     "vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
				     "gl_Position = vec4(p * 2.0 - 1.0, 0, 1); }\n";
    const std::string colorsFragment = "#version 330 core\nuniform vec4 colors[2];\n"
				       "out vec4 color;\nvoid main() { color = colors[0] + colors[1]; }\n";
    std::shared_ptr<ShaderProgramCache::SharingGroup> firstGroup, secondGroup;
    const GLuint first = cache.createProgram (screenVertex, colorsFragment, &firstGroup);
    const GLuint second = cache.createProgram (screenVertex, colorsFragment, &secondGroup);
    if (!firstGroup || !secondGroup) {
	glDeleteProgram (first);
	glDeleteProgram (second);
	SKIP ("Driver does not provide retrievable program binaries");
    }
    REQUIRE (firstGroup == secondGroup);
    const GLint colors = glGetUniformLocation (first, "colors");
    REQUIRE (colors >= 0);
    REQUIRE (glGetUniformLocation (second, "colors") == colors);
    const std::string fullLayout = "colors:" + std::to_string (colors) + ":vec4:2";
    auto red = ShaderProgramCache::shareProgram (first, firstGroup, fullLayout);
    auto green = ShaderProgramCache::shareProgram (second, secondGroup, fullLayout);
    REQUIRE (red);
    REQUIRE (green);
    CHECK (red->id == green->id);
    CHECK (red->id != first);
    CHECK (red->id != second);
    auto partial
	= ShaderProgramCache::shareProgram (second, secondGroup, "colors:" + std::to_string (colors) + ":vec4:1");
    REQUIRE (partial);
    CHECK (partial->id != red->id);

    GLuint vao = 0, framebuffer = 0, texture = 0;
    glGenVertexArrays (1, &vao);
    glBindVertexArray (vao);
    glGenFramebuffers (1, &framebuffer);
    glBindFramebuffer (GL_FRAMEBUFFER, framebuffer);
    glGenTextures (1, &texture);
    glBindTexture (GL_TEXTURE_2D, texture);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    REQUIRE (glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport (0, 0, 1, 1);
    const float redValues[] = { 0.5f, 0, 0, 0.5f, 0.5f, 0, 0, 0.5f };
    const float greenValues[] = { 0, 0.5f, 0, 0.5f, 0, 0.5f, 0, 0.5f };
    const float blueValues[] = { 0, 0, 1, 1 };
    auto draw = [&] (GLuint program, const float* values, int count, int channel) {
	glUseProgram (program);
	glUniform4fv (colors, count, values);
	glDrawArrays (GL_TRIANGLES, 0, 3);
	unsigned char pixel[4] = {};
	glReadPixels (0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	for (int i = 0; i < 4; ++i) {
	    CHECK (pixel[i] == (i == channel || i == 3 ? 255 : 0));
	}
    };
    draw (red->id, redValues, 2, 0);
    draw (green->id, greenValues, 2, 1);
    draw (partial->id, blueValues, 1, 2); // Its unwritten array tail must stay zero.
    draw (red->id, redValues, 2, 0);
    float privateValues[4] = { -1, -1, -1, -1 };
    glGetUniformfv (first, colors, privateValues);
    for (const float value : privateValues) {
	CHECK (value == 0);
    }

    // Neither the cache nor the sharing group pins a GL object after its users
    // leave. The group's stale weak entry must also be safe to reuse.
    glUseProgram (0);
    const GLuint sharedID = red->id;
    red.reset ();
    CHECK (glIsProgram (sharedID) == GL_TRUE);
    green.reset ();
    CHECK (glIsProgram (sharedID) == GL_FALSE);
    auto replacement = ShaderProgramCache::shareProgram (first, firstGroup, fullLayout);
    REQUIRE (replacement);
    checkLinked (replacement->id);
    replacement.reset ();
    partial.reset ();
    glDeleteProgram (first);
    glDeleteProgram (second);
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers (1, &framebuffer);
    glDeleteTextures (1, &texture);
    glBindVertexArray (0);
    glDeleteVertexArrays (1, &vao);

    ShaderProgramCache disabled (1);
    std::shared_ptr<ShaderProgramCache::SharingGroup> noGroup;
    const GLuint privateOnly = disabled.createProgram (screenVertex, colorsFragment, &noGroup);
    CHECK_FALSE (noGroup);
    CHECK_FALSE (ShaderProgramCache::shareProgram (privateOnly, noGroup, fullLayout));
    checkLinked (privateOnly);
    glDeleteProgram (privateOnly);
    CHECK (glGetError () == GL_NO_ERROR);
}
