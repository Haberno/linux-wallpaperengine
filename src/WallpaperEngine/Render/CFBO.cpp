#include "CFBO.h"
#include "WallpaperEngine/BuildTiming.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine::Render;

CFBO::CFBO (
    std::string name, const TextureFormat format, const uint32_t flags, const float scale, uint32_t realWidth,
    uint32_t realHeight, uint32_t textureWidth, uint32_t textureHeight, bool withDepthBuffer
) : m_scale (scale), m_name (std::move (name)), m_format (format), m_flags (flags) {
    // ponytail: temporary switch-timing instrumentation, remove after measuring
    const WallpaperEngine::BuildTiming::Scope timing_ (WallpaperEngine::BuildTiming::fboUs);
    // NOTE: the framebuffer object itself is created lazily on first use (see
    // ensureFramebuffer): FBOs are not shared between GL contexts, so when this
    // constructor runs on the async switch worker only the shared objects
    // (texture, renderbuffer) can be created here
    // create the main texture
    glGenTextures (1, &this->m_texture);
    // bind the new texture to set settings on it
    glBindTexture (GL_TEXTURE_2D, this->m_texture);
    // give OpenGL an empty image. Use a 16-bit float internal format so the render
    // targets are HDR: shaders can emit values above 1.0 (e.g. a model's bright rim) and
    // the bloom pass can pick them up. The final blit to the 8-bit screen clamps for us.
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA16F, textureWidth, textureHeight, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    // label stuff for debugging
#if !NDEBUG
    glObjectLabel (GL_TEXTURE, this->m_texture, -1, this->m_name.c_str ());
#endif /* DEBUG */
    // set filtering parameters, otherwise the texture is not rendered
    if (flags & TextureFlags_ClampUVs) {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else if (flags & TextureFlags_ClampUVsBorder) {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    } else {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    if (flags & TextureFlags_NoInterpolation) {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    } else {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);

    // 3D scenes depth-test their models, so the scene framebuffer needs a depth attachment
    // (renderbuffers are shared objects, so this is safe on the worker context too)
    if (withDepthBuffer) {
	glGenRenderbuffers (1, &this->m_depthbuffer);
	glBindRenderbuffer (GL_RENDERBUFFER, this->m_depthbuffer);
	glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, textureWidth, textureHeight);
    }

    this->m_resolution = { textureWidth, textureHeight, realWidth, realHeight };

    // create the textureframe entries
    const auto frame = std::make_shared<Frame> ();

    frame->frameNumber = 0;
    frame->frametime = 0;
    frame->height1 = textureHeight;
    frame->height2 = realHeight;
    frame->width1 = textureWidth;
    frame->width2 = realWidth;
    frame->x = 0;
    frame->y = 0;

    this->m_frames.push_back (frame);
}

CFBO::~CFBO () {
    // free opengl texture and framebuffer
    glDeleteTextures (1, &this->m_texture);
    glDeleteFramebuffers (1, &this->m_framebuffer);

    if (this->m_depthbuffer != GL_NONE) {
	glDeleteRenderbuffers (1, &this->m_depthbuffer);
    }
}

void CFBO::ensureFramebuffer () const {
    if (this->m_framebuffer != GL_NONE) {
	return;
    }

    // this can trigger mid-frame (e.g. from getTextureID while another FBO is bound),
    // so the caller's framebuffer bindings must be preserved
    GLint previousDraw = GL_NONE;
    GLint previousRead = GL_NONE;
    glGetIntegerv (GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    glGetIntegerv (GL_READ_FRAMEBUFFER_BINDING, &previousRead);

    constexpr GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };

    glGenFramebuffers (1, &this->m_framebuffer);
    glBindFramebuffer (GL_FRAMEBUFFER, this->m_framebuffer);

    // set the texture as the colour attachmend #0
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->m_texture, 0);

    if (this->m_depthbuffer != GL_NONE) {
	glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, this->m_depthbuffer);
    }

    // finally set the list of draw buffers
    glDrawBuffers (1, drawBuffers);

    // ensure first framebuffer is okay
    if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
	sLog.exception ("Framebuffers are not properly set");
    }

    // Layer framebuffers must start transparent. The scene clear color is often opaque,
    // and using it here makes empty layer areas render as solid rectangles.
    GLfloat previousClearColor[4] = {};
    glGetFloatv (GL_COLOR_CLEAR_VALUE, previousClearColor);
    glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
    glClear (GL_COLOR_BUFFER_BIT);
    glClearColor (previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);

    glBindFramebuffer (GL_DRAW_FRAMEBUFFER, static_cast<GLuint> (previousDraw));
    glBindFramebuffer (GL_READ_FRAMEBUFFER, static_cast<GLuint> (previousRead));
}

const std::string& CFBO::getName () const { return this->m_name; }

const float& CFBO::getScale () const { return this->m_scale; }

TextureFormat CFBO::getFormat () const { return this->m_format; }

uint32_t CFBO::getFlags () const { return this->m_flags; }

GLuint CFBO::getFramebuffer () const {
    this->ensureFramebuffer ();
    return this->m_framebuffer;
}

GLuint CFBO::getDepthbuffer () const { return this->m_depthbuffer; }

GLuint CFBO::getTextureID (uint32_t imageIndex) const {
    // ensure the FBO exists (and its initial transparent clear ran) before anything
    // samples from this texture, otherwise the first frame reads undefined memory
    this->ensureFramebuffer ();
    return this->m_texture;
}

uint32_t CFBO::getTextureWidth (uint32_t imageIndex) const { return this->m_resolution.x; }

uint32_t CFBO::getTextureHeight (uint32_t imageIndex) const { return this->m_resolution.y; }

uint32_t CFBO::getRealWidth () const { return this->m_resolution.z; }

uint32_t CFBO::getRealHeight () const { return this->m_resolution.w; }

const std::vector<FrameSharedPtr>& CFBO::getFrames () const { return this->m_frames; }

const glm::vec4* CFBO::getResolution () const { return &this->m_resolution; }

bool CFBO::isAnimated () const { return false; }

uint32_t CFBO::getSpritesheetCols () const {
    return 0; // FBOs don't have spritesheets
}

uint32_t CFBO::getSpritesheetRows () const {
    return 0; // FBOs don't have spritesheets
}

uint32_t CFBO::getSpritesheetFrames () const {
    return 0; // FBOs don't have spritesheets
}

float CFBO::getSpritesheetDuration () const {
    return 0.0f; // FBOs don't have spritesheets
}

void CFBO::incrementUsageCount () const { }
void CFBO::decrementUsageCount () const { }
void CFBO::update () const { }
// FBOs are always ready
bool CFBO::isReady () const { return true; }