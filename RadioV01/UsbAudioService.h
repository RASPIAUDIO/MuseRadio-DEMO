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
void usbAudioStartUsbDebugOnly();
bool usbAudioActive();
uint32_t usbAudioBufferedMs();
bool usbAudioBufferHealthy();
const char* usbAudioDeviceName();
void usbAudioSetLocalVolume(uint8_t volume, uint8_t maxVolume);
void usbAudioSetLocalMute(bool muted);
void usbAudioSendHostVolumeDelta(int8_t steps);
void usbAudioSendHostMuteToggle();
void usbAudioSetMicNoiseGate(bool enabled);
void usbAudioSetMicNoiseGateThresholds(uint16_t openThreshold, uint16_t closeThreshold);
bool usbAudioMicNoiseGateEnabled();
