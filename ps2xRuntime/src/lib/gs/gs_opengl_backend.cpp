#include "runtime/gs/gs_opengl_backend.h"
#include "runtime/gs/gs_cpu_backend.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#endif

namespace
{
    bool parseOpenGLESVersion(const char *version, unsigned int &major, unsigned int &minor)
    {
        major = 0;
        minor = 0;

        if (!version || std::strncmp(version, "OpenGL ES", 9) != 0)
            return false;

        const char *number = version;
        while (*number && (*number < '0' || *number > '9'))
            ++number;

        if (!*number)
            return false;

        char *end = nullptr;
        const unsigned long parsedMajor = std::strtoul(number, &end, 10);
        if (end == number || *end != '.')
            return false;

        const char *minorStart = end + 1;
        const unsigned long parsedMinor = std::strtoul(minorStart, &end, 10);
        if (end == minorStart)
            return false;

        major = static_cast<unsigned int>(parsedMajor);
        minor = static_cast<unsigned int>(parsedMinor);
        return true;
    }
}

GSOpenGLBackend::GSOpenGLBackend()
    : m_reference(std::make_unique<GSCpuBackend>())
{
}

GSOpenGLBackend::~GSOpenGLBackend() = default;

bool GSOpenGLBackend::IsOpenGLES32Available() const noexcept
{
    return m_gles32Available;
}

const char *GSOpenGLBackend::GetOpenGLVersion() const noexcept
{
    return m_glVersion;
}

void GSOpenGLBackend::Initialize(uint8_t *vram, uint32_t vramSize)
{
    if (!m_reference)
        m_reference = std::make_unique<GSCpuBackend>();

    m_reference->Initialize(vram, vramSize);
    m_gles32Available = false;
    std::snprintf(m_glVersion, sizeof(m_glVersion), "%s", "OpenGL ES context not queried");

#if defined(__ANDROID__)
    // initialize() installs this backend only after raylib has created its EGL
    // window/context, so glGetString is valid here on the Android render thread.
    const GLubyte *version = glGetString(GL_VERSION);
    if (version)
    {
        std::snprintf(m_glVersion, sizeof(m_glVersion), "%s",
                      reinterpret_cast<const char *>(version));

        unsigned int major = 0;
        unsigned int minor = 0;
        if (parseOpenGLESVersion(m_glVersion, major, minor))
            m_gles32Available = (major > 3u) || (major == 3u && minor >= 2u);
    }
#endif

    std::cerr << "[GS/GLES] Context: " << m_glVersion
              << " | GLES 3.2: " << (m_gles32Available ? "available" : "not available")
              << " | GS rasterization: CPU reference path"
              << std::endl;
}

void GSOpenGLBackend::Reset()
{
    if (m_reference)
        m_reference->Reset();
}

void GSOpenGLBackend::Submit(const GSPrimitiveBatch &batch)
{
    if (m_reference)
        m_reference->Submit(batch);
}

void GSOpenGLBackend::LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
{
    if (m_reference)
        m_reference->LoadClut(tex0, texclut);
}

void GSOpenGLBackend::BeginTransfer(const GSTransferCommand &command)
{
    if (m_reference)
        m_reference->BeginTransfer(command);
}

void GSOpenGLBackend::UploadImage(const uint8_t *data, uint32_t sizeBytes)
{
    if (m_reference)
        m_reference->UploadImage(data, sizeBytes);
}

void GSOpenGLBackend::Flush()
{
    if (m_reference)
        m_reference->Flush();
}

void GSOpenGLBackend::TextureFlush()
{
    if (m_reference)
        m_reference->TextureFlush();
}

void GSOpenGLBackend::Sync(GSSyncReason reason)
{
    if (m_reference)
        m_reference->Sync(reason);
}

PresentationFrame GSOpenGLBackend::Present(const GSPresentationRequest &request)
{
    return m_reference ? m_reference->Present(request) : PresentationFrame{};
}

bool GSOpenGLBackend::ClearFramebuffer(const GSContext &context, uint32_t rgba)
{
    return m_reference && m_reference->ClearFramebuffer(context, rgba);
}

uint32_t GSOpenGLBackend::ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes)
{
    return m_reference ? m_reference->ConsumeLocalToHostBytes(dst, maxBytes) : 0u;
}

uint32_t GSOpenGLBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw,
                                  uint32_t x, uint32_t y) const
{
    return m_reference ? m_reference->ReadVram(psm, base, bw, x, y) : 0u;
}

void GSOpenGLBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw,
                                uint32_t x, uint32_t y, uint32_t value)
{
    if (m_reference)
        m_reference->WriteVram(psm, base, bw, x, y, value);
}

void GSOpenGLBackend::SnapshotVram(std::vector<uint8_t> &out) const
{
    if (m_reference)
        m_reference->SnapshotVram(out);
    else
        out.clear();
}

GSTransferSnapshot GSOpenGLBackend::GetTransferSnapshot() const
{
    return m_reference ? m_reference->GetTransferSnapshot() : GSTransferSnapshot{};
}
