#include "runtime/android_video_output.h"

#if defined(__ANDROID__)

#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{
    constexpr char kLogTag[] = "Manhunt2Video";

    constexpr char kVertexShader[] = R"(
#version 300 es
precision highp float;
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main()
{
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

    constexpr char kFragmentShader[] = R"(
#version 300 es
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D uFrameTexture;
out vec4 outColor;
void main()
{
    outColor = texture(uFrameTexture, vTexCoord);
}
)";

    void logInfo(const char *message)
    {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message);
    }

    void logError(const char *message)
    {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
    }

    void restoreCapability(GLenum capability, GLboolean wasEnabled)
    {
        if (wasEnabled == GL_TRUE)
            glEnable(capability);
        else
            glDisable(capability);
    }
}

AndroidVideoOutput::~AndroidVideoOutput()
{
    shutdown();
}

GLuint AndroidVideoOutput::compileShader(GLenum type, const char *source)
{
    const GLuint shader = glCreateShader(type);
    if (shader == 0)
        return 0;

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE)
    {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 1)
        {
            char logBuffer[2048]{};
            GLsizei written = 0;
            glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(logBuffer)),
                               &written, logBuffer);
            __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                                "Shader compilation failed: %s", logBuffer);
        }
        else
        {
            logError("Shader compilation failed without an info log.");
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

bool AndroidVideoOutput::bindCurrentContext()
{
    const EGLDisplay currentDisplay = eglGetCurrentDisplay();
    const EGLContext currentContext = eglGetCurrentContext();
    const EGLSurface currentSurface = eglGetCurrentSurface(EGL_DRAW);

    // raylib's Android platform owns NativeActivity, ANativeWindow and EGL.
    // During pause/resume there can be a short interval with no current surface.
    if (currentDisplay == EGL_NO_DISPLAY ||
        currentContext == EGL_NO_CONTEXT ||
        currentSurface == EGL_NO_SURFACE)
    {
        return false;
    }

    EGLint clientVersion = 0;
    if (eglQueryContext(currentDisplay, currentContext,
                        EGL_CONTEXT_CLIENT_VERSION, &clientVersion) != EGL_TRUE ||
        clientVersion < 3)
    {
        logError("The active NativeActivity EGL context is not OpenGL ES 3.0; using raylib fallback.");
        return false;
    }

    const bool contextChanged =
        currentDisplay != m_display || currentContext != m_context;

    m_display = currentDisplay;
    m_context = currentContext;
    m_surface = currentSurface;

    if (contextChanged)
    {
        // A replaced EGL context invalidates all previous GL names. Do not try
        // to delete them through the new context; the old context owns them.
        m_program = 0;
        m_texture = 0;
        m_vertexArray = 0;
        m_vertexBuffer = 0;
        m_samplerLocation = -1;
        m_textureWidth = 0;
        m_textureHeight = 0;
    }

    if (m_program == 0 || m_texture == 0 ||
        m_vertexArray == 0 || m_vertexBuffer == 0)
    {
        return createResources();
    }

    return true;
}

bool AndroidVideoOutput::createResources()
{
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
    if (vertexShader == 0)
        return false;

    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (fragmentShader == 0)
    {
        glDeleteShader(vertexShader);
        return false;
    }

    const GLuint program = glCreateProgram();
    if (program == 0)
    {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        if (logLength > 1)
        {
            char logBuffer[2048]{};
            GLsizei written = 0;
            glGetProgramInfoLog(program, static_cast<GLsizei>(sizeof(logBuffer)),
                                &written, logBuffer);
            __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                                "Program link failed: %s", logBuffer);
        }
        glDeleteProgram(program);
        return false;
    }

    m_program = program;
    m_samplerLocation = glGetUniformLocation(m_program, "uFrameTexture");
    if (m_samplerLocation < 0)
    {
        logError("Could not locate the frame texture sampler.");
        destroyCurrentResources();
        return false;
    }

    glGenTextures(1, &m_texture);
    if (m_texture == 0)
    {
        logError("Could not allocate the GS presentation texture.");
        destroyCurrentResources();
        return false;
    }

    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));

    static constexpr GLfloat vertices[] = {
        // position      // UV (top-down source image)
        -1.0f, -1.0f,     0.0f, 1.0f,
         1.0f, -1.0f,     1.0f, 1.0f,
        -1.0f,  1.0f,     0.0f, 0.0f,
         1.0f,  1.0f,     1.0f, 0.0f,
    };

    glGenVertexArrays(1, &m_vertexArray);
    glGenBuffers(1, &m_vertexBuffer);
    if (m_vertexArray == 0 || m_vertexBuffer == 0)
    {
        logError("Could not allocate the GLES3 vertex objects.");
        destroyCurrentResources();
        return false;
    }

    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);

    glBindVertexArray(m_vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          4 * static_cast<GLsizei>(sizeof(GLfloat)), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          4 * static_cast<GLsizei>(sizeof(GLfloat)),
                          reinterpret_cast<const void *>(2 * sizeof(GLfloat)));

    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));

    logInfo("GLES3 output attached to raylib's NativeActivity EGL context.");
    return true;
}

