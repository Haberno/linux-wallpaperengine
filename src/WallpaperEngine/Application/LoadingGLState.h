#pragma once

#include <GL/glew.h>
#include <array>
#include <vector>

namespace WallpaperEngine::Application {
/** A loading checkpoint may run a complete outgoing frame. Restore context state
 * afterwards; checkpoints must also be between complete draws, since uniforms
 * and VAO/FBO contents belong to GL objects and are not context state. */
class LoadingGLState final {
public:
    LoadingGLState () {
        for (size_t i = 0; i < caps.size (); ++i) enabled[i] = glIsEnabled (caps[i]);
        for (size_t i = 0; i < integerNames.size (); ++i) glGetIntegerv (integerNames[i], &integers[i]);
        for (size_t i = 0; i < pixelNames.size (); ++i) glGetIntegerv (pixelNames[i], &pixels[i]);
        glGetIntegerv (GL_VIEWPORT, viewport.data ());
        glGetIntegerv (GL_SCISSOR_BOX, scissor.data ());
        glGetBooleanv (GL_COLOR_WRITEMASK, colorMask.data ());
        glGetBooleanv (GL_DEPTH_WRITEMASK, &depthMask);
        glGetFloatv (GL_COLOR_CLEAR_VALUE, clearColor.data ());
        glGetFloatv (GL_BLEND_COLOR, blendColor.data ());
        glGetDoublev (GL_DEPTH_RANGE, depthRange.data ());
        glGetDoublev (GL_DEPTH_CLEAR_VALUE, &clearDepth);
        glGetFloatv (GL_POLYGON_OFFSET_FACTOR, &polygonFactor);
        glGetFloatv (GL_POLYGON_OFFSET_UNITS, &polygonUnits);
        GLint count;
        glGetIntegerv (GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &count);
        textures.resize (count);
        for (GLint unit = 0; unit < count; ++unit) {
            glActiveTexture (GL_TEXTURE0 + unit);
            glGetIntegerv (GL_TEXTURE_BINDING_2D, &textures[unit].image);
            glGetIntegerv (GL_TEXTURE_BINDING_3D, &textures[unit].volume);
            glGetIntegerv (GL_TEXTURE_BINDING_CUBE_MAP, &textures[unit].cube);
            glGetIntegerv (GL_SAMPLER_BINDING, &textures[unit].sampler);
        }
        glActiveTexture (get (GL_ACTIVE_TEXTURE));
    }

