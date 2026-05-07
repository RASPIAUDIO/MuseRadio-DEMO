#include "AirPlayService.h"

#if ENABLE_AIRPLAY

#include <WiFi.h>
#include <atomic>
#include <math.h>
#include <string>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern "C" {
#include "raop.h"
}

namespace {
constexpr uint32_t SAMPLE_RATE = 44100;
constexpr uint8_t CHANNELS = 2;
constexpr size_t AIRPLAY_BUFFER_BYTES = 640 * 1024;
constexpr size_t AIRPLAY_BUFFER_FALLBACK_BYTES = 384 * 1024;
constexpr size_t PCM_BLOCK_BYTES = 1408;
constexpr size_t PCM_BLOCK_COUNT = 96;
constexpr UBaseType_t PCM_OUTPUT_TASK_PRIORITY = 5;
constexpr BaseType_t PCM_OUTPUT_TASK_CORE = 0;

struct PcmBlockRef {
  uint16_t index;
  uint16_t len;
};

Audio* s_audio = nullptr;
AirPlayEventHandler s_handler = nullptr;
TaskHandle_t s_task = nullptr;
TaskHandle_t s_pcmTask = nullptr;
std::atomic<bool> s_started(false);
std::atomic<bool> s_active(false);
std::atomic<bool> s_pcmOutputEnabled(false);
struct raop_ctx_s* s_raop = nullptr;
uint8_t* s_buffer = nullptr;
size_t s_bufferSize = 0;
uint8_t* s_pcmBlocks = nullptr;
QueueHandle_t s_pcmFreeQueue = nullptr;
QueueHandle_t s_pcmFillQueue = nullptr;
StaticQueue_t* s_pcmFreeQueueStruct = nullptr;
StaticQueue_t* s_pcmFillQueueStruct = nullptr;
uint8_t* s_pcmFreeQueueStorage = nullptr;
uint8_t* s_pcmFillQueueStorage = nullptr;
String s_deviceName;
std::string s_metadataText;

void emit(AirPlayEvent event, uint32_t value = 0, const char* text = nullptr)
{
  if (s_handler) {
    s_handler(event, value, text);
  }
}

size_t preparePcmBlock(uint8_t* dst, const uint8_t* src, size_t len)
{
  const size_t payload = len - (len % 4);
  memcpy(dst, src, payload);
  return payload;
}

void emitActive(const char* text = "AirPlay")
{
  if (!s_active.exchange(true)) {
    emit(AirPlayEvent::Active, 0, text);
  }
}

void emitInactive(const char* text = "Radio resumes")
{
  if (s_active.exchange(false)) {
    emit(AirPlayEvent::Inactive, 0, text);
  }
}

std::string deviceNameString()
{
  if (s_deviceName.length() == 0) {
    const uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
    char name[24];
    snprintf(name, sizeof(name), "Muse Radio-%04X", (unsigned)suffix);
    s_deviceName = name;
  }
  return std::string(s_deviceName.c_str());
}

std::string hostName()
{
  const uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  char host[24];
  snprintf(host, sizeof(host), "museradio-%04x", (unsigned)suffix);
  return std::string(host);
}

uint32_t localIpAddr()
{
  esp_netif_ip_info_t info = {};
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK) {
    return info.ip.addr;
  }
  return (uint32_t)WiFi.localIP();
}