bool AndroidVideoOutput::initialize()
{
    if (!bindCurrentContext())
    {
        logError("GLES3 presenter initialization failed: no valid current EGL/GLES3 context.");
        return false;
    }

    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    const char *vendor = reinterpret_cast<const char *>(glGetString(GL_VENDOR));
    const char *eglVersion = eglQueryString(m_display, EGL_VERSION);
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "GPU_CONTEXT egl=%s gl=%s vendor=%s renderer=%s",
                        eglVersion ? eglVersion : "(unknown)",
                        version ? version : "(unknown)",
                        vendor ? vendor : "(unknown)",
                        renderer ? renderer : "(unknown)");
    logInfo("GLES3 presenter initialized on raylib NativeActivity context.");
    return true;
}

bool AndroidVideoOutput::uploadRgba8888(const uint8_t *pixels,
                                        uint32_t width,
                                        uint32_t height)
{
    if (pixels == nullptr || width == 0 || height == 0 ||
        width > 4096 || height > 4096)
    {
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(width) *
                              static_cast<size_t>(height);
    if (pixelCount > std::numeric_limits<size_t>::max() / 4u)
        return false;

    if (!bindCurrentContext())
        return false;

    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

    glBindTexture(GL_TEXTURE_2D, m_texture);
    if (width != m_textureWidth || height != m_textureHeight)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     static_cast<GLsizei>(width),
                     static_cast<GLsizei>(height), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        m_textureWidth = width;
        m_textureHeight = height;
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        static_cast<GLsizei>(width),
                        static_cast<GLsizei>(height),
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }

    const GLenum error = glGetError();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));

    static std::atomic<uint32_t> s_uploadCount{0u};
    const uint32_t uploadCount = s_uploadCount.fetch_add(1u, std::memory_order_relaxed);
    if (error != GL_NO_ERROR)
    {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "FRAME_UPLOAD_FAIL count=%u size=%ux%u gl_error=0x%04x",
                            uploadCount, width, height, static_cast<unsigned int>(error));
        return false;
    }
    if (uploadCount < 8u || (uploadCount % 120u) == 0u)
    {
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "FRAME_UPLOAD_OK count=%u size=%ux%u texture=%u",
                            uploadCount, width, height, m_texture);
    }

    return true;
}

