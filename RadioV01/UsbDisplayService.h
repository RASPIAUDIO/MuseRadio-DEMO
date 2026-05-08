#pragma once

#include <stdint.h>

class TFT_eSPI;

enum class UsbDisplayEvent : uint8_t {
  Ready,
  Active,
  Inactive,
  Frame,
  Dropped,
};

using UsbDisplayEventHandler = void (*)(UsbDisplayEvent event, uint32_t value, const char* text);

void usbDisplayBegin(TFT_eSPI& tft, UsbDisplayEventHandler handler);
bool usbDisplayActive();
const char* usbDisplayDeviceName();
