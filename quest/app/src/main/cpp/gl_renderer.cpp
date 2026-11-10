#include "gl_renderer.h"

#include "hud_layout.h"
#include "layers.h"

#include <GLES2/gl2ext.h>
#include <android/surface_texture_jni.h>
#include <android/log.h>

#include <cstring>

namespace imago::quest {

namespace {

constexpr const char* kTag = "imago-quest-gl";

// The picture's corner radius, as a fraction of its width (13 dp on a tablet's video card).
constexpr float kPictureCorner = 0.018f;

void logError(const char* what) { __android_log_print(ANDROID_LOG_ERROR, kTag, "%s", what); }

// Both textures hold sRGB-encoded pictures (the decoder's output, a Canvas); the swapchain is
// an sRGB framebuffer that encodes on write, so they are decoded to linear first, or the
// picture would be washed out.
constexpr const char* kPanelVertex = R"(#version 300 es
uniform mat4 uMvp;
uniform mat4 uTexMatrix;
uniform vec4 uUvRect; // the crop of the texture this quad shows: origin and size
in vec2 aPos;
in vec2 aUv;
out vec2 vUv;
out vec2 vQuad;
void main() {
    gl_Position = uMvp * vec4(aPos, 0.0, 1.0);
    vQuad = aUv;
    vUv = (uTexMatrix * vec4(uUvRect.xy + aUv * uUvRect.zw, 0.0, 1.0)).xy;
}
)";

constexpr const char* kPanelFragment = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
uniform samplerExternalOES uTex;
uniform int uOverlay;
uniform vec2 uCorner; // corner radius as a fraction of the quad's width and height; 0 = square
in vec2 vUv;
in vec2 vQuad;
out vec4 fragColor;
vec3 toLinear(vec3 c) {
    return pow(c, vec3(2.2));
}
void main() {
    vec4 c = texture(uTex, vUv);
    if (uOverlay == 1) {
        // Premultiplied alpha from the Canvas.
        fragColor = vec4(toLinear(c.rgb), c.a);
        return;
    }
    // The picture, with rounded corners: distance to the rounded rectangle in units of the
    // radius, antialiased over the last few percent.
    float a = 1.0;
    if (uCorner.x > 0.0) {
        vec2 q = (abs(vQuad - 0.5) - (0.5 - uCorner)) / uCorner;
        float d = length(max(q, 0.0));
        a = 1.0 - smoothstep(0.94, 1.0, d);
    }
    fragColor = vec4(toLinear(c.rgb) * a, a);
}
)";

constexpr const char* kLineVertex = R"(#version 300 es
uniform mat4 uMvp;
in vec3 aPos;
in float aAlpha;
out float vAlpha;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    vAlpha = aAlpha;
}
)";

constexpr const char* kLineFragment = R"(#version 300 es
precision mediump float;
uniform vec4 uColor;
in float vAlpha;
out vec4 fragColor;
void main() {
    fragColor = uColor * vAlpha; // premultiplied
}
)";

GLuint compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[512];
        glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
        __android_log_print(ANDROID_LOG_ERROR, kTag, "shader: %s", info);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint link(const char* vertex, const char* fragment) {
    const GLuint v = compile(GL_VERTEX_SHADER, vertex);
    const GLuint f = compile(GL_FRAGMENT_SHADER, fragment);
    if (v == 0 || f == 0) {
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[512];
        glGetProgramInfoLog(program, sizeof(info), nullptr, info);
        __android_log_print(ANDROID_LOG_ERROR, kTag, "program: %s", info);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace

GlRenderer::~GlRenderer() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, surface_, surface_, context_);
        for (auto& [tex, target] : targets_) {
            glDeleteFramebuffers(1, &target.fbo);
            glDeleteRenderbuffers(1, &target.depth);
        }
        if (videoSurface_ != nullptr) {
            ASurfaceTexture_detachFromGLContext(videoSurface_);
            ASurfaceTexture_release(videoSurface_);
        }
        if (overlaySurface_ != nullptr) {
            ASurfaceTexture_detachFromGLContext(overlaySurface_);
            ASurfaceTexture_release(overlaySurface_);
        }
        glDeleteProgram(panelProgram_);
        glDeleteProgram(lineProgram_);
        glDeleteBuffers(1, &quadVbo_);
        glDeleteBuffers(1, &lineVbo_);
        glDeleteVertexArrays(1, &vao_);
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
        }
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
        }
        eglTerminate(display_);
    }
}

