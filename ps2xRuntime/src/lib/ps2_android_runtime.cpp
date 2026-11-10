#include "runtime/ps2_android_runtime.h"

#if defined(__ANDROID__)

#include "ps2_host_backend.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace
{
    constexpr uint16_t PAD_LEFT = 0x0080u;
    constexpr uint16_t PAD_DOWN = 0x0040u;
    constexpr uint16_t PAD_RIGHT = 0x0020u;
    constexpr uint16_t PAD_UP = 0x0010u;
    constexpr uint16_t PAD_START = 0x0008u;
    constexpr uint16_t PAD_SELECT = 0x0001u;
    constexpr uint16_t PAD_SQUARE = 0x8000u;
    constexpr uint16_t PAD_CROSS = 0x4000u;
    constexpr uint16_t PAD_CIRCLE = 0x2000u;
    constexpr uint16_t PAD_TRIANGLE = 0x1000u;
    constexpr uint16_t PAD_R1 = 0x0800u;
    constexpr uint16_t PAD_L1 = 0x0400u;
    constexpr uint16_t PAD_R2 = 0x0200u;
    constexpr uint16_t PAD_L2 = 0x0100u;

    std::atomic<uint16_t> g_touchButtons{0u};

    bool insideCircle(Vector2 point, float x, float y, float radius)
    {
        const float dx = point.x - x;
        const float dy = point.y - y;
        return (dx * dx + dy * dy) <= (radius * radius);
    }

    void addButtonIfTouched(uint16_t &buttons, Vector2 point,
                            float x, float y, float radius, uint16_t mask)
    {
        if (insideCircle(point, x, y, radius))
            buttons |= mask;
    }

    void drawTouchButton(float x, float y, float radius,
                         const char *label, uint16_t mask, uint16_t pressed)
    {
        const bool active = (pressed & mask) != 0u;
        const Color fill = active ? Color{210, 35, 35, 175} : Color{24, 24, 28, 105};
        const Color outline = active ? Color{255, 220, 150, 230} : Color{230, 230, 230, 125};
        DrawCircle(static_cast<int>(x), static_cast<int>(y), radius, fill);
        DrawCircleLines(static_cast<int>(x), static_cast<int>(y), radius, outline);
        const int fontSize = std::max(10, static_cast<int>(radius * 0.72f));
        const int textWidth = MeasureText(label, fontSize);
        DrawText(label, static_cast<int>(x) - textWidth / 2,
                 static_cast<int>(y) - fontSize / 2, fontSize, RAYWHITE);
    }
}

uint16_t ps2AndroidTouchButtons() noexcept
{
    return g_touchButtons.load(std::memory_order_acquire);
}

void ps2AndroidUpdateTouchState()
{
    if (!IsWindowReady())
    {
        g_touchButtons.store(0u, std::memory_order_release);
        return;
    }

    const int width = GetScreenWidth();
    const int height = GetScreenHeight();
    if (width <= 0 || height <= 0)
    {
        g_touchButtons.store(0u, std::memory_order_release);
        return;
    }

    const float h = static_cast<float>(height);
    const float dpadX = static_cast<float>(width) * 0.16f;
    const float dpadY = h * 0.72f;
    const float dpadRadius = h * 0.19f;
    const float faceRadius = h * 0.065f;
    const float faceX = static_cast<float>(width) * 0.83f;
    const float faceY = h * 0.69f;
    const float shoulderRadius = h * 0.055f;
    const float smallRadius = h * 0.045f;

    uint16_t pressed = 0u;
    const int touchCount = std::min(GetTouchPointCount(), 10);
    for (int i = 0; i < touchCount; ++i)
    {
        const Vector2 point = GetTouchPosition(i);
        const float dx = point.x - dpadX;
        const float dy = point.y - dpadY;
        if ((dx * dx + dy * dy) <= (dpadRadius * dpadRadius))
        {
            const float threshold = dpadRadius * 0.28f;
            if (dy <= -threshold) pressed |= PAD_UP;
            if (dy >= threshold) pressed |= PAD_DOWN;
            if (dx <= -threshold) pressed |= PAD_LEFT;
            if (dx >= threshold) pressed |= PAD_RIGHT;
        }

        addButtonIfTouched(pressed, point, faceX, faceY - faceRadius * 1.25f, faceRadius, PAD_TRIANGLE);
        addButtonIfTouched(pressed, point, faceX + faceRadius * 1.25f, faceY, faceRadius, PAD_CIRCLE);
        addButtonIfTouched(pressed, point, faceX, faceY + faceRadius * 1.25f, faceRadius, PAD_CROSS);
        addButtonIfTouched(pressed, point, faceX - faceRadius * 1.25f, faceY, faceRadius, PAD_SQUARE);

        addButtonIfTouched(pressed, point, static_cast<float>(width) * 0.15f,
                           h * 0.14f, shoulderRadius, PAD_L1);
        addButtonIfTouched(pressed, point, static_cast<float>(width) * 0.30f,
                           h * 0.14f, shoulderRadius, PAD_L2);
        addButtonIfTouched(pressed, point, static_cast<float>(width) * 0.70f,
                           h * 0.14f, shoulderRadius, PAD_R2);
        addButtonIfTouched(pressed, point, static_cast<float>(width) * 0.85f,
                           h * 0.14f, shoulderRadius, PAD_R1);

        addButtonIfTouched(pressed, point, static_cast<float>(width) * 0.56f,
                           h * 0.91f, smallRadius, PAD_SELECT);
        addButtonIfTouched(pressed, point, static_cast<float>(width) * 0.66f,
                           h * 0.91f, smallRadius, PAD_START);
    }

    g_touchButtons.store(pressed, std::memory_order_release);
}

