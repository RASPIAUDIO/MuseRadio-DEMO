#pragma once

#include <Arduino.h>
#include <Audio.h>

enum class UsbAudioEvent : uint8_t {
  Ready,
  Active,
  Inactive,
  Volume,
  Mute
};

using UsbAudioEventHandler = void (*)(UsbAudioEvent event, uint32_t value, const char* text);

void usbAudioBegin(Audio& audio, UsbAudioEventHandler handler);
bool usbAudioActive();
uint32_t usbAudioBufferedMs();
bool usbAudioBufferHealthy();
const char* usbAudioDeviceName();
