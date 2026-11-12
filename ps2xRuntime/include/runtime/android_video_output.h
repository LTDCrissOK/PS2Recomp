#pragma once

#if defined(__ANDROID__)

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdint>

class AndroidVideoOutput final
{
public:
    AndroidVideoOutput() = default;
    ~AndroidVideoOutput();

    AndroidVideoOutput(const AndroidVideoOutput &) = delete;
    AndroidVideoOutput &operator=(const AndroidVideoOutput &) = delete;

    // Attaches to the EGL context already created and owned by raylib's
    // NativeActivity backend. This class never creates or destroys a surface.
    bool initialize();

    // Uploads a tightly packed, top-to-bottom RGBA8888 GS presentation frame.
    bool uploadRgba8888(const uint8_t *pixels, uint32_t width, uint32_t height);

    // Draws the latest frame into the current raylib window, preserving aspect.
    // Call between BeginDrawing()/EndDrawing() after ClearBackground().
    bool draw(uint32_t contentWidth,
              uint32_t contentHeight,
              uint32_t viewportWidth,
              uint32_t viewportHeight);

    // Deletes only this module's GL resources. EGL remains owned by raylib.
    void shutdown() noexcept;

    bool isInitialized() const noexcept { return m_program != 0 && m_texture != 0; }

private:
    bool bindCurrentContext();
    bool createResources();
    void destroyCurrentResources() noexcept;

    static GLuint compileShader(GLenum type, const char *source);

    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_surface = EGL_NO_SURFACE;

    GLuint m_program = 0;
    GLuint m_texture = 0;
    GLuint m_vertexArray = 0;
    GLuint m_vertexBuffer = 0;
    GLint m_samplerLocation = -1;
    uint32_t m_textureWidth = 0;
    uint32_t m_textureHeight = 0;
};

#endif // __ANDROID__
