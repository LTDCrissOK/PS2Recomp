#pragma once

#include <string>

namespace GS::Android
{
// Reports whether the caller currently owns a usable OpenGL ES 3 context.
// This is a prerequisite for the Android GS hardware backend, not a renderer.
struct HardwareContextInfo
{
    bool current = false;
    bool gles3 = false;
    std::string version;
    std::string vendor;
    std::string renderer;
    std::string shadingLanguage;
    std::string diagnostic;
};

HardwareContextInfo InspectHardwareContext();
}