void ps2AndroidDrawTouchControls()
{
    if (!IsWindowReady())
        return;

    const float width = static_cast<float>(GetScreenWidth());
    const float height = static_cast<float>(GetScreenHeight());
    if (width <= 0.0f || height <= 0.0f)
        return;

    const uint16_t pressed = ps2AndroidTouchButtons();
    const float dpadX = width * 0.16f;
    const float dpadY = height * 0.72f;
    const float dpadRadius = height * 0.19f;
    const float faceX = width * 0.83f;
    const float faceY = height * 0.69f;
    const float faceRadius = height * 0.065f;
    const float shoulderRadius = height * 0.055f;
    const float smallRadius = height * 0.045f;

    DrawCircle(static_cast<int>(dpadX), static_cast<int>(dpadY),
               dpadRadius, Color{24, 24, 28, 45});
    drawTouchButton(dpadX, dpadY - dpadRadius * 0.62f, dpadRadius * 0.34f, "▲", PAD_UP, pressed);
    drawTouchButton(dpadX, dpadY + dpadRadius * 0.62f, dpadRadius * 0.34f, "▼", PAD_DOWN, pressed);
    drawTouchButton(dpadX - dpadRadius * 0.62f, dpadY, dpadRadius * 0.34f, "◀", PAD_LEFT, pressed);
    drawTouchButton(dpadX + dpadRadius * 0.62f, dpadY, dpadRadius * 0.34f, "▶", PAD_RIGHT, pressed);

    drawTouchButton(faceX, faceY - faceRadius * 1.25f, faceRadius, "△", PAD_TRIANGLE, pressed);
    drawTouchButton(faceX + faceRadius * 1.25f, faceY, faceRadius, "○", PAD_CIRCLE, pressed);
    drawTouchButton(faceX, faceY + faceRadius * 1.25f, faceRadius, "X", PAD_CROSS, pressed);
    drawTouchButton(faceX - faceRadius * 1.25f, faceY, faceRadius, "□", PAD_SQUARE, pressed);

    drawTouchButton(width * 0.15f, height * 0.14f, shoulderRadius, "L1", PAD_L1, pressed);
    drawTouchButton(width * 0.30f, height * 0.14f, shoulderRadius, "L2", PAD_L2, pressed);
    drawTouchButton(width * 0.70f, height * 0.14f, shoulderRadius, "R2", PAD_R2, pressed);
    drawTouchButton(width * 0.85f, height * 0.14f, shoulderRadius, "R1", PAD_R1, pressed);
    drawTouchButton(width * 0.56f, height * 0.91f, smallRadius, "SEL", PAD_SELECT, pressed);
    drawTouchButton(width * 0.66f, height * 0.91f, smallRadius, "START", PAD_START, pressed);
}

#else

uint16_t ps2AndroidTouchButtons() noexcept { return 0u; }
void ps2AndroidUpdateTouchState() {}
void ps2AndroidDrawTouchControls() {}

#endif
