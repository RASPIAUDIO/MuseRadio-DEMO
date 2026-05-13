#include "UsbAudioService.h"

#ifndef ENABLE_USB_AUDIO
#define ENABLE_USB_AUDIO 0
#endif

#ifndef ENABLE_USB_MIC
#define ENABLE_USB_MIC 0
#endif

#if ENABLE_USB_AUDIO

#include <atomic>
#include <cstring>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb_device_uac.h"

namespace {
constexpr uint32_t SAMPLE_RATE = 44100;
constexpr uint8_t CHANNELS = 2;
constexpr uint32_t INACTIVE_TIMEOUT_MS = 1500;
constexpr size_t PCM_FRAME_BYTES = CHANNELS * sizeof(int16_t);
constexpr uint32_t PCM_BYTES_PER_SECOND = SAMPLE_RATE * PCM_FRAME_BYTES;
constexpr size_t PCM_BLOCK_BYTES = 2048;
constexpr size_t PCM_BLOCK_TARGET_BYTES = (SAMPLE_RATE * PCM_FRAME_BYTES * 10) / 1000;
constexpr size_t PCM_WRITE_TARGET_BYTES = (SAMPLE_RATE * PCM_FRAME_BYTES * 20) / 1000;
constexpr uint32_t PCM_PREBUFFER_BYTES = (SAMPLE_RATE * PCM_FRAME_BYTES * 90) / 1000;
constexpr uint32_t PCM_DISPLAY_SAFE_BUFFER_MS = 60;
constexpr size_t PCM_BLOCK_COUNT = 128;
constexpr UBaseType_t PCM_OUTPUT_TASK_PRIORITY = 6;
constexpr BaseType_t PCM_OUTPUT_TASK_CORE = 0;
constexpr UBaseType_t MONITOR_TASK_PRIORITY = 2;
constexpr BaseType_t MONITOR_TASK_CORE = 1;
constexpr UBaseType_t EVENT_TASK_PRIORITY = 4;
constexpr BaseType_t EVENT_TASK_CORE = 1;
constexpr uint16_t HID_USAGE_CONSUMER_VOLUME_INCREMENT = 0x00E9;
constexpr uint16_t HID_USAGE_CONSUMER_VOLUME_DECREMENT = 0x00EA;
constexpr uint16_t HID_USAGE_CONSUMER_MUTE = 0x00E2;

struct PcmBlockRef {
  uint16_t index;
  uint16_t len;
};

struct QueuedEvent {
  UsbAudioEvent event;
  uint32_t value;
  char text[40];
};

Audio* s_audio = nullptr;
UsbAudioEventHandler s_handler = nullptr;
TaskHandle_t s_pcmTask = nullptr;
TaskHandle_t s_monitorTask = nullptr;
TaskHandle_t s_eventTask = nullptr;
std::atomic<bool> s_started(false);
std::atomic<bool> s_active(false);
std::atomic<bool> s_pcmOutputEnabled(false);
std::atomic<bool> s_outputModeReady(false);
std::atomic<bool> s_prebufferReady(false);
std::atomic<uint32_t> s_lastPcmMs(0);
std::atomic<uint32_t> s_bufferedBytes(0);
std::atomic<uint32_t> s_droppedBytes(0);
std::atomic<uint32_t> s_underrunCount(0);
std::atomic<uint32_t> s_uacBytesRead(0);
std::atomic<uint32_t> s_micBytesSent(0);
std::atomic<uint32_t> s_micShortReads(0);
std::atomic<uint32_t> s_i2sWriteMaxUs(0);
std::atomic<uint32_t> s_hostVolume(100);
uint8_t* s_pcmBlocks = nullptr;
uint8_t* s_pcmWriteBuffer = nullptr;
QueueHandle_t s_pcmFreeQueue = nullptr;
QueueHandle_t s_pcmFillQueue = nullptr;
QueueHandle_t s_eventQueue = nullptr;
StaticQueue_t* s_pcmFreeQueueStruct = nullptr;
StaticQueue_t* s_pcmFillQueueStruct = nullptr;
uint8_t* s_pcmFreeQueueStorage = nullptr;
uint8_t* s_pcmFillQueueStorage = nullptr;
uint16_t s_pendingIndex = 0;
uint16_t s_pendingLen = 0;
bool s_hasPendingBlock = false;
String s_deviceName;

void dispatchEvent(const QueuedEvent& item)
{
  if (s_handler) {
    s_handler(item.event, item.value, item.text[0] ? item.text : nullptr);
  }

  if (item.event == UsbAudioEvent::Active) {
    s_outputModeReady = true;
  }
}

void eventTask(void*)
{
  while (true) {
    QueuedEvent item = {};
    if (xQueueReceive(s_eventQueue, &item, portMAX_DELAY) == pdTRUE) {
      dispatchEvent(item);
    }
  }
}

bool startEventTask()
{
  if (s_eventTask) return true;

  if (!s_eventQueue) {
    s_eventQueue = xQueueCreate(16, sizeof(QueuedEvent));
    if (!s_eventQueue) {
      Serial.println("[usb-audio] event queue create failed");
      return false;
    }
  }

  const BaseType_t rc = xTaskCreatePinnedToCore(eventTask, "usb_audio_evt", 4096,
                                                nullptr, EVENT_TASK_PRIORITY,
                                                &s_eventTask, EVENT_TASK_CORE);
  if (rc != pdPASS) {
    Serial.printf("[usb-audio] event task create failed rc=%ld\n", (long)rc);
    s_eventTask = nullptr;
    return false;
  }
  return true;
}

void emit(UsbAudioEvent event, uint32_t value = 0, const char* text = nullptr)
{
  QueuedEvent item = {};
  item.event = event;
  item.value = value;
  if (text) {
    strlcpy(item.text, text, sizeof(item.text));
  }

  if (s_eventQueue) {
    if (xQueueSend(s_eventQueue, &item, 0) == pdTRUE) {
      return;
    }
    Serial.println("[usb-audio] event queue full");
  }

  dispatchEvent(item);
}

void addBufferedBytes(uint32_t bytes)
{
  s_bufferedBytes.fetch_add(bytes);
}

void removeBufferedBytes(uint32_t bytes)
{
  uint32_t current = s_bufferedBytes.load();
  while (true) {
    const uint32_t next = (current > bytes) ? (current - bytes) : 0;
    if (s_bufferedBytes.compare_exchange_weak(current, next)) {
      return;
    }
  }
}

void addDroppedBytes(uint32_t bytes)
{
  s_droppedBytes.fetch_add(bytes);
}

void addUnderrun()
{
  s_underrunCount.fetch_add(1);
}

uint32_t bufferedMsFromBytes(uint32_t bytes)
{
  return (uint32_t)(((uint64_t)bytes * 1000ULL) / PCM_BYTES_PER_SECOND);
}

void updateMaxUs(uint32_t value)
{
  uint32_t current = s_i2sWriteMaxUs.load();
  while (value > current && !s_i2sWriteMaxUs.compare_exchange_weak(current, value)) {
  }
}

void buildDeviceName()
{
  if (s_deviceName.length() != 0) return;
  const uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  char name[24];
  snprintf(name, sizeof(name), "Muse Radio-%04X", (unsigned)suffix);
  s_deviceName = name;
}

bool ensurePcmQueue()
{
  if (s_pcmFreeQueue && s_pcmFillQueue && s_pcmBlocks) return true;

  s_pcmBlocks = (uint8_t*)heap_caps_malloc(PCM_BLOCK_BYTES * PCM_BLOCK_COUNT,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_pcmBlocks) {
    s_pcmBlocks = (uint8_t*)heap_caps_malloc(PCM_BLOCK_BYTES * PCM_BLOCK_COUNT,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!s_pcmBlocks) {
    s_pcmBlocks = (uint8_t*)malloc(PCM_BLOCK_BYTES * PCM_BLOCK_COUNT);
  }

  s_pcmWriteBuffer = (uint8_t*)heap_caps_malloc(PCM_WRITE_TARGET_BYTES,
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_pcmWriteBuffer) {
    s_pcmWriteBuffer = (uint8_t*)malloc(PCM_WRITE_TARGET_BYTES);
  }

  s_pcmFreeQueueStruct = (StaticQueue_t*)heap_caps_malloc(sizeof(StaticQueue_t),
                                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_pcmFillQueueStruct = (StaticQueue_t*)heap_caps_malloc(sizeof(StaticQueue_t),
                                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_pcmFreeQueueStorage = (uint8_t*)heap_caps_malloc(PCM_BLOCK_COUNT * sizeof(uint16_t),
                                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_pcmFillQueueStorage = (uint8_t*)heap_caps_malloc(PCM_BLOCK_COUNT * sizeof(PcmBlockRef),
                                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (!s_pcmBlocks || !s_pcmWriteBuffer || !s_pcmFreeQueueStruct || !s_pcmFillQueueStruct ||
      !s_pcmFreeQueueStorage || !s_pcmFillQueueStorage) {
    Serial.println("[usb-audio] PCM queue allocation failed");
    return false;
  }

  s_pcmFreeQueue = xQueueCreateStatic(PCM_BLOCK_COUNT, sizeof(uint16_t),
                                      s_pcmFreeQueueStorage, s_pcmFreeQueueStruct);
  s_pcmFillQueue = xQueueCreateStatic(PCM_BLOCK_COUNT, sizeof(PcmBlockRef),
                                      s_pcmFillQueueStorage, s_pcmFillQueueStruct);
  if (!s_pcmFreeQueue || !s_pcmFillQueue) {
    Serial.println("[usb-audio] PCM queue create failed");
    return false;
  }

  for (uint16_t i = 0; i < PCM_BLOCK_COUNT; i++) {
    xQueueSend(s_pcmFreeQueue, &i, 0);
  }

  Serial.printf("[usb-audio] PCM queue ready blocks=%u bytes=%u storage=%p\n",
                (unsigned)PCM_BLOCK_COUNT,
                (unsigned)(PCM_BLOCK_BYTES * PCM_BLOCK_COUNT),
                (void*)s_pcmBlocks);
  return true;
}

void releasePendingBlock()
{
  if (!s_hasPendingBlock || !s_pcmFreeQueue) return;
  xQueueSend(s_pcmFreeQueue, &s_pendingIndex, 0);
  s_pendingIndex = 0;
  s_pendingLen = 0;
  s_hasPendingBlock = false;
}

bool flushPendingBlock()
{
  if (!s_hasPendingBlock || s_pendingLen == 0 || !s_pcmFillQueue) return true;

  PcmBlockRef item = { s_pendingIndex, (uint16_t)(s_pendingLen - (s_pendingLen % PCM_FRAME_BYTES)) };
  if (item.len == 0) {
    releasePendingBlock();
    return true;
  }

  if (xQueueSend(s_pcmFillQueue, &item, 0) != pdTRUE) {
    addDroppedBytes(item.len);
    releasePendingBlock();
    return false;
  }

  addBufferedBytes(item.len);
  s_pendingIndex = 0;
  s_pendingLen = 0;
  s_hasPendingBlock = false;
  return true;
}

void drainPcmQueue()
{
  if (!s_pcmFreeQueue || !s_pcmFillQueue) return;

  releasePendingBlock();
  PcmBlockRef item = {};
  while (xQueueReceive(s_pcmFillQueue, &item, 0) == pdTRUE) {
    xQueueSend(s_pcmFreeQueue, &item.index, 0);
  }
  s_bufferedBytes = 0;
}

void emitActive()
{
  s_lastPcmMs = millis();
  if (!s_pcmOutputEnabled.exchange(true)) {
    s_outputModeReady = false;
    s_prebufferReady = false;
  }
  if (!s_active.exchange(true)) {
    emit(UsbAudioEvent::Active, 0, usbAudioDeviceName());
  }
}

void emitInactive(const char* text = "Radio resumes")
{
  s_pcmOutputEnabled = false;
  s_outputModeReady = false;
  s_prebufferReady = false;
  drainPcmQueue();
  if (s_active.exchange(false)) {
    emit(UsbAudioEvent::Inactive, 0, text);
  }
}

void pcmOutputTask(void*)
{
  Serial.println("[usb-audio] PCM output task started");

  while (true) {
    if (!s_pcmOutputEnabled.load()) {
      drainPcmQueue();
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    if (!s_outputModeReady.load()) {
      vTaskDelay(2 / portTICK_PERIOD_MS);
      continue;
    }

    if (!s_prebufferReady.load()) {
      if (s_bufferedBytes.load() < PCM_PREBUFFER_BYTES) {
        vTaskDelay(2 / portTICK_PERIOD_MS);
        continue;
      }
      s_prebufferReady = true;
    }

    size_t writeLen = 0;
    while (writeLen < PCM_WRITE_TARGET_BYTES) {
      PcmBlockRef item = {};
      const TickType_t waitTicks = (writeLen == 0) ? (20 / portTICK_PERIOD_MS) : 0;
      if (xQueueReceive(s_pcmFillQueue, &item, waitTicks) != pdTRUE) {
        break;
      }

      const size_t len = item.len - (item.len % PCM_FRAME_BYTES);
      if (len > 0 && item.index < PCM_BLOCK_COUNT) {
        const size_t copyLen = min(len, PCM_WRITE_TARGET_BYTES - writeLen);
        memcpy(s_pcmWriteBuffer + writeLen,
               s_pcmBlocks + ((size_t)item.index * PCM_BLOCK_BYTES),
               copyLen);
        writeLen += copyLen;
        removeBufferedBytes((uint32_t)len);
        if (copyLen < len) {
          addDroppedBytes((uint32_t)(len - copyLen));
        }
      }
      xQueueSend(s_pcmFreeQueue, &item.index, 0);
    }

    if (writeLen == 0) {
      addUnderrun();
      s_prebufferReady = false;
      continue;
    }

    if (s_audio) {
      size_t offset = 0;
      while (offset < writeLen) {
        const uint32_t writeStartUs = micros();
        const size_t written = s_audio->writeRawPCM16(s_pcmWriteBuffer + offset,
                                                      writeLen - offset,
                                                      SAMPLE_RATE, CHANNELS);
        updateMaxUs((uint32_t)(micros() - writeStartUs));
        if (written == 0) {
          addUnderrun();
          s_prebufferReady = false;
          static uint32_t lastWarnMs = 0;
          const uint32_t nowMs = millis();
          if ((int32_t)(nowMs - lastWarnMs) > 1000) {
            lastWarnMs = nowMs;
            Serial.printf("[usb-audio] pcm write stalled at %u/%u buffered=%u\n",
                          (unsigned)offset, (unsigned)writeLen,
                          (unsigned)s_bufferedBytes.load());
          }
          break;
        }
        offset += written;
      }
    }
  }
}

bool startPcmOutputTask()
{
  if (s_pcmTask) return true;
  if (!ensurePcmQueue()) return false;

  const BaseType_t rc = xTaskCreatePinnedToCore(pcmOutputTask, "usb_audio_pcm", 8192,
                                                nullptr, PCM_OUTPUT_TASK_PRIORITY,
                                                &s_pcmTask, PCM_OUTPUT_TASK_CORE);
  if (rc != pdPASS) {
    Serial.printf("[usb-audio] PCM task create failed rc=%ld\n", (long)rc);
    s_pcmTask = nullptr;
    return false;
  }
  return true;
}

void queuePcmBlock(const uint8_t* data, size_t len)
{
  if (!data || len == 0 || !ensurePcmQueue()) return;

  size_t offset = 0;
  while (offset < len) {
    if (!s_hasPendingBlock) {
      uint16_t index = 0;
      if (xQueueReceive(s_pcmFreeQueue, &index, 0) != pdTRUE) {
        addDroppedBytes((uint32_t)(len - offset));
        static uint32_t lastWarnMs = 0;
        const uint32_t nowMs = millis();
        if ((int32_t)(nowMs - lastWarnMs) > 1000) {
          lastWarnMs = nowMs;
          Serial.printf("[usb-audio] pcm queue full fill=%u free=%u buffered=%u dropped=%u\n",
                        (unsigned)uxQueueMessagesWaiting(s_pcmFillQueue),
                        (unsigned)uxQueueMessagesWaiting(s_pcmFreeQueue),
                        (unsigned)s_bufferedBytes.load(),
                        (unsigned)s_droppedBytes.load());
        }
        return;
      }
      s_pendingIndex = index;
      s_pendingLen = 0;
      s_hasPendingBlock = true;
    }

    const size_t space = PCM_BLOCK_TARGET_BYTES - s_pendingLen;
    const size_t chunk = min(space, len - offset);
    const size_t payload = chunk - (chunk % PCM_FRAME_BYTES);
    if (payload == 0) {
      return;
    }

    memcpy(s_pcmBlocks + ((size_t)s_pendingIndex * PCM_BLOCK_BYTES) + s_pendingLen,
           data + offset, payload);
    s_pendingLen += payload;
    offset += payload;

    if (s_pendingLen >= PCM_BLOCK_TARGET_BYTES && !flushPendingBlock()) {
      return;
    }
  }
}

esp_err_t uacOutputCb(uint8_t* buf, size_t len, void*)
{
  if (!buf || len == 0) return ESP_OK;
  s_uacBytesRead.fetch_add((uint32_t)len);
  emitActive();
  queuePcmBlock(buf, len);
  return ESP_OK;
}

esp_err_t uacInputCb(uint8_t* buf, size_t len, size_t* bytesRead, void*)
{
  if (!buf || !bytesRead) return ESP_ERR_INVALID_ARG;
  size_t readLen = 0;
#if ENABLE_USB_MIC
  if (s_audio && len > 0) {
    readLen = s_audio->readRawPCM16(buf, len, SAMPLE_RATE, CHANNELS);
  }
#endif
  if (readLen < len) {
    memset(buf + readLen, 0, len - readLen);
    s_micShortReads.fetch_add(1);
  }
  *bytesRead = len;
  s_micBytesSent.fetch_add((uint32_t)len);
  return ESP_OK;
}

void uacSetMuteCb(uint32_t mute, void*)
{
  Serial.printf("[usb-audio] mute=%lu\n", (unsigned long)mute);
  emit(UsbAudioEvent::Mute, mute ? 1 : 0, nullptr);
}

void uacSetVolumeCb(uint32_t volume, void*)
{
  if (volume > 100) volume = 100;
  s_hostVolume = volume;
  Serial.printf("[usb-audio] volume=%lu\n", (unsigned long)volume);
  emit(UsbAudioEvent::Volume, volume, nullptr);
}

void monitorTask(void*)
{
  while (true) {
    const uint32_t nowMs = millis();
    if (s_active.load()) {
      const uint32_t last = s_lastPcmMs.load();
      if (last != 0 && (int32_t)(nowMs - last) > (int32_t)INACTIVE_TIMEOUT_MS) {
        flushPendingBlock();
        emitInactive("Radio resumes");
      }
    }
    static uint32_t lastLogMs = 0;
    static uint32_t lastUacBytes = 0;
    static uint32_t lastMicBytes = 0;
    static uint32_t lastDropped = 0;
    static uint32_t lastUnderruns = 0;
    const uint32_t micBytes = s_micBytesSent.load();
    if ((s_active.load() || micBytes != lastMicBytes) && (int32_t)(nowMs - lastLogMs) > 2000) {
      const uint32_t uacBytes = s_uacBytesRead.load();
      const uint32_t dropped = s_droppedBytes.load();
      const uint32_t underruns = s_underrunCount.load();
      const uint32_t bufferedBytes = s_bufferedBytes.load();
      const uint32_t i2sMaxUs = s_i2sWriteMaxUs.exchange(0);
      lastLogMs = nowMs;
      Serial.printf("[usb-audio] stats uac=%u +%u mic=%u +%u mic_short=%u buffer=%ums dropped=%u +%u underruns=%u +%u i2s_max_us=%u volume=%u fill=%u free=%u\n",
                    (unsigned)uacBytes,
                    (unsigned)(uacBytes - lastUacBytes),
                    (unsigned)micBytes,
                    (unsigned)(micBytes - lastMicBytes),
                    (unsigned)s_micShortReads.load(),
                    (unsigned)bufferedMsFromBytes(bufferedBytes),
                    (unsigned)dropped,
                    (unsigned)(dropped - lastDropped),
                    (unsigned)underruns,
                    (unsigned)(underruns - lastUnderruns),
                    (unsigned)i2sMaxUs,
                    (unsigned)s_hostVolume.load(),
                    s_pcmFillQueue ? (unsigned)uxQueueMessagesWaiting(s_pcmFillQueue) : 0,
                    s_pcmFreeQueue ? (unsigned)uxQueueMessagesWaiting(s_pcmFreeQueue) : 0);
      lastUacBytes = uacBytes;
      lastMicBytes = micBytes;
      lastDropped = dropped;
      lastUnderruns = underruns;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}
} // namespace

void usbAudioBegin(Audio& audio, UsbAudioEventHandler handler)
{
  if (s_started.exchange(true)) {
    Serial.println("[usb-audio] begin ignored: already started");
    return;
  }

  s_audio = &audio;
  s_handler = handler;
  buildDeviceName();

  if (!startEventTask()) {
    s_started = false;
    return;
  }

  if (!startPcmOutputTask()) {
    s_started = false;
    return;
  }

  if (!s_monitorTask) {
    const BaseType_t rc = xTaskCreatePinnedToCore(monitorTask, "usb_audio_mon", 4096,
                                                  nullptr, MONITOR_TASK_PRIORITY,
                                                  &s_monitorTask, MONITOR_TASK_CORE);
    if (rc != pdPASS) {
      Serial.printf("[usb-audio] monitor task create failed rc=%ld\n", (long)rc);
      s_monitorTask = nullptr;
      s_started = false;
      return;
    }
  }

  uac_device_config_t config = {};
  config.output_cb = uacOutputCb;
  config.input_cb = ENABLE_USB_MIC ? uacInputCb : nullptr;
  config.set_mute_cb = uacSetMuteCb;
  config.set_volume_cb = uacSetVolumeCb;
  config.cb_ctx = nullptr;

  const esp_err_t err = uac_device_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[usb-audio] uac_device_init failed err=%d\n", (int)err);
    s_started = false;
    return;
  }

  Serial.printf("[usb-audio] ready device=%s rate=%u speaker_channels=%u mic_channels=%u\n",
                s_deviceName.c_str(), (unsigned)SAMPLE_RATE, (unsigned)CHANNELS,
                ENABLE_USB_MIC ? (unsigned)CHANNELS : 0U);
  emit(UsbAudioEvent::Ready, 0, s_deviceName.c_str());
}

bool usbAudioActive()
{
  return s_active.load();
}

uint32_t usbAudioBufferedMs()
{
  return bufferedMsFromBytes(s_bufferedBytes.load());
}

bool usbAudioBufferHealthy()
{
  return !s_active.load() || usbAudioBufferedMs() >= PCM_DISPLAY_SAFE_BUFFER_MS;
}

const char* usbAudioDeviceName()
{
  buildDeviceName();
  return s_deviceName.c_str();
}

void usbAudioSetLocalVolume(uint8_t volume, uint8_t maxVolume)
{
  if (maxVolume == 0) return;
  uint32_t percent = ((uint32_t)volume * 100UL + (maxVolume / 2)) / maxVolume;
  if (percent > 100) percent = 100;
  s_hostVolume = percent;
  const esp_err_t err = uac_device_set_volume_percent(percent);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("[usb-audio] local volume sync failed err=%d\n", (int)err);
  }
}

void usbAudioSetLocalMute(bool muted)
{
  const esp_err_t err = uac_device_set_mute_state(muted);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("[usb-audio] local mute sync failed err=%d\n", (int)err);
  }
}

void usbAudioSendHostVolumeDelta(int8_t steps)
{
  if (steps == 0) return;
  const uint16_t usage = steps > 0 ? HID_USAGE_CONSUMER_VOLUME_INCREMENT
                                   : HID_USAGE_CONSUMER_VOLUME_DECREMENT;
  uint8_t count = (uint8_t)abs((int)steps);
  if (count > 16) count = 16;
  while (count--) {
    const esp_err_t err = uac_device_send_hid_consumer_control(usage);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && err != ESP_ERR_TIMEOUT) {
      Serial.printf("[usb-audio] HID volume sync failed err=%d\n", (int)err);
      break;
    }
    vTaskDelay(8 / portTICK_PERIOD_MS);
  }
}

void usbAudioSendHostMuteToggle()
{
  const esp_err_t err = uac_device_send_hid_consumer_control(HID_USAGE_CONSUMER_MUTE);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && err != ESP_ERR_TIMEOUT) {
    Serial.printf("[usb-audio] HID mute sync failed err=%d\n", (int)err);
  }
}

#else

namespace {
String s_deviceName;
}

void usbAudioBegin(Audio&, UsbAudioEventHandler) {}

bool usbAudioActive()
{
  return false;
}

uint32_t usbAudioBufferedMs()
{
  return 0;
}

bool usbAudioBufferHealthy()
{
  return true;
}

const char* usbAudioDeviceName()
{
  if (s_deviceName.length() == 0) {
    const uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
    char name[24];
    snprintf(name, sizeof(name), "Muse Radio-%04X", (unsigned)suffix);
    s_deviceName = name;
  }
  return s_deviceName.c_str();
}

void usbAudioSetLocalVolume(uint8_t, uint8_t) {}

void usbAudioSetLocalMute(bool) {}

void usbAudioSendHostVolumeDelta(int8_t) {}

void usbAudioSendHostMuteToggle() {}

#endif
