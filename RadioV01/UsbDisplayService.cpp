#include "UsbDisplayService.h"

#ifndef ENABLE_USB_DISPLAY
#define ENABLE_USB_DISPLAY 0
#endif

#ifndef ENABLE_USB_AUDIO
#define ENABLE_USB_AUDIO 0
#endif

#if ENABLE_USB_DISPLAY

#include <atomic>
#include <string.h>

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp32s3/rom/tjpgd.h"
#if ENABLE_USB_AUDIO
#include "UsbAudioService.h"
#endif

extern "C" {
uint32_t tud_vendor_n_available(uint8_t itf);
uint32_t tud_vendor_n_read(uint8_t itf, void* buffer, uint32_t bufsize);
}

namespace {
constexpr uint16_t DISPLAY_WIDTH = 320;
constexpr uint16_t DISPLAY_HEIGHT = 240;
constexpr uint32_t MIN_FRAME_INTERVAL_MS = 100;
constexpr size_t RX_CHUNK_BYTES = 64;
constexpr size_t FRAME_LIMIT_BYTES = 65536;
constexpr size_t FRAME_SLOT_COUNT = 3;
constexpr size_t JPEG_WORK_BYTES = 4096;
constexpr size_t TILE_PIXELS = 1024;
constexpr UBaseType_t RENDER_TASK_PRIORITY = 2;
constexpr BaseType_t RENDER_TASK_CORE = 1;

#ifndef USB_DISPLAY_SWAP_RED_BLUE
#define USB_DISPLAY_SWAP_RED_BLUE 0
#endif

enum UsbDisplayFrameType : uint8_t {
  UDISP_TYPE_RGB565 = 0,
  UDISP_TYPE_RGB888 = 1,
  UDISP_TYPE_YUV420 = 2,
  UDISP_TYPE_JPG = 3,
  UDISP_TYPE_END = 0xff,
};

struct __attribute__((packed)) UsbDisplayFrameHeader {
  uint16_t crc16;
  uint8_t type;
  uint8_t cmd;
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
  uint32_t frame_id : 10;
  uint32_t payload_total : 22;
};

struct FrameSlot {
  uint8_t* data;
  uint32_t len;
  uint32_t expected;
  uint16_t width;
  uint16_t height;
  uint8_t type;
  uint32_t frameId;
};

struct JpegInput {
  const uint8_t* data;
  size_t len;
  size_t pos;
};

TFT_eSPI* s_tft = nullptr;
UsbDisplayEventHandler s_handler = nullptr;
TaskHandle_t s_renderTask = nullptr;
QueueHandle_t s_freeQueue = nullptr;
QueueHandle_t s_fullQueue = nullptr;
FrameSlot s_slots[FRAME_SLOT_COUNT] = {};
FrameSlot* s_current = nullptr;
bool s_skipFrame = false;
uint32_t s_skipReceived = 0;
uint32_t s_skipExpected = 0;
uint8_t s_rxBuf[RX_CHUNK_BYTES];
uint8_t* s_jpegWork = nullptr;
uint16_t* s_tile = nullptr;
String s_deviceName;
std::atomic<bool> s_started(false);
std::atomic<bool> s_active(false);
std::atomic<uint32_t> s_lastFrameMs(0);
std::atomic<uint32_t> s_lastDrawMs(0);
std::atomic<uint32_t> s_frames(0);
std::atomic<uint32_t> s_dropped(0);

uint16_t rgbTo565(uint8_t r, uint8_t g, uint8_t b)
{
#if USB_DISPLAY_SWAP_RED_BLUE
  const uint8_t tmp = r;
  r = b;
  b = tmp;
#endif
  return ((uint16_t)(r & 0xF8) << 8) |
         ((uint16_t)(g & 0xFC) << 3) |
         (uint16_t)(b >> 3);
}

void buildDeviceName()
{
  if (s_deviceName.length() != 0) return;
  const uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  char name[32];
  snprintf(name, sizeof(name), "Muse Radio-%04X", (unsigned)suffix);
  s_deviceName = name;
}

void emit(UsbDisplayEvent event, uint32_t value = 0, const char* text = nullptr)
{
  if (s_handler) {
    s_handler(event, value, text);
  }
}

void emitActive()
{
  s_lastFrameMs = millis();
  if (!s_active.exchange(true)) {
    emit(UsbDisplayEvent::Active, 0, usbDisplayDeviceName());
  }
}

void countDrop(uint32_t bytes)
{
  const uint32_t dropped = s_dropped.fetch_add(1) + 1;
  if ((dropped % 25) == 1) {
    Serial.printf("[usb-display] dropped frame count=%lu bytes=%lu\n",
                  (unsigned long)dropped, (unsigned long)bytes);
  }
  emit(UsbDisplayEvent::Dropped, dropped, nullptr);
}

bool audioAllowsDisplayFrame()
{
#if ENABLE_USB_AUDIO
  return !usbAudioActive() || usbAudioBufferHealthy();
#else
  return true;
#endif
}

void returnSlot(FrameSlot* slot)
{
  if (!slot || !s_freeQueue) return;
  slot->len = 0;
  slot->expected = 0;
  xQueueSend(s_freeQueue, &slot, 0);
}

bool queueFullSlot(FrameSlot* slot)
{
  if (!slot || !s_fullQueue) return false;
  if (xQueueSend(s_fullQueue, &slot, 0) == pdTRUE) {
    return true;
  }
  countDrop(slot->len);
  returnSlot(slot);
  return false;
}

void resetReceiver()
{
  if (s_current) {
    returnSlot(s_current);
    s_current = nullptr;
  }
  s_skipFrame = false;
  s_skipReceived = 0;
  s_skipExpected = 0;
}

bool finishSkip(uint32_t len)
{
  if (s_skipReceived + len >= s_skipExpected) {
    s_skipFrame = false;
    s_skipReceived = 0;
    s_skipExpected = 0;
    return true;
  }
  s_skipReceived += len;
  return false;
}

void appendToCurrent(const uint8_t* data, uint32_t len)
{
  if (!s_current || !data || len == 0) return;
  const uint32_t room = s_current->expected > s_current->len
                          ? s_current->expected - s_current->len
                          : 0;
  const uint32_t copyLen = len < room ? len : room;
  if (copyLen > 0) {
    memcpy(s_current->data + s_current->len, data, copyLen);
    s_current->len += copyLen;
  }

  if (s_current->len >= s_current->expected) {
    FrameSlot* done = s_current;
    s_current = nullptr;
    queueFullSlot(done);
  }
}

void startFrame(const UsbDisplayFrameHeader& header, const uint8_t* payload, uint32_t payloadLen)
{
  const bool supportedType = header.type == UDISP_TYPE_JPG || header.type == UDISP_TYPE_RGB565;
  const bool supportedGeometry = header.x == 0 && header.y == 0 &&
                                 header.width == DISPLAY_WIDTH &&
                                 header.height == DISPLAY_HEIGHT;
  const uint32_t expected = header.payload_total;

  if (!supportedType || !supportedGeometry || expected == 0 ||
      expected > FRAME_LIMIT_BYTES || !audioAllowsDisplayFrame()) {
    s_skipFrame = true;
    s_skipExpected = expected;
    s_skipReceived = payloadLen;
    countDrop(expected);
    if (s_skipReceived >= s_skipExpected) {
      s_skipFrame = false;
    }
    return;
  }

  FrameSlot* slot = nullptr;
  if (!s_freeQueue || xQueueReceive(s_freeQueue, &slot, 0) != pdTRUE || !slot) {
    s_skipFrame = true;
    s_skipExpected = expected;
    s_skipReceived = payloadLen;
    countDrop(expected);
    if (s_skipReceived >= s_skipExpected) {
      s_skipFrame = false;
    }
    return;
  }

  slot->len = 0;
  slot->expected = expected;
  slot->width = header.width;
  slot->height = header.height;
  slot->type = header.type;
  slot->frameId = header.frame_id;
  s_current = slot;
  appendToCurrent(payload, payloadLen);
}

UINT jpegInput(JDEC* decoder, BYTE* buffer, UINT requested)
{
  JpegInput* input = static_cast<JpegInput*>(decoder->device);
  if (!input || input->pos >= input->len) return 0;
  const UINT available = (UINT)min((size_t)requested, input->len - input->pos);
  if (buffer) {
    memcpy(buffer, input->data + input->pos, available);
  }
  input->pos += available;
  return available;
}

UINT jpegOutput(JDEC*, void* bitmap, JRECT* rect)
{
  if (!s_tft || !bitmap || !rect || !s_tile) return 0;

  const int srcWidth = rect->right - rect->left + 1;
  const int srcHeight = rect->bottom - rect->top + 1;
  if (srcWidth <= 0 || srcHeight <= 0) return 1;

  int drawX = rect->left;
  int drawY = rect->top;
  int drawW = srcWidth;
  int drawH = srcHeight;
  if (drawX >= DISPLAY_WIDTH || drawY >= DISPLAY_HEIGHT) return 1;
  if (drawX + drawW > DISPLAY_WIDTH) drawW = DISPLAY_WIDTH - drawX;
  if (drawY + drawH > DISPLAY_HEIGHT) drawH = DISPLAY_HEIGHT - drawY;
  if (drawW <= 0 || drawH <= 0) return 1;
  if ((size_t)drawW * (size_t)drawH > TILE_PIXELS) return 0;

  const uint8_t* src = static_cast<const uint8_t*>(bitmap);
  for (int y = 0; y < drawH; y++) {
    for (int x = 0; x < drawW; x++) {
      const size_t srcIndex = ((size_t)y * (size_t)srcWidth + (size_t)x) * 3;
      const uint8_t r = src[srcIndex + 0];
      const uint8_t g = src[srcIndex + 1];
      const uint8_t b = src[srcIndex + 2];
      s_tile[(size_t)y * (size_t)drawW + (size_t)x] = rgbTo565(r, g, b);
    }
  }

  s_tft->pushImage(drawX, drawY, drawW, drawH, s_tile);
  return 1;
}

bool drawJpegFrame(const FrameSlot* slot)
{
  if (!slot || !slot->data || slot->len == 0 || !s_jpegWork) return false;

  JDEC decoder = {};
  JpegInput input = { slot->data, slot->len, 0 };
  JRESULT res = jd_prepare(&decoder, jpegInput, s_jpegWork, JPEG_WORK_BYTES, &input);
  if (res != JDR_OK) {
    Serial.printf("[usb-display] jpeg prepare failed=%d len=%lu\n",
                  (int)res, (unsigned long)slot->len);
    return false;
  }

  const bool oldSwap = s_tft->getSwapBytes();
  s_tft->setSwapBytes(true);
  s_tft->startWrite();
  res = jd_decomp(&decoder, jpegOutput, 0);
  s_tft->endWrite();
  s_tft->setSwapBytes(oldSwap);

  if (res != JDR_OK) {
    Serial.printf("[usb-display] jpeg decode failed=%d len=%lu\n",
                  (int)res, (unsigned long)slot->len);
    return false;
  }
  return true;
}

bool drawRgb565Frame(const FrameSlot* slot)
{
  if (!slot || !slot->data || slot->len < (DISPLAY_WIDTH * DISPLAY_HEIGHT * 2UL)) return false;
  const bool oldSwap = s_tft->getSwapBytes();
  s_tft->setSwapBytes(true);
  s_tft->pushImage(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, (uint16_t*)slot->data);
  s_tft->setSwapBytes(oldSwap);
  return true;
}

void drawFrame(const FrameSlot* slot)
{
  if (!s_tft || !slot) return;
  s_tft->setRotation(1);
  emitActive();

  const uint32_t now = millis();
  const uint32_t lastDraw = s_lastDrawMs.load();
  if (lastDraw != 0 &&
      (int32_t)(now - lastDraw) < (int32_t)MIN_FRAME_INTERVAL_MS) {
    countDrop(slot->len);
    return;
  }
  if (!audioAllowsDisplayFrame()) {
    countDrop(slot->len);
    return;
  }
  s_lastDrawMs = now;

  bool ok = false;
  if (slot->type == UDISP_TYPE_JPG) {
    ok = drawJpegFrame(slot);
  } else if (slot->type == UDISP_TYPE_RGB565) {
    ok = drawRgb565Frame(slot);
  }

  if (ok) {
    const uint32_t frameCount = s_frames.fetch_add(1) + 1;
    if ((frameCount % 50) == 0) {
      Serial.printf("[usb-display] frames=%lu dropped=%lu last=%lu bytes\n",
                    (unsigned long)frameCount,
                    (unsigned long)s_dropped.load(),
                    (unsigned long)slot->len);
    }
    emit(UsbDisplayEvent::Frame, frameCount, nullptr);
  } else {
    countDrop(slot->len);
  }
}

bool allocateBuffers()
{
  if (!s_freeQueue) {
    s_freeQueue = xQueueCreate(FRAME_SLOT_COUNT, sizeof(FrameSlot*));
  }
  if (!s_fullQueue) {
    s_fullQueue = xQueueCreate(FRAME_SLOT_COUNT, sizeof(FrameSlot*));
  }
  if (!s_freeQueue || !s_fullQueue) {
    Serial.println("[usb-display] queue allocation failed");
    return false;
  }

  for (size_t i = 0; i < FRAME_SLOT_COUNT; i++) {
    if (!s_slots[i].data) {
      s_slots[i].data = (uint8_t*)heap_caps_malloc(FRAME_LIMIT_BYTES,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!s_slots[i].data) {
        s_slots[i].data = (uint8_t*)heap_caps_malloc(FRAME_LIMIT_BYTES,
                                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      }
    }
    if (!s_slots[i].data) {
      Serial.printf("[usb-display] frame slot %u allocation failed\n", (unsigned)i);
      return false;
    }
    FrameSlot* slot = &s_slots[i];
    xQueueSend(s_freeQueue, &slot, 0);
  }

  if (!s_jpegWork) {
    s_jpegWork = (uint8_t*)heap_caps_malloc(JPEG_WORK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!s_tile) {
    s_tile = (uint16_t*)heap_caps_malloc(TILE_PIXELS * sizeof(uint16_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!s_jpegWork || !s_tile) {
    Serial.println("[usb-display] jpeg scratch allocation failed");
    return false;
  }
  return true;
}

void renderTask(void*)
{
  Serial.println("[usb-display] render task started");
  while (true) {
    FrameSlot* slot = nullptr;
    if (xQueueReceive(s_fullQueue, &slot, 200 / portTICK_PERIOD_MS) == pdTRUE) {
      drawFrame(slot);
      returnSlot(slot);
    }
  }
}

bool startRenderTask()
{
  if (s_renderTask) return true;
  const BaseType_t rc = xTaskCreatePinnedToCore(renderTask, "usb_display",
                                                8192, nullptr,
                                                RENDER_TASK_PRIORITY,
                                                &s_renderTask,
                                                RENDER_TASK_CORE);
  if (rc != pdPASS) {
    Serial.printf("[usb-display] render task create failed rc=%ld\n", (long)rc);
    s_renderTask = nullptr;
    return false;
  }
  return true;
}

void receiveVendorData(uint8_t itf)
{
  if (!s_started.load()) return;

  while (tud_vendor_n_available(itf)) {
    const int readLen = tud_vendor_n_read(itf, s_rxBuf, sizeof(s_rxBuf));
    if (readLen <= 0) return;

    if (s_skipFrame) {
      finishSkip((uint32_t)readLen);
      continue;
    }

    if (!s_current) {
      if ((size_t)readLen < sizeof(UsbDisplayFrameHeader)) {
        countDrop((uint32_t)readLen);
        continue;
      }
      const UsbDisplayFrameHeader* header =
        reinterpret_cast<const UsbDisplayFrameHeader*>(s_rxBuf);
      if (header->type == UDISP_TYPE_END) {
        continue;
      }
      startFrame(*header,
                 s_rxBuf + sizeof(UsbDisplayFrameHeader),
                 (uint32_t)(readLen - sizeof(UsbDisplayFrameHeader)));
    } else {
      appendToCurrent(s_rxBuf, (uint32_t)readLen);
    }
  }
}
} // namespace

void usbDisplayBegin(TFT_eSPI& tft, UsbDisplayEventHandler handler)
{
  if (s_started.exchange(true)) return;
  s_tft = &tft;
  s_handler = handler;
  buildDeviceName();
  resetReceiver();

  if (!allocateBuffers() || !startRenderTask()) {
    s_started = false;
    return;
  }

  Serial.printf("[usb-display] ready device=%s resolution=%ux%u fps=10 frame_limit=%u\n",
                s_deviceName.c_str(), DISPLAY_WIDTH, DISPLAY_HEIGHT,
                (unsigned)FRAME_LIMIT_BYTES);
  emit(UsbDisplayEvent::Ready, 0, s_deviceName.c_str());
}

bool usbDisplayActive()
{
  return s_active.load();
}

const char* usbDisplayDeviceName()
{
  buildDeviceName();
  return s_deviceName.c_str();
}

extern "C" void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize)
{
  (void)buffer;
  (void)bufsize;
  receiveVendorData(itf);
}

#else

#include <Arduino.h>

namespace {
String s_deviceName;
}

void usbDisplayBegin(TFT_eSPI&, UsbDisplayEventHandler) {}

bool usbDisplayActive()
{
  return false;
}

const char* usbDisplayDeviceName()
{
  if (s_deviceName.length() == 0) {
    const uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
    char name[32];
    snprintf(name, sizeof(name), "Muse Radio-%04X", (unsigned)suffix);
    s_deviceName = name;
  }
  return s_deviceName.c_str();
}

#endif
