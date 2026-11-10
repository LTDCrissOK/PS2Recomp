#pragma once

#include <cstdint>

// Android touch state is sampled on the render thread and consumed by the
// emulated pad backend through an atomic snapshot.
uint16_t ps2AndroidTouchButtons() noexcept;
void ps2AndroidUpdateTouchState();
void ps2AndroidDrawTouchControls();
