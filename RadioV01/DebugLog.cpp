#include "DebugLog.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#if ENABLE_USB_DEBUG_CDC
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "usb_device_uac.h"
#endif

namespace {

#if ENABLE_USB_DEBUG_CDC
constexpr size_t LOG_RING_SIZE = 8192;
uint8_t s_ring[LOG_RING_SIZE];
size_t s_head = 0;
size_t s_tail = 0;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

size_t ringUsedLocked()
{
  return (s_head >= s_tail) ? (s_head - s_tail) : (LOG_RING_SIZE - s_tail + s_head);
}

size_t ringFreeLocked()
{
  return LOG_RING_SIZE - ringUsedLocked() - 1;
}

void pushByteLocked(uint8_t value)
{
  if (ringFreeLocked() == 0) {
    s_tail = (s_tail + 1) % LOG_RING_SIZE;
  }
  s_ring[s_head] = value;
  s_head = (s_head + 1) % LOG_RING_SIZE;
}

void pushLog(const char* data, size_t len)
{
  if (!data || len == 0) return;
  portENTER_CRITICAL(&s_mux);
  for (size_t i = 0; i < len; i++) {
    pushByteLocked((uint8_t)data[i]);
  }
  portEXIT_CRITICAL(&s_mux);
}

size_t popChunk(uint8_t* out, size_t maxLen)
{
  if (!out || maxLen == 0) return 0;
  portENTER_CRITICAL(&s_mux);
  size_t count = 0;
  while (s_tail != s_head && count < maxLen) {
    out[count++] = s_ring[s_tail];
    s_tail = (s_tail + 1) % LOG_RING_SIZE;
  }
  portEXIT_CRITICAL(&s_mux);
  return count;
}

void restoreChunk(const uint8_t* data, size_t len)
{
  if (!data || len == 0) return;
  portENTER_CRITICAL(&s_mux);
  for (size_t i = len; i > 0; i--) {
    s_tail = (s_tail + LOG_RING_SIZE - 1) % LOG_RING_SIZE;
    s_ring[s_tail] = data[i - 1];
  }
  portEXIT_CRITICAL(&s_mux);
}
#endif

} // namespace

int debugPrintf(const char* fmt, ...)
{
  if (!fmt) return 0;

  va_list stdoutArgs;
  va_start(stdoutArgs, fmt);
  const int rc = vprintf(fmt, stdoutArgs);
  va_end(stdoutArgs);

#if ENABLE_USB_DEBUG_CDC
  char buffer[384];
  va_list formatArgs;
  va_start(formatArgs, fmt);
  const int len = vsnprintf(buffer, sizeof(buffer), fmt, formatArgs);
  va_end(formatArgs);

  if (len > 0) {
    const size_t used = (size_t)((len < (int)sizeof(buffer)) ? len : (int)sizeof(buffer) - 1);
    pushLog(buffer, used);
    debugLogService();
  }
#endif

  return rc;
}

void debugLogService()
{
#if ENABLE_USB_DEBUG_CDC
  if (!uac_device_debug_cdc_connected()) return;

  uint8_t chunk[128];
  while (true) {
    const size_t count = popChunk(chunk, sizeof(chunk));
    if (count == 0) return;

    size_t written = 0;
    const esp_err_t err = uac_device_debug_cdc_write(chunk, count, &written);
    if (err != ESP_OK || written == 0) {
      restoreChunk(chunk, count);
      return;
    }
    if (written < count) {
      restoreChunk(chunk + written, count - written);
      return;
    }
  }
#endif
}
