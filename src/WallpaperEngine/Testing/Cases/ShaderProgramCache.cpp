#include "WallpaperEngine/Render/Shaders/ShaderProgramCache.h"

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
