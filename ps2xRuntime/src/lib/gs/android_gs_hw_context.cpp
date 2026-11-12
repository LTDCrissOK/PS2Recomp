#include "runtime/gs/android_gs_hw_context.h"

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#endif

namespace GS::Android
{
HardwareContextInfo InspectHardwareContext()
{
    HardwareContextInfo info{};
#if defined(__ANDROID__)
    const EGLContext context = eglGetCurrentContext();
    const EGLDisplay display = eglGetCurrentDisplay();
    info.current = context != EGL_NO_CONTEXT && display != EGL_NO_DISPLAY;
    if (!info.current)
    {
        info.diagnostic = "No current EGL context on the calling thread";
        return info;
    }

    const auto readString = [](GLenum name) -> std::string
    {
        const GLubyte *value = glGetString(name);
        return value ? reinterpret_cast<const char *>(value) : std::string{};
    };

    info.version = readString(GL_VERSION);
    info.vendor = readString(GL_VENDOR);
    info.renderer = readString(GL_RENDERER);
    info.shadingLanguage = readString(GL_SHADING_LANGUAGE_VERSION);
    info.gles3 = info.version.find("OpenGL ES 3.") != std::string::npos ||
                 info.version.find("OpenGL ES 3 ") != std::string::npos;
    info.diagnostic = info.gles3 ? "Current GLES 3 context detected"
                                 : "Current context is not OpenGL ES 3";
#else
    info.diagnostic = "Android GS hardware context probe is only available on Android";
#endif
    return info;
}
}