    ~LoadingGLState () {
        for (size_t i = 0; i < caps.size (); ++i)
            if (enabled[i]) glEnable (caps[i]); else glDisable (caps[i]);
        glUseProgram (get (GL_CURRENT_PROGRAM));
        glBindVertexArray (get (GL_VERTEX_ARRAY_BINDING));
        glBindBuffer (GL_ARRAY_BUFFER, get (GL_ARRAY_BUFFER_BINDING));
        if (get (GL_VERTEX_ARRAY_BINDING))
            glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, get (GL_ELEMENT_ARRAY_BUFFER_BINDING));
        glBindBuffer (GL_PIXEL_PACK_BUFFER, get (GL_PIXEL_PACK_BUFFER_BINDING));
        glBindBuffer (GL_PIXEL_UNPACK_BUFFER, get (GL_PIXEL_UNPACK_BUFFER_BINDING));
        glBindRenderbuffer (GL_RENDERBUFFER, get (GL_RENDERBUFFER_BINDING));
        glBindFramebuffer (GL_DRAW_FRAMEBUFFER, get (GL_DRAW_FRAMEBUFFER_BINDING));
        glBindFramebuffer (GL_READ_FRAMEBUFFER, get (GL_READ_FRAMEBUFFER_BINDING));
        glViewport (viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor (scissor[0], scissor[1], scissor[2], scissor[3]);
        glColorMask (colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        glDepthMask (depthMask);
        glDepthFunc (get (GL_DEPTH_FUNC));
        glDepthRange (depthRange[0], depthRange[1]);
        glClearDepth (clearDepth);
        glClearColor (clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        glBlendColor (blendColor[0], blendColor[1], blendColor[2], blendColor[3]);
        glBlendFuncSeparate (get (GL_BLEND_SRC_RGB), get (GL_BLEND_DST_RGB),
                             get (GL_BLEND_SRC_ALPHA), get (GL_BLEND_DST_ALPHA));
        glBlendEquationSeparate (get (GL_BLEND_EQUATION_RGB), get (GL_BLEND_EQUATION_ALPHA));
        glCullFace (get (GL_CULL_FACE_MODE));
        glFrontFace (get (GL_FRONT_FACE));
        glPolygonOffset (polygonFactor, polygonUnits);
        for (size_t i = 0; i < pixelNames.size (); ++i) glPixelStorei (pixelNames[i], pixels[i]);
        for (size_t unit = 0; unit < textures.size (); ++unit) {
            glActiveTexture (GL_TEXTURE0 + unit);
            glBindTexture (GL_TEXTURE_2D, textures[unit].image);
            glBindTexture (GL_TEXTURE_3D, textures[unit].volume);
            glBindTexture (GL_TEXTURE_CUBE_MAP, textures[unit].cube);
            glBindSampler (unit, textures[unit].sampler);
        }
        glActiveTexture (get (GL_ACTIVE_TEXTURE));
    }

    void prepareOutgoingFrame (GLuint destinationFramebuffer, GLuint loadingVao) const {
        // A checkpoint can be inside shadow rendering or a layer's clipping scope.
        // Neither state may leak into the outgoing scene (including videos/web).
        // GLFW clears before invoking the scene. Never clear the candidate target
        // that was bound at the checkpoint.
        glBindFramebuffer (GL_FRAMEBUFFER, destinationFramebuffer);
        for (auto cap : caps) glDisable (cap);
        glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask (GL_TRUE);
        glDepthRange (0.0, 1.0);
        glClearDepth (1.0);
        glFrontFace (GL_CCW);
        glBindBuffer (GL_PIXEL_PACK_BUFFER, 0);
        glBindBuffer (GL_PIXEL_UNPACK_BUFFER, 0);
        for (auto name : pixelNames)
            glPixelStorei (name, name == GL_PACK_ALIGNMENT || name == GL_UNPACK_ALIGNMENT ? 4 : 0);
        for (size_t unit = 0; unit < textures.size (); ++unit) glBindSampler (unit, 0);
        glActiveTexture (GL_TEXTURE0);
        glBindVertexArray (loadingVao);
    }

    LoadingGLState (const LoadingGLState&) = delete;
    LoadingGLState& operator= (const LoadingGLState&) = delete;

private:
    GLint get (GLenum name) const {
        for (size_t i = 0; i < integerNames.size (); ++i)
            if (integerNames[i] == name) return integers[i];
        return 0;
    }
    static constexpr std::array<GLenum, 9> caps {
        GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_SCISSOR_TEST, GL_MULTISAMPLE,
        GL_SAMPLE_ALPHA_TO_COVERAGE, GL_POLYGON_OFFSET_FILL, GL_DEPTH_CLAMP, GL_FRAMEBUFFER_SRGB
    };
    static constexpr std::array<GLenum, 19> integerNames {
        GL_CURRENT_PROGRAM, GL_VERTEX_ARRAY_BINDING, GL_ARRAY_BUFFER_BINDING,
        GL_ELEMENT_ARRAY_BUFFER_BINDING, GL_PIXEL_PACK_BUFFER_BINDING, GL_PIXEL_UNPACK_BUFFER_BINDING,
        GL_RENDERBUFFER_BINDING, GL_DRAW_FRAMEBUFFER_BINDING, GL_READ_FRAMEBUFFER_BINDING,
        GL_DEPTH_FUNC, GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA,
        GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA, GL_CULL_FACE_MODE, GL_FRONT_FACE, GL_ACTIVE_TEXTURE
    };
    static constexpr std::array<GLenum, 12> pixelNames {
        GL_PACK_ALIGNMENT, GL_UNPACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_UNPACK_ROW_LENGTH,
        GL_PACK_SKIP_PIXELS, GL_UNPACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS, GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_IMAGES, GL_PACK_SWAP_BYTES, GL_UNPACK_SWAP_BYTES
    };
    std::array<GLboolean, caps.size ()> enabled {};
    std::array<GLint, integerNames.size ()> integers {};
    std::array<GLint, pixelNames.size ()> pixels {};
    std::array<GLint, 4> viewport {}, scissor {};
    std::array<GLboolean, 4> colorMask {};
    GLboolean depthMask {};
    std::array<GLfloat, 4> clearColor {}, blendColor {};
    std::array<GLdouble, 2> depthRange {};
    GLdouble clearDepth {};
    GLfloat polygonFactor {}, polygonUnits {};
    struct TextureBindings { GLint image {}, volume {}, cube {}, sampler {}; };
    std::vector<TextureBindings> textures;
};
}
