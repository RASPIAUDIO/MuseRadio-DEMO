#pragma once

#include <Arduino.h>
#include <Audio.h>

enum class AirPlayEvent : uint8_t {
  Ready,
  Active,
  Inactive,
  Metadata,
  Volume
};

using AirPlayEventHandler = void (*)(AirPlayEvent event, uint32_t value, const char* text);

void airPlayBegin(Audio& audio, AirPlayEventHandler handler);
bool airPlayActive();
void airPlaySetLocalVolume(uint8_t volume, uint8_t maxVolume);
void airPlayDisconnect();
const char* airPlayDeviceName();