bool ensureBuffer(uint8_t** buffer, size_t* size)
{
  if (!buffer || !size) return false;

  if (!s_buffer) {
    s_buffer = (uint8_t*)heap_caps_malloc(AIRPLAY_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_bufferSize = s_buffer ? AIRPLAY_BUFFER_BYTES : 0;
  }
  if (!s_buffer) {
    s_buffer = (uint8_t*)heap_caps_malloc(AIRPLAY_BUFFER_FALLBACK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_bufferSize = s_buffer ? AIRPLAY_BUFFER_FALLBACK_BYTES : 0;
  }
  if (!s_buffer) {
    s_buffer = (uint8_t*)malloc(AIRPLAY_BUFFER_FALLBACK_BYTES);
    s_bufferSize = s_buffer ? AIRPLAY_BUFFER_FALLBACK_BYTES : 0;
  }

  *buffer = s_buffer;
  *size = s_bufferSize;
  Serial.printf("[airplay] setup buffer=%p size=%u free_heap=%u free_psram=%u\n",
                (void*)s_buffer,
                (unsigned)s_bufferSize,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return s_buffer != nullptr;
}

bool ensurePcmQueue()
{
  if (s_pcmFreeQueue && s_pcmFillQueue && s_pcmBlocks) return true;

  s_pcmBlocks = (uint8_t*)heap_caps_malloc(PCM_BLOCK_BYTES * PCM_BLOCK_COUNT,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_pcmBlocks) {
    s_pcmBlocks = (uint8_t*)malloc(PCM_BLOCK_BYTES * PCM_BLOCK_COUNT);
  }

  s_pcmFreeQueueStruct = (StaticQueue_t*)heap_caps_malloc(sizeof(StaticQueue_t),
                                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_pcmFillQueueStruct = (StaticQueue_t*)heap_caps_malloc(sizeof(StaticQueue_t),
                                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_pcmFreeQueueStorage = (uint8_t*)heap_caps_malloc(PCM_BLOCK_COUNT * sizeof(uint16_t),
                                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_pcmFillQueueStorage = (uint8_t*)heap_caps_malloc(PCM_BLOCK_COUNT * sizeof(PcmBlockRef),
                                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (!s_pcmBlocks || !s_pcmFreeQueueStruct || !s_pcmFillQueueStruct ||
      !s_pcmFreeQueueStorage || !s_pcmFillQueueStorage) {
    Serial.println("[airplay] PCM queue allocation failed");
    return false;
  }

  s_pcmFreeQueue = xQueueCreateStatic(PCM_BLOCK_COUNT, sizeof(uint16_t),
                                      s_pcmFreeQueueStorage, s_pcmFreeQueueStruct);
  s_pcmFillQueue = xQueueCreateStatic(PCM_BLOCK_COUNT, sizeof(PcmBlockRef),
                                      s_pcmFillQueueStorage, s_pcmFillQueueStruct);
  if (!s_pcmFreeQueue || !s_pcmFillQueue) {
    Serial.println("[airplay] PCM queue create failed");
    return false;
  }

  for (uint16_t i = 0; i < PCM_BLOCK_COUNT; i++) {
    xQueueSend(s_pcmFreeQueue, &i, 0);
  }

  Serial.printf("[airplay] PCM queue ready blocks=%u bytes=%u storage=%p\n",
                (unsigned)PCM_BLOCK_COUNT,
                (unsigned)(PCM_BLOCK_BYTES * PCM_BLOCK_COUNT),
                (void*)s_pcmBlocks);
  return true;
}

void drainPcmQueue()
{
  if (!s_pcmFreeQueue || !s_pcmFillQueue) return;

  PcmBlockRef item = {};
  while (xQueueReceive(s_pcmFillQueue, &item, 0) == pdTRUE) {
    xQueueSend(s_pcmFreeQueue, &item.index, 0);
  }
}

void setPcmOutput(bool enabled)
{
  s_pcmOutputEnabled = enabled;
  if (!enabled) {
    drainPcmQueue();
  }
}

void pcmOutputTask(void*)
{
  Serial.println("[airplay] PCM output task started");

  while (true) {
    if (!s_pcmOutputEnabled.load()) {
      drainPcmQueue();
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    PcmBlockRef item = {};
    if (xQueueReceive(s_pcmFillQueue, &item, 20 / portTICK_PERIOD_MS) != pdTRUE) {
      continue;
    }

    const size_t len = item.len - (item.len % 4);
    if (s_audio && len > 0 && item.index < PCM_BLOCK_COUNT) {
      const uint8_t* block = s_pcmBlocks + ((size_t)item.index * PCM_BLOCK_BYTES);
      const size_t written = s_audio->writeRawPCM16(block, len, SAMPLE_RATE, CHANNELS);
      if (written != len) {
        static uint32_t lastWarnMs = 0;
        const uint32_t nowMs = millis();
        if ((int32_t)(nowMs - lastWarnMs) > 1000) {
          lastWarnMs = nowMs;
          Serial.printf("[airplay] pcm short write %u/%u\n", (unsigned)written, (unsigned)len);
        }
      }
    }

    xQueueSend(s_pcmFreeQueue, &item.index, 0);
  }
}

bool startPcmOutputTask()
{
  if (s_pcmTask) return true;
  if (!ensurePcmQueue()) return false;

  const BaseType_t rc = xTaskCreatePinnedToCore(pcmOutputTask, "airplay_pcm", 4096,
                                                nullptr, PCM_OUTPUT_TASK_PRIORITY,
                                                &s_pcmTask, PCM_OUTPUT_TASK_CORE);
  if (rc != pdPASS) {
    Serial.printf("[airplay] PCM task create failed: rc=%ld\n", (long)rc);
    s_pcmTask = nullptr;
    return false;
  }
  return true;
}

void queuePcmBlock(const u8_t* data, size_t len)
{
  if (!data || len == 0 || !ensurePcmQueue()) return;

  size_t offset = 0;
  while (offset < len) {
    uint16_t index = 0;
    if (xQueueReceive(s_pcmFreeQueue, &index, 0) != pdTRUE) {
      static uint32_t lastWarnMs = 0;
      const uint32_t nowMs = millis();
      if ((int32_t)(nowMs - lastWarnMs) > 1000) {
        lastWarnMs = nowMs;
        Serial.printf("[airplay] pcm queue full fill=%u free=%u\n",
                      (unsigned)uxQueueMessagesWaiting(s_pcmFillQueue),
                      (unsigned)uxQueueMessagesWaiting(s_pcmFreeQueue));
      }
      return;
    }

    const size_t chunk = min((size_t)PCM_BLOCK_BYTES, len - offset);
    const size_t payload = preparePcmBlock(s_pcmBlocks + ((size_t)index * PCM_BLOCK_BYTES), data + offset, chunk);
    if (payload == 0) {
      xQueueSend(s_pcmFreeQueue, &index, 0);
      return;
    }

    PcmBlockRef item = { index, (uint16_t)payload };
    if (xQueueSend(s_pcmFillQueue, &item, 0) != pdTRUE) {
      xQueueSend(s_pcmFreeQueue, &index, 0);
      return;
    }
    offset += chunk;
  }
}

uint32_t volumeToRaw(double normalized)
{
  if (normalized < 0.0) normalized = 0.0;
  if (normalized > 1.0) normalized = 1.0;
  return (uint32_t)lround(normalized * 65535.0);
}

bool handleRaopCommand(raop_event_t event, ...)
{
  bool success = true;
  va_list args;
  va_start(args, event);

  switch (event) {
    case RAOP_SETUP: {
      uint8_t** buffer = va_arg(args, uint8_t**);
      size_t* size = va_arg(args, size_t*);
      success = ensureBuffer(buffer, size) && startPcmOutputTask();
      setPcmOutput(success);
      emitActive("AirPlay");
      break;
    }
    case RAOP_STREAM:
      setPcmOutput(true);
      emitActive("AirPlay");
      break;
    case RAOP_PLAY:
      (void)va_arg(args, u32_t);
      setPcmOutput(true);
      emitActive("AirPlay");
      break;
    case RAOP_FLUSH:
      drainPcmQueue();
      emitActive("AirPlay paused");
      break;
    case RAOP_STOP:
      setPcmOutput(false);
      emitInactive("Radio resumes");
      break;
    case RAOP_STALLED:
      Serial.println("[airplay] stalled, aborting session");
      if (s_raop) raop_abort(s_raop);
      setPcmOutput(false);
      emitInactive("AirPlay stalled");
      break;
    case RAOP_VOLUME: {
      const double normalized = va_arg(args, double);
      emit(AirPlayEvent::Volume, volumeToRaw(normalized), nullptr);
      break;
    }
    case RAOP_METADATA: {
      char* artist = va_arg(args, char*);
      char* album = va_arg(args, char*);
      char* title = va_arg(args, char*);
      (void)va_arg(args, uint32_t);
      s_metadataText = title && title[0] ? title : "AirPlay";
      s_metadataText += "\n";
      s_metadataText += artist && artist[0] ? artist : "Unknown artist";
      if (album && album[0]) {
        s_metadataText += "\n";
        s_metadataText += album;
      }
      emit(AirPlayEvent::Metadata, 0, s_metadataText.c_str());
      break;
    }
    case RAOP_ARTWORK:
      (void)va_arg(args, uint8_t*);
      (void)va_arg(args, int);
      (void)va_arg(args, uint32_t);
      break;
    case RAOP_PROGRESS:
      (void)va_arg(args, int);
      (void)va_arg(args, int);
      break;
    default:
      break;
  }

  va_end(args);
  return success;
}

void handleRaopData(const u8_t* data, size_t len, u32_t)
{
  if (!s_audio || !data || len == 0) return;

  emitActive("AirPlay");
  queuePcmBlock(data, len);
}

bool startRaop()
{
  if (s_raop || WiFi.status() != WL_CONNECTED) return s_raop != nullptr;

  const esp_err_t mdnsErr = mdns_init();
  if (mdnsErr == ESP_OK || mdnsErr == ESP_ERR_INVALID_STATE) {
    mdns_hostname_set(hostName().c_str());
  } else {
    Serial.printf("[airplay] mdns_init failed: %d\n", (int)mdnsErr);
  }

  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  const uint32_t host = localIpAddr();
  std::string name = deviceNameString();

  Serial.printf("[airplay] starting device=%s ip=%s heap=%u psram=%u\n",
                name.c_str(),
                WiFi.localIP().toString().c_str(),
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  s_raop = raop_create(host, (char*)name.c_str(), mac, 0, handleRaopCommand, handleRaopData);
  if (!s_raop) {
    Serial.println("[airplay] raop_create failed");
    return false;
  }

  emit(AirPlayEvent::Ready, 0, name.c_str());
  return true;
}

void stopRaop()
{
  if (!s_raop) return;
  raop_delete(s_raop);
  s_raop = nullptr;
  emitInactive("Radio resumes");
}

void airPlayTask(void*)
{
  Serial.printf("[airplay] task started, wifi=%d\n", (int)WiFi.status());

  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      startRaop();
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    } else {
      stopRaop();
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
  }
}
} // namespace

void airPlayBegin(Audio& audio, AirPlayEventHandler handler)
{
  if (s_started.exchange(true)) {
    Serial.println("[airplay] begin ignored: already started");
    return;
  }

  s_audio = &audio;
  s_handler = handler;
  deviceNameString();
  startPcmOutputTask();

  const BaseType_t rc = xTaskCreatePinnedToCore(airPlayTask, "airplay", 12288, nullptr, 4, &s_task, 1);
  if (rc != pdPASS) {
    Serial.printf("[airplay] task create failed: rc=%ld\n", (long)rc);
    s_task = nullptr;
    s_started = false;
    return;
  }
  Serial.printf("[airplay] task created: handle=%p\n", (void*)s_task);
}

bool airPlayActive()
{
  return s_active.load();
}

void airPlaySetLocalVolume(uint8_t volume, uint8_t maxVolume)
{
  if (!s_raop || maxVolume == 0) return;
  float normalized = (float)volume / (float)maxVolume;
  if (normalized < 0.0f) normalized = 0.0f;
  if (normalized > 1.0f) normalized = 1.0f;
  raop_cmd(s_raop, RAOP_VOLUME, &normalized);
}

void airPlayDisconnect()
{
  if (!s_raop) return;
  Serial.println("[airplay] forced disconnect");
  raop_abort(s_raop);
  emitInactive("Radio resumes");
}

const char* airPlayDeviceName()
{
  deviceNameString();
  return s_deviceName.c_str();
}

#else

namespace {
String s_deviceName;
}

void airPlayBegin(Audio&, AirPlayEventHandler) {}

bool airPlayActive()
{
  return false;
}

void airPlaySetLocalVolume(uint8_t, uint8_t) {}

void airPlayDisconnect() {}

const char* airPlayDeviceName()
{
  if (s_deviceName.length() == 0) {
    const uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
    char name[24];
    snprintf(name, sizeof(name), "Muse Radio-%04X", (unsigned)suffix);
    s_deviceName = name;
  }
  return s_deviceName.c_str();
}

#endif
