#include "CFBO.h"
#include "WallpaperEngine/Logging/Log.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <ranges>
#include <sstream>
#include <tuple>
#include <vector>

using namespace WallpaperEngine::Render;

std::atomic<size_t> CFBO::s_liveCount { 0 };
std::atomic<size_t> CFBO::s_liveGpuBytes { 0 };

namespace {
struct LiveFBOInfo {
    std::string name;
    uint32_t width;
    uint32_t height;
    size_t bytes;
};

std::mutex s_liveFBOsMutex;
std::map<const CFBO*, LiveFBOInfo> s_liveFBOs;
}

CFBO::CFBO (
    std::string name, const TextureFormat format, const uint32_t flags, const float scale, uint32_t realWidth,
    uint32_t realHeight, uint32_t textureWidth, uint32_t textureHeight, bool withDepthBuffer, bool depthTexture
) : m_depthTexture (depthTexture), m_scale (scale), m_name (std::move (name)), m_format (format), m_flags (flags) {
    // NOTE: the framebuffer object itself is created lazily on first use (see
    // ensureFramebuffer): FBOs are not shared between GL contexts, so when this
    // constructor runs on the async switch worker only the shared objects
    // (texture, renderbuffer) can be created here
    // create the main texture
    glGenTextures (1, &this->m_texture);
    // bind the new texture to set settings on it
    glBindTexture (GL_TEXTURE_2D, this->m_texture);
    // Wallpaper Engine declares these render targets as ARGB8888. Keeping them at
    // eight bits per channel halves their color-storage footprint compared with
    // RGBA16F and avoids paying the HDR cost for every intermediate effect surface.
    if (this->m_depthTexture) {
	glTexImage2D (
	    GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, textureWidth, textureHeight, 0, GL_DEPTH_COMPONENT,
	    GL_UNSIGNED_INT, nullptr
	);
	// Stock scene shaders declare the shadow atlas as sampler2DComparison. Hardware
	// comparison also lets their 9-tap PCF path work without a custom sampler object.
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	const GLfloat borderDepth[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameterfv (GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderDepth);
    } else {
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, textureWidth, textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    // label stuff for debugging
#if !NDEBUG
    glObjectLabel (GL_TEXTURE, this->m_texture, -1, this->m_name.c_str ());
#endif /* DEBUG */
    // set filtering parameters, otherwise the texture is not rendered
    if (this->m_depthTexture) {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    } else if (flags & TextureFlags_ClampUVs) {
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
	// Layer composite targets get sampled onto grazing 3D geometry in the final pass
	// (e.g. the reflective water-road panels in 3708206626). A mip chain lets the
	// anisotropic filter above damp that minification instead of aliasing into rainbow
	// scanlines. Every other FBO is sampled ~1:1 and stays on plain GL_LINEAR.
	glTexParameteri (
	    GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, this->hasMipmaps () ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR
	);
    }

    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);

    // 3D scenes depth-test their models, so the scene framebuffer needs a depth attachment
    // (renderbuffers are shared objects, so this is safe on the worker context too)
    if (withDepthBuffer && !this->m_depthTexture) {
	glGenRenderbuffers (1, &this->m_depthbuffer);
	glBindRenderbuffer (GL_RENDERBUFFER, this->m_depthbuffer);
	glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, textureWidth, textureHeight);
    }

    this->m_resolution = { textureWidth, textureHeight, realWidth, realHeight };
    const size_t pixels = static_cast<size_t> (textureWidth) * textureHeight;
    this->m_approximateGpuBytes = pixels * 4 + (withDepthBuffer && !depthTexture ? pixels * 4 : 0);
    s_liveCount.fetch_add (1, std::memory_order_relaxed);
    s_liveGpuBytes.fetch_add (this->m_approximateGpuBytes, std::memory_order_relaxed);
    {
	std::lock_guard lock (s_liveFBOsMutex);
	s_liveFBOs.emplace (
	    this,
	    LiveFBOInfo {
		.name = this->m_name.empty () ? "<unnamed>" : this->m_name,
		.width = textureWidth,
		.height = textureHeight,
		.bytes = this->m_approximateGpuBytes,
	    }
	);
    }

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

    {
	std::lock_guard lock (s_liveFBOsMutex);
	s_liveFBOs.erase (this);
    }

    s_liveCount.fetch_sub (1, std::memory_order_relaxed);
    s_liveGpuBytes.fetch_sub (this->m_approximateGpuBytes, std::memory_order_relaxed);
}

