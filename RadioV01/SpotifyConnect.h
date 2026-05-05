#pragma once

#include <Arduino.h>
#include <Audio.h>

enum class SpotifyConnectEvent : uint8_t {
  Ready,
  Active,
  Inactive,
  Paused,
  Volume,
  Track
};

using SpotifyConnectEventHandler = void (*)(SpotifyConnectEvent event, uint32_t value, const char* text);

void spotifyConnectBegin(Audio& audio, SpotifyConnectEventHandler handler);
bool spotifyConnectActive();
void spotifyConnectSetLocalVolume(uint8_t volume, uint8_t maxVolume);
const char* spotifyConnectDeviceName();