bool GlRenderer::initEgl() {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
        logError("eglInitialize failed");
        return false;
    }
    // No window: the compositor's swapchain images are the render targets. A pbuffer only
    // serves to make the context current.
    const EGLint attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                 EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
                                 EGL_RED_SIZE,        8,
                                 EGL_GREEN_SIZE,      8,
                                 EGL_BLUE_SIZE,       8,
                                 EGL_ALPHA_SIZE,      8,
                                 EGL_DEPTH_SIZE,      0,
                                 EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(display_, attributes, &config_, 1, &count) || count == 0) {
        logError("no EGL config");
        return false;
    }
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttributes);
    if (context_ == EGL_NO_CONTEXT) {
        logError("eglCreateContext failed");
        return false;
    }
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    surface_ = eglCreatePbufferSurface(display_, config_, surfaceAttributes);
    if (surface_ == EGL_NO_SURFACE || !eglMakeCurrent(display_, surface_, surface_, context_)) {
        logError("eglMakeCurrent failed");
        return false;
    }
    return buildPrograms();
}

bool GlRenderer::buildPrograms() {
    panelProgram_ = link(kPanelVertex, kPanelFragment);
    lineProgram_ = link(kLineVertex, kLineFragment);
    if (panelProgram_ == 0 || lineProgram_ == 0) {
        return false;
    }
    panelMvp_ = glGetUniformLocation(panelProgram_, "uMvp");
    panelTexMatrix_ = glGetUniformLocation(panelProgram_, "uTexMatrix");
    panelSampler_ = glGetUniformLocation(panelProgram_, "uTex");
    panelOverlay_ = glGetUniformLocation(panelProgram_, "uOverlay");
    panelUvRect_ = glGetUniformLocation(panelProgram_, "uUvRect");
    panelCorner_ = glGetUniformLocation(panelProgram_, "uCorner");
    lineMvp_ = glGetUniformLocation(lineProgram_, "uMvp");
    lineColor_ = glGetUniformLocation(lineProgram_, "uColor");

    // A unit quad in the panel's plane, scaled per panel; the texture's (0, 0) is its
    // bottom-left in GL terms, which the surface texture matrix turns the right way up.
    static constexpr float kQuad[] = {
        -0.5f, -0.5f, 0, 0, //
        0.5f,  -0.5f, 1, 0, //
        -0.5f, 0.5f,  0, 1, //
        0.5f,  0.5f,  1, 1,
    };
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glGenBuffers(1, &lineVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, nullptr, GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
    return true;
}

bool GlRenderer::attachTextures(JNIEnv* env, jobject videoTexture, jobject overlayTexture) {
    glGenTextures(1, &videoTex_);
    glGenTextures(1, &overlayTex_);
    for (GLuint tex : {videoTex_, overlayTex_}) {
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    videoSurface_ = ASurfaceTexture_fromSurfaceTexture(env, videoTexture);
    overlaySurface_ = ASurfaceTexture_fromSurfaceTexture(env, overlayTexture);
    if (videoSurface_ == nullptr || overlaySurface_ == nullptr) {
        logError("ASurfaceTexture_fromSurfaceTexture failed");
        return false;
    }
    if (ASurfaceTexture_attachToGLContext(videoSurface_, videoTex_) != 0 ||
        ASurfaceTexture_attachToGLContext(overlaySurface_, overlayTex_) != 0) {
        logError("ASurfaceTexture_attachToGLContext failed");
        return false;
    }
    return true;
}

void GlRenderer::updateTextures() {
    // updateTexImage takes the newest buffer queued and drops the older ones: the panel
    // always shows the latest decoded picture, never a backlog.
    if (videoSurface_ != nullptr && ASurfaceTexture_updateTexImage(videoSurface_) == 0) {
        ASurfaceTexture_getTransformMatrix(videoSurface_, videoMatrix_);
        videoReady_ = ASurfaceTexture_getTimestamp(videoSurface_) != 0;
    }
    if (overlaySurface_ != nullptr && ASurfaceTexture_updateTexImage(overlaySurface_) == 0) {
        ASurfaceTexture_getTransformMatrix(overlaySurface_, overlayMatrix_);
        overlayReady_ = ASurfaceTexture_getTimestamp(overlaySurface_) != 0;
    }
}

GLuint GlRenderer::framebufferFor(uint32_t colorTexture, int width, int height) {
    auto it = targets_.find(colorTexture);
    if (it != targets_.end()) {
        return it->second.fbo;
    }
    Target t;
    t.width = width;
    t.height = height;
    glGenRenderbuffers(1, &t.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        logError("framebuffer incomplete");
    }
    targets_[colorTexture] = t;
    return t.fbo;
}

void GlRenderer::render(uint32_t colorTexture, int width, int height, const XrPosef& viewPose,
                        const XrFovf& fov, const Panel& panel, const Layer* layers, size_t count,
                        const Ray& ray, bool passthrough, float roomDim) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebufferFor(colorTexture, width, height));
    glViewport(0, 0, width, height);
    if (passthrough) {
        // The room shows through; black at this alpha darkens it so the picture stands out.
        glClearColor(0, 0, 0, roomDim);
    } else {
        glClearColor(0.02f, 0.02f, 0.025f, 1.0f);
    }
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // Everything is translucent somewhere (rounded corners, glass panels): drawn back to
    // front, blended premultiplied, without writing depth.
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    const Mat4 viewProjection = Mat4::projection(fov, 0.05f, 100.0f) * Mat4::viewFromPose(viewPose);
    const Mat4 panelMvp = viewProjection * Mat4::fromPose(panel.pose) * Mat4::scale(panel.width, panel.height, 1);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    const auto aPos = static_cast<GLuint>(glGetAttribLocation(panelProgram_, "aPos"));
    const auto aUv = static_cast<GLuint>(glGetAttribLocation(panelProgram_, "aUv"));
    const auto bindQuad = [&] {
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
        glEnableVertexAttribArray(aPos);
        glEnableVertexAttribArray(aUv);
        glVertexAttribPointer(aPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glVertexAttribPointer(aUv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<const void*>(2 * sizeof(float)));
    };
    glUseProgram(panelProgram_);
    glUniform1i(panelSampler_, 0);
    glActiveTexture(GL_TEXTURE0);

    // The picture, corners rounded (the same radius on screen in both directions): a dark
    // panel until the first frame, so the screen can be seen and placed.
    const float cornerX = kPictureCorner;
    const float cornerY = kPictureCorner * panel.width / panel.height;
    if (videoReady_) {
        bindQuad();
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, videoTex_);
        glUniformMatrix4fv(panelMvp_, 1, GL_FALSE, panelMvp.m);
        glUniformMatrix4fv(panelTexMatrix_, 1, GL_FALSE, videoMatrix_);
        glUniform4f(panelUvRect_, 0, 0, 1, 1);
        glUniform2f(panelCorner_, cornerX, cornerY);
        glUniform1i(panelOverlay_, 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    } else {
        glUseProgram(lineProgram_);
        glUniformMatrix4fv(lineMvp_, 1, GL_FALSE, panelMvp.m);
        glUniform4f(lineColor_, 0.012f, 0.014f, 0.02f, 0.92f);
        const auto lPos = static_cast<GLuint>(glGetAttribLocation(lineProgram_, "aPos"));
        const auto lAlpha = static_cast<GLuint>(glGetAttribLocation(lineProgram_, "aAlpha"));
        glEnableVertexAttribArray(lPos);
        glDisableVertexAttribArray(lAlpha);
        glVertexAttrib1f(lAlpha, 1.0f);
        glVertexAttribPointer(lPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(lPos);
        glUseProgram(panelProgram_);
    }

    // The interface: crops of the overlay texture, each at its own place.
    if (overlayReady_) {
        bindQuad();
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, overlayTex_);
        glUniformMatrix4fv(panelTexMatrix_, 1, GL_FALSE, overlayMatrix_);
        glUniform2f(panelCorner_, 0, 0);
        glUniform1i(panelOverlay_, 1);
        const float spanX = 1 + 2 * hud::kMarginSide;
        const float spanY = 1 + hud::kMarginTop + hud::kMarginBottom;
        for (size_t k = 0; k < count; ++k) {
            const Layer& l = layers[k];
            const Mat4 mvp = viewProjection * Mat4::fromPose(l.pose) * Mat4::scale(l.width, l.height, 1);
            glUniformMatrix4fv(panelMvp_, 1, GL_FALSE, mvp.m);
            // Texture coordinates run up, picture coordinates down.
            const float u0 = (l.x0 + hud::kMarginSide) / spanX;
            const float u1 = (l.x1 + hud::kMarginSide) / spanX;
            const float vBottom = 1 - (l.y1 + hud::kMarginTop) / spanY;
            const float vTop = 1 - (l.y0 + hud::kMarginTop) / spanY;
            glUniform4f(panelUvRect_, u0, vBottom, u1 - u0, vTop - vBottom);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }
    glDisableVertexAttribArray(aPos);
    glDisableVertexAttribArray(aUv);

    // The pointer ray: bright at the hand, fading towards where it lands.
    if (ray.valid) {
        const float line[8] = {ray.origin.x, ray.origin.y, ray.origin.z, 0.9f,
                               ray.end.x,    ray.end.y,    ray.end.z,    0.12f};
        glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(line), line);
        glUseProgram(lineProgram_);
        glUniformMatrix4fv(lineMvp_, 1, GL_FALSE, viewProjection.m);
        glUniform4f(lineColor_, 0.93f, 0.29f, 0.65f, 1.0f); // the accent
        const auto lPos = static_cast<GLuint>(glGetAttribLocation(lineProgram_, "aPos"));
        const auto lAlpha = static_cast<GLuint>(glGetAttribLocation(lineProgram_, "aAlpha"));
        glEnableVertexAttribArray(lPos);
        glEnableVertexAttribArray(lAlpha);
        glVertexAttribPointer(lPos, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glVertexAttribPointer(lAlpha, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<const void*>(3 * sizeof(float)));
        glLineWidth(3.0f);
        glDrawArrays(GL_LINES, 0, 2);
        glDisableVertexAttribArray(lPos);
        glDisableVertexAttribArray(lAlpha);
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace imago::quest
