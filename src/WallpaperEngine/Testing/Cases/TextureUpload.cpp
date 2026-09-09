#include "WallpaperEngine/Data/Assets/Texture.h"
#include "WallpaperEngine/Render/CFBO.h"
#include "WallpaperEngine/Render/CTexture.h"
#include "WallpaperEngine/Render/Shaders/ShaderProgramCache.h"
#include "WallpaperEngine/Render/TextureCache.h"

#include <GLFW/glfw3.h>
#include <array>

#ifdef CHECK
#undef CHECK
#endif
#include <catch2/catch_test_macros.hpp>

using WallpaperEngine::Data::Assets::TextureFlags_ClampUVs;
using WallpaperEngine::Data::Assets::TextureFormat_ARGB8888;
using WallpaperEngine::Data::Assets::TextureFormat_DXT1;
using WallpaperEngine::Data::Assets::TextureFormat_DXT5;
using WallpaperEngine::Data::Assets::TextureFormat_RG88;
using WallpaperEngine::Render::CFBO;
using WallpaperEngine::Render::CTexture;
using WallpaperEngine::Render::TextureCache;
using WallpaperEngine::Render::Shaders::ShaderProgramCache;

namespace {
struct TextureGLContext {
    GLFWwindow* window = nullptr;
    TextureGLContext () {
	REQUIRE (glfwInit ());
	glfwWindowHint (GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	window = glfwCreateWindow (32, 32, "Framebuffer texture test", nullptr, nullptr);
	REQUIRE (window != nullptr);
	glfwMakeContextCurrent (window);
	glewExperimental = GL_TRUE;
	const GLenum result = glewInit ();
	REQUIRE ((result == GLEW_OK || result == GLEW_ERROR_NO_GLX_DISPLAY));
	while (glGetError () != GL_NO_ERROR) { }
    }
    ~TextureGLContext () {
	glfwDestroyWindow (window);
	glfwTerminate ();
    }
};
} // namespace

TEST_CASE ("TEX payload layout is detected independently from its authored format", "[texture]") {
    SECTION ("DXT5 block payload") {
	CHECK (CTexture::isBlockCompressedPayload (TextureFormat_DXT5, 512, 512, 512 * 512));
    }

    SECTION ("DXT5 expanded RGBA payload") {
	CHECK_FALSE (CTexture::isBlockCompressedPayload (TextureFormat_DXT5, 512, 512, 512 * 512 * 4));
    }

    SECTION ("DXT1 block payload rounds dimensions to complete blocks") {
	CHECK (CTexture::isBlockCompressedPayload (TextureFormat_DXT1, 5, 7, 2 * 2 * 8));
    }

    SECTION ("uncompressed authored formats never use block upload") {
	CHECK_FALSE (CTexture::isBlockCompressedPayload (TextureFormat_ARGB8888, 512, 512, 512 * 512 * 4));
	CHECK_FALSE (CTexture::isBlockCompressedPayload (TextureFormat_RG88, 512, 512, 512 * 512 * 2));
    }
}

TEST_CASE ("only unscoped runtime texture aliases are pinned", "[texture][cache]") {
    CHECK (TextureCache::isPinnedRuntimeTextureKey ("$mediaThumbnail"));
    CHECK (TextureCache::isPinnedRuntimeTextureKey ("$mediaPreviousThumbnail"));

    const std::string authoredKey
	= "$mediaThumbnail=$mediaThumbnail;/=/workshop/431960/2297432332;" + std::string (1, '\x1f')
	+ "materials/background.tex";
    CHECK_FALSE (TextureCache::isPinnedRuntimeTextureKey (authoredKey));
    CHECK_FALSE (TextureCache::isPinnedRuntimeTextureKey ("materials/background.tex"));
}

TEST_CASE ("only the dedicated reflection framebuffer generates mipmaps", "[texture][fbo]") {
    CHECK (CFBO::targetUsesMipmaps ("_rt_MipMappedFrameBuffer"));
    CHECK_FALSE (CFBO::targetUsesMipmaps ("_rt_FullFrameBuffer"));
    CHECK_FALSE (CFBO::targetUsesMipmaps ("_rt_imageLayerComposite_206_a"));
    CHECK_FALSE (CFBO::targetUsesMipmaps ("_rt_imageLayerComposite_206_b"));
}

// Explicit opt-in, like the shader cache GPU tests: requires a graphics session.
TEST_CASE ("zero-sized framebuffer textures remain complete and sampleable", "[.][gl][texture][fbo]") {
    TextureGLContext context;
    ShaderProgramCache cache;
    const std::string vertex = "#version 330 core\nvoid main() {\n"
			       "vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
			       "gl_Position = vec4(p * 2.0 - 1.0, 0, 1); }\n";
    const ShaderProgramCache::SharedProgram colorProgram (cache.createProgram (
	vertex,
	"#version 330 core\nuniform sampler2D image;\nout vec4 color;\n"
	"void main() { color = texture(image, vec2((gl_FragCoord.x - 0.5) * 0.5)); }\n"
    ));
    const ShaderProgramCache::SharedProgram depthProgram (cache.createProgram (
	vertex,
	"#version 330 core\nuniform sampler2DShadow image;\nout vec4 color;\n"
	"void main() { color = vec4(texture(image, vec3(vec2((gl_FragCoord.x - 0.5) * 0.5), 0.5))); }\n"
    ));
    CFBO output ("fbo_size_test_output", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0f, 3, 1, 3, 1);
    GLuint vao = 0;
    glGenVertexArrays (1, &vao);
    glBindVertexArray (vao);
    glViewport (0, 0, 3, 1);

    struct Dimensions {
	uint32_t realWidth;
	uint32_t realHeight;
	uint32_t width;
	uint32_t height;
	uint32_t allocatedWidth;
	uint32_t allocatedHeight;
    };
    const Dimensions dimensions[] = {
	{ 64, 0, 64, 0, 64, 1 },
	{ 0, 64, 0, 64, 1, 64 },
	{ 0, 0, 0, 0, 1, 1 },
	// Ordinary padded textures must retain both their storage and logical sizes.
	{ 32, 16, 64, 32, 64, 32 },
    };
    enum class Attachment { Color, ColorAndDepth, DepthTexture };
    for (const Dimensions size : dimensions) {
	for (const Attachment attachment : { Attachment::Color, Attachment::ColorAndDepth, Attachment::DepthTexture }) {
	    const bool depthTexture = attachment == Attachment::DepthTexture;
	    const bool withDepth = attachment == Attachment::ColorAndDepth;
	    CAPTURE (size.width, size.height, withDepth, depthTexture);
	    CFBO source (
		"fbo_size_test_input", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0f, size.realWidth,
		size.realHeight, size.width, size.height, withDepth, depthTexture
	    );
	    CHECK (source.getTextureWidth (0) == size.allocatedWidth);
	    CHECK (source.getTextureHeight (0) == size.allocatedHeight);
	    CHECK (source.getRealWidth () == size.realWidth);
	    CHECK (source.getRealHeight () == size.realHeight);
	    REQUIRE (source.getFrames ().size () == 1);
	    CHECK (source.getFrames ()[0]->width1 == size.allocatedWidth);
	    CHECK (source.getFrames ()[0]->height1 == size.allocatedHeight);
	    CHECK (source.getFrames ()[0]->width2 == size.realWidth);
	    CHECK (source.getFrames ()[0]->height2 == size.realHeight);

	    // getTextureID must lazily create and initialize a usable attachment even
	    // when the authored dimensions describe an empty layer.
	    const GLuint texture = source.getTextureID (0);
	    glBindTexture (GL_TEXTURE_2D, texture);
	    GLint width = 0, height = 0;
	    glGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
	    glGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
	    CHECK (width == static_cast<GLint> (size.allocatedWidth));
	    CHECK (height == static_cast<GLint> (size.allocatedHeight));
	    glBindFramebuffer (GL_FRAMEBUFFER, source.getFramebuffer ());
	    REQUIRE (glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
	    if (withDepth) {
		REQUIRE (source.getDepthbuffer () != GL_NONE);
		glBindRenderbuffer (GL_RENDERBUFFER, source.getDepthbuffer ());
		glGetRenderbufferParameteriv (GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
		glGetRenderbufferParameteriv (GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
		CHECK (width == static_cast<GLint> (size.allocatedWidth));
		CHECK (height == static_cast<GLint> (size.allocatedHeight));
	    }

	    glBindFramebuffer (GL_FRAMEBUFFER, output.getFramebuffer ());
	    glClearColor (1, 0, 1, 1);
	    glClear (GL_COLOR_BUFFER_BIT);
	    const GLuint program = depthTexture ? depthProgram.id : colorProgram.id;
	    glUseProgram (program);
	    const GLint sampler = glGetUniformLocation (program, "image");
	    REQUIRE (sampler >= 0);
	    glUniform1i (sampler, 0);
	    glActiveTexture (GL_TEXTURE0);
	    glBindTexture (GL_TEXTURE_2D, texture);
	    glDrawArrays (GL_TRIANGLES, 0, 3);
	    std::array<unsigned char, 12> pixels {};
	    glReadPixels (0, 0, 3, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data ());
	    // Sample both edges and the center. A missing draw leaves magenta; an
	    // incomplete color texture returns opaque black, which also fails.
	    for (const unsigned char channel : pixels) {
		CHECK (channel == (depthTexture ? 255 : 0));
	    }
	    CHECK (glGetError () == GL_NO_ERROR);
	}
    }
    glUseProgram (0);
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    glBindVertexArray (0);
    glDeleteVertexArrays (1, &vao);
}