std::string CFBO::getLiveDebugSummary (const size_t limit) {
    struct Group {
	std::string name;
	uint32_t width;
	uint32_t height;
	size_t count = 0;
	size_t bytes = 0;
    };

    std::map<std::tuple<std::string, uint32_t, uint32_t>, Group> grouped;
    {
	std::lock_guard lock (s_liveFBOsMutex);
	for (const auto& [instance, info] : s_liveFBOs) {
	    auto& group = grouped[std::make_tuple (info.name, info.width, info.height)];
	    group.name = info.name;
	    group.width = info.width;
	    group.height = info.height;
	    group.count++;
	    group.bytes += info.bytes;
	}
    }

    std::vector<Group> sorted;
    sorted.reserve (grouped.size ());
    for (auto& group : grouped | std::views::values) {
	sorted.push_back (std::move (group));
    }
    std::ranges::sort (sorted, [] (const Group& left, const Group& right) { return left.bytes > right.bytes; });

    std::ostringstream out;
    for (size_t index = 0; index < std::min (limit, sorted.size ()); index++) {
	if (index > 0) {
	    out << ';';
	}
	auto name = sorted[index].name;
	std::ranges::replace (name, ' ', '_');
	out << name << '@' << sorted[index].width << 'x' << sorted[index].height << 'x' << sorted[index].count << '='
	    << sorted[index].bytes;
    }
    return out.str ();
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

    glGenFramebuffers (1, &this->m_framebuffer);
    glBindFramebuffer (GL_FRAMEBUFFER, this->m_framebuffer);

    if (this->m_depthTexture) {
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, this->m_texture, 0);
	glDrawBuffer (GL_NONE);
	glReadBuffer (GL_NONE);
    } else {
	constexpr GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->m_texture, 0);
	glDrawBuffers (1, drawBuffers);
    }

    if (this->m_depthbuffer != GL_NONE) {
	glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, this->m_depthbuffer);
    }

    // ensure first framebuffer is okay
    if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
	sLog.exception ("Framebuffers are not properly set");
    }

    if (this->m_depthTexture) {
	// Unrendered atlas texels represent the far plane and therefore compare as lit.
	glClearDepth (1.0);
	glClear (GL_DEPTH_BUFFER_BIT);
    } else {
	// Layer framebuffers must start transparent. The scene clear color is often opaque,
	// and using it here makes empty layer areas render as solid rectangles.
	GLfloat previousClearColor[4] = {};
	glGetFloatv (GL_COLOR_CLEAR_VALUE, previousClearColor);
	glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
	glClear (GL_COLOR_BUFFER_BIT);
	glClearColor (previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);

	// A mipmapped min-filter is incomplete until the chain exists, so seed it from the
	// transparent level 0 now; per-frame renders refresh it via generateMipmaps().
	if (this->hasMipmaps ()) {
	    glBindTexture (GL_TEXTURE_2D, this->m_texture);
	    glGenerateMipmap (GL_TEXTURE_2D);
	    glBindTexture (GL_TEXTURE_2D, 0);
	}
    }

    glBindFramebuffer (GL_DRAW_FRAMEBUFFER, static_cast<GLuint> (previousDraw));
    glBindFramebuffer (GL_READ_FRAMEBUFFER, static_cast<GLuint> (previousRead));
}

bool CFBO::hasMipmaps () const {
    return !this->m_depthTexture && this->m_name.starts_with ("_rt_imageLayerComposite");
}

void CFBO::generateMipmaps () const {
    if (!this->hasMipmaps ()) {
	return;
    }

    glBindTexture (GL_TEXTURE_2D, this->m_texture);
    glGenerateMipmap (GL_TEXTURE_2D);
    glBindTexture (GL_TEXTURE_2D, 0);
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

size_t CFBO::getApproximateGpuBytes () const { return this->m_approximateGpuBytes; }

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

size_t CFBO::getLiveCount () { return s_liveCount.load (std::memory_order_relaxed); }

size_t CFBO::getLiveGpuBytes () { return s_liveGpuBytes.load (std::memory_order_relaxed); }
