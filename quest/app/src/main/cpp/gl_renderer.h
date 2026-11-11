#pragma once

#include "xr_math.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/surface_texture.h>
#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace imago::quest {

/// The virtual screen: a quad fixed in the reference space, showing the video (an external
/// texture the decoder writes) with the HUD overlay (another one, painted by Kotlin) on top.
struct Panel {
    XrPosef pose{{0, 0, 0, 1}, {0, 0, -2.2f}}; // centre; +Z of the pose faces the operator
    float width = 2.6f;
    float height = 2.6f * 9 / 16;
};

/// OpenGL ES 3 on EGL, on the XR thread only. Draws into the swapchain images OpenXR hands
/// out: the panel, and the pointer ray from the right controller.
class GlRenderer {
  public:
    GlRenderer() = default;
    ~GlRenderer();

    GlRenderer(const GlRenderer&) = delete;
    GlRenderer& operator=(const GlRenderer&) = delete;

    bool initEgl();
    EGLDisplay display() const { return display_; }
    EGLConfig config() const { return config_; }
    EGLContext context() const { return context_; }

    /// Needs the context current. The surface textures were created detached in Kotlin.
    bool attachTextures(JNIEnv* env, jobject videoTexture, jobject overlayTexture);
    /// Takes the newest picture of each texture, once per frame.
    void updateTextures();

    struct Ray {
        bool valid = false;
        XrVector3f origin{0, 0, 0};
        XrVector3f end{0, 0, 0};
    };

    /// Renders one eye into `colorTexture` (a swapchain image) of the given size: the
    /// picture, the interface layers back to front (`layers`, `count` of them) and the ray.
    /// With `passthrough` the background is the room, darkened by `roomDim` (0..1).
    void render(uint32_t colorTexture, int width, int height, const XrPosef& viewPose,
                const XrFovf& fov, const Panel& panel, const struct Layer* layers, size_t count,
                const Ray& ray, bool passthrough, float roomDim, float vignette);

  private:
    bool buildPrograms();
    GLuint framebufferFor(uint32_t colorTexture, int width, int height);

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE; // a tiny pbuffer, to make the context current

    GLuint panelProgram_ = 0;
    GLint panelMvp_ = -1, panelTexMatrix_ = -1, panelSampler_ = -1, panelOverlay_ = -1;
    GLint panelUvRect_ = -1, panelCorner_ = -1, panelVignette_ = -1;
    GLuint lineProgram_ = 0;
    GLint lineMvp_ = -1, lineColor_ = -1;
    GLuint quadVbo_ = 0;
    GLuint lineVbo_ = 0;
    GLuint vao_ = 0;

    using FramebufferTextureMultisampleFn = void (*)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);
    using RenderbufferStorageMultisampleFn = void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    FramebufferTextureMultisampleFn framebufferTextureMultisample_ = nullptr;
    RenderbufferStorageMultisampleFn renderbufferStorageMultisample_ = nullptr;

    GLuint videoTex_ = 0;
    GLuint overlayTex_ = 0;
    ASurfaceTexture* videoSurface_ = nullptr;
    ASurfaceTexture* overlaySurface_ = nullptr;
    float videoMatrix_[16]{};
    float overlayMatrix_[16]{};
    bool videoReady_ = false;
    bool overlayReady_ = false;

    struct Target {
        GLuint fbo = 0;
        GLuint depth = 0;
        int width = 0, height = 0;
    };
    std::unordered_map<uint32_t, Target> targets_; // by swapchain colour texture
};

} // namespace imago::quest
