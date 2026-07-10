#include "AudioModeController.h"

#include <atomic>
#include "freertos/FreeRTOS.h"

namespace {
portMUX_TYPE s_modeMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> s_activeMask(0);
std::atomic<AudioMode> s_currentMode(AudioMode::Radio);
std::atomic<uint32_t> s_radioResumeAtMs(0);
uint32_t s_radioResumeGuardMs = 1500;

uint32_t modeBit(AudioMode mode)
{
  if (mode == AudioMode::Radio) return 0;
  return 1UL << static_cast<uint8_t>(mode);
}

AudioMode chooseMode(uint32_t activeMask)
{
  if (activeMask & modeBit(AudioMode::UsbAudio)) return AudioMode::UsbAudio;
  if (activeMask & modeBit(AudioMode::Spotify)) return AudioMode::Spotify;
  if (activeMask & modeBit(AudioMode::AirPlay)) return AudioMode::AirPlay;
  return AudioMode::Radio;
}

AudioMode updateModeLocked(uint32_t activeMask)
{
  const AudioMode previous = s_currentMode.load(std::memory_order_relaxed);
  const AudioMode next = chooseMode(activeMask);
  s_currentMode.store(next, std::memory_order_release);
  if (next != AudioMode::Radio) {
    s_radioResumeAtMs.store(0, std::memory_order_release);
  } else if (previous != AudioMode::Radio) {
    s_radioResumeAtMs.store(millis() + s_radioResumeGuardMs, std::memory_order_release);
  }
  return next;
}
} // namespace

void audioModeBegin(uint32_t radioResumeGuardMs)
{
  portENTER_CRITICAL(&s_modeMux);
  s_radioResumeGuardMs = radioResumeGuardMs;
  s_activeMask.store(0, std::memory_order_relaxed);
  s_currentMode.store(AudioMode::Radio, std::memory_order_relaxed);
  s_radioResumeAtMs.store(0, std::memory_order_relaxed);
  portEXIT_CRITICAL(&s_modeMux);
}

AudioMode audioModeActivate(AudioMode mode)
{
  if (mode == AudioMode::Radio) return audioModeCurrent();
  portENTER_CRITICAL(&s_modeMux);
  const uint32_t activeMask = s_activeMask.load(std::memory_order_relaxed) | modeBit(mode);
  s_activeMask.store(activeMask, std::memory_order_relaxed);
  const AudioMode current = updateModeLocked(activeMask);
  portEXIT_CRITICAL(&s_modeMux);
  return current;
}

AudioMode audioModeDeactivate(AudioMode mode)
{
  if (mode == AudioMode::Radio) return audioModeCurrent();
  portENTER_CRITICAL(&s_modeMux);
  const uint32_t activeMask = s_activeMask.load(std::memory_order_relaxed) & ~modeBit(mode);
  s_activeMask.store(activeMask, std::memory_order_relaxed);
  const AudioMode current = updateModeLocked(activeMask);
  portEXIT_CRITICAL(&s_modeMux);
  return current;
}

AudioMode audioModeCurrent()
{
  return s_currentMode.load(std::memory_order_acquire);
}

bool audioModeAllows(AudioMode mode)
{
  return audioModeCurrent() == mode;
}

bool audioModeRadioReady()
{
  if (!audioModeAllows(AudioMode::Radio)) return false;
  const uint32_t resumeAtMs = s_radioResumeAtMs.load(std::memory_order_acquire);
  return resumeAtMs == 0 || (int32_t)(millis() - resumeAtMs) >= 0;
}

const char* audioModeName(AudioMode mode)
{
  switch (mode) {
    case AudioMode::Radio: return "radio";
    case AudioMode::AirPlay: return "airplay";
    case AudioMode::Spotify: return "spotify";
    case AudioMode::UsbAudio: return "usb-audio";
    default: return "unknown";
  }
}