bool AndroidVideoOutput::draw(uint32_t contentWidth,
                              uint32_t contentHeight,
                              uint32_t viewportWidth,
                              uint32_t viewportHeight)
{
    if (contentWidth == 0 || contentHeight == 0 ||
        viewportWidth == 0 || viewportHeight == 0 ||
        m_texture == 0 || m_textureWidth == 0 || m_textureHeight == 0 ||
        !bindCurrentContext())
    {
        return false;
    }

    const float scale = std::min(
        static_cast<float>(viewportWidth) / static_cast<float>(contentWidth),
        static_cast<float>(viewportHeight) / static_cast<float>(contentHeight));
    const GLsizei drawWidth = static_cast<GLsizei>(
        std::max(1.0f, static_cast<float>(contentWidth) * scale));
    const GLsizei drawHeight = static_cast<GLsizei>(
        std::max(1.0f, static_cast<float>(contentHeight) * scale));
    const GLint drawX = static_cast<GLint>((viewportWidth - drawWidth) / 2);
    const GLint topY = static_cast<GLint>((viewportHeight - drawHeight) / 2);
    const GLint drawY = static_cast<GLint>(viewportHeight) - topY - drawHeight;

    GLint previousProgram = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    GLint previousViewport[4]{};

    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    const GLboolean previousBlend = glIsEnabled(GL_BLEND);
    const GLboolean previousDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean previousCull = glIsEnabled(GL_CULL_FACE);
    const GLboolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    glViewport(drawX, drawY, drawWidth, drawHeight);
    glUseProgram(m_program);
    glUniform1i(m_samplerLocation, 0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glBindVertexArray(m_vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glUseProgram(static_cast<GLuint>(previousProgram));
    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);

    restoreCapability(GL_BLEND, previousBlend);
    restoreCapability(GL_DEPTH_TEST, previousDepth);
    restoreCapability(GL_CULL_FACE, previousCull);
    restoreCapability(GL_SCISSOR_TEST, previousScissor);

    static std::atomic<uint32_t> s_drawCount{0u};
    const uint32_t drawCount = s_drawCount.fetch_add(1u, std::memory_order_relaxed);
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "FRAME_DRAW_FAIL count=%u content=%ux%u viewport=%ux%u gl_error=0x%04x",
                            drawCount, contentWidth, contentHeight, viewportWidth, viewportHeight,
                            static_cast<unsigned int>(error));
        return false;
    }
    if (drawCount < 8u || (drawCount % 120u) == 0u)
    {
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "FRAME_DRAW_OK count=%u content=%ux%u viewport=%ux%u texture=%u",
                            drawCount, contentWidth, contentHeight, viewportWidth, viewportHeight, m_texture);
    }

    return true;
}

void AndroidVideoOutput::destroyCurrentResources() noexcept
{
    if (m_vertexBuffer != 0)
        glDeleteBuffers(1, &m_vertexBuffer);
    if (m_vertexArray != 0)
        glDeleteVertexArrays(1, &m_vertexArray);
    if (m_texture != 0)
        glDeleteTextures(1, &m_texture);
    if (m_program != 0)
        glDeleteProgram(m_program);

    m_vertexBuffer = 0;
    m_vertexArray = 0;
    m_texture = 0;
    m_program = 0;
    m_samplerLocation = -1;
    m_textureWidth = 0;
    m_textureHeight = 0;
}

void AndroidVideoOutput::shutdown() noexcept
{
    // Never make or destroy EGL objects here: raylib owns the NativeActivity
    // EGL context and surface. Delete GL resources only when that context is current.
    if (m_context != EGL_NO_CONTEXT &&
        eglGetCurrentContext() == m_context)
    {
        destroyCurrentResources();
    }
    else
    {
        // If Android already replaced/destroyed the context, its GL objects are
        // owned by that lost context and will be reclaimed by EGL.
        m_program = 0;
        m_texture = 0;
        m_vertexArray = 0;
        m_vertexBuffer = 0;
        m_samplerLocation = -1;
        m_textureWidth = 0;
        m_textureHeight = 0;
    }

    m_display = EGL_NO_DISPLAY;
    m_context = EGL_NO_CONTEXT;
    m_surface = EGL_NO_SURFACE;
}

#endif // __ANDROID__
