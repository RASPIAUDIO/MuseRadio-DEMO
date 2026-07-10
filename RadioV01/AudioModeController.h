#pragma once

#include <Arduino.h>

enum class AudioMode : uint8_t {
  Radio = 0,
  AirPlay,
  Spotify,
  UsbAudio,
};

void audioModeBegin(uint32_t radioResumeGuardMs = 1500);
AudioMode audioModeActivate(AudioMode mode);
AudioMode audioModeDeactivate(AudioMode mode);
AudioMode audioModeCurrent();
bool audioModeAllows(AudioMode mode);
bool audioModeRadioReady();
const char* audioModeName(AudioMode mode);
