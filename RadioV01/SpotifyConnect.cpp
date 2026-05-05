#include "SpotifyConnect.h"

#if ENABLE_SPOTIFY_CONNECT

#include <WiFi.h>
#include <LittleFS.h>
#include <atomic>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include "BellHTTPServer.h"
#include "BellLogger.h"
#include "BellUtils.h"
#include "CSpotContext.h"
#include "LoginBlob.h"
#include "MDNSService.h"
#include "SpircHandler.h"
#include "TrackPlayer.h"
#include "civetweb.h"
#include "esp_heap_caps.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef CSPOT_ZEROCONF_PORT
#define CSPOT_ZEROCONF_PORT 8080
#endif

namespace {
constexpr const char* AUTH_CACHE_PATH = "/spotify_auth.json";
constexpr uint32_t SAMPLE_RATE = 44100;
constexpr uint32_t PCM_START_TIMEOUT_MS = 15000;
constexpr uint8_t CHANNELS = 2;

Audio* s_audio = nullptr;
SpotifyConnectEventHandler s_handler = nullptr;
TaskHandle_t s_task = nullptr;
std::atomic<bool> s_started(false);
std::atomic<bool> s_active(false);
std::atomic<bool> s_pcmPending(false);
std::atomic<uint32_t> s_pcmPendingSinceMs(0);
std::shared_ptr<cspot::SpircHandler> s_spirc;
std::unique_ptr<bell::MDNSService> s_mdnsService;
String s_deviceName;

void unregisterSpotifyMDNS() {
  if (!s_mdnsService) {
    return;
  }

  s_mdnsService->unregisterService();
  s_mdnsService.reset();
}

void emit(SpotifyConnectEvent event, uint32_t value = 0, const char* text = nullptr) {
  if (s_handler) {
    s_handler(event, value, text);
  }
}

void emitActive(const char* text = "Spotify Connect") {
  if (!s_active.exchange(true)) {
    emit(SpotifyConnectEvent::Active, 0, text);
  }
}

void markPlaybackRequested() {
  s_pcmPending = true;
  s_pcmPendingSinceMs = millis();
  emitActive();
}

void markInactive(const char* text = "Radio resumes") {
  s_pcmPending = false;
  s_active = false;
  emit(SpotifyConnectEvent::Inactive, 0, text);
}

void checkPcmStartTimeout() {
  if (!s_pcmPending.load()) {
    return;
  }

  const uint32_t pendingMs = millis() - s_pcmPendingSinceMs.load();
  if (pendingMs < PCM_START_TIMEOUT_MS) {
    return;
  }

  s_pcmPending = false;
  if (s_active.exchange(false)) {
    emit(SpotifyConnectEvent::Inactive, 0, "Spotify stream unavailable");
  }
}

std::string deviceName() {
  if (s_deviceName.length() == 0) {
    const uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
    char name[24];
    snprintf(name, sizeof(name), "Muse Radio-%04X", (unsigned)suffix);
    s_deviceName = name;
  }
  return std::string(s_deviceName.c_str());
}

std::string hostName() {
  const uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  char host[24];
  snprintf(host, sizeof(host), "museradio-%04x", (unsigned)suffix);
  return std::string(host);
}

bool readAuthCache(std::string& json) {
  File file = LittleFS.open(AUTH_CACHE_PATH, FILE_READ);
  if (!file) {
    return false;
  }

  const size_t size = file.size();
  json.resize(size);
  const size_t read = file.readBytes(json.data(), size);
  file.close();
  if (read != size) {
    json.clear();
    return false;
  }

  return !json.empty();
}

void writeAuthCache(const std::string& json) {
  File file = LittleFS.open(AUTH_CACHE_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("[spotify] cannot open auth cache for write");
    return;
  }

  file.write((const uint8_t*)json.data(), json.size());
  file.close();
}

size_t writePCM(uint8_t* data, size_t bytes, std::string_view) {
  if (!s_audio || !data || bytes == 0) {
    return 0;
  }

  emitActive();
  s_pcmPending = false;

  return s_audio->writeRawPCM16(data, bytes, SAMPLE_RATE, CHANNELS);
}

void handleCSpotEvent(std::unique_ptr<cspot::SpircHandler::Event> event) {
  if (!event) {
    return;
  }

  using EventType = cspot::SpircHandler::EventType;
  switch (event->eventType) {
    case EventType::PLAYBACK_START:
      markPlaybackRequested();
      break;
    case EventType::PLAY_PAUSE:
      if (std::holds_alternative<bool>(event->data) && std::get<bool>(event->data)) {
        s_pcmPending = false;
        emit(SpotifyConnectEvent::Paused, 0, "Spotify paused");
      } else {
        markPlaybackRequested();
      }
      break;
    case EventType::VOLUME:
      if (std::holds_alternative<int>(event->data)) {
        emit(SpotifyConnectEvent::Volume, (uint32_t)std::get<int>(event->data), nullptr);
      }
      break;
    case EventType::DISC:
    case EventType::DEPLETED:
      markInactive();
      break;
    case EventType::FLUSH:
    case EventType::SEEK:
      break;
    default:
      break;
  }
}

bool runSession(std::shared_ptr<cspot::LoginBlob> blob, bool persistAuth) {
  try {
    Serial.println("[spotify] connecting session");
    auto ctx = cspot::Context::createFromBlob(blob);
    ctx->session->connectWithRandomAp();
    auto token = ctx->session->authenticate(blob);
    if (token.empty()) {
      Serial.println("[spotify] authentication failed");
      return false;
    }

    if (persistAuth) {
      writeAuthCache(ctx->getCredentialsJson());
      Serial.println("[spotify] auth cache saved");
    }

    s_active = false;
    s_pcmPending = false;
    ctx->session->startTask();
    s_spirc = std::make_shared<cspot::SpircHandler>(ctx);
    s_spirc->setEventHandler(handleCSpotEvent);
    s_spirc->getTrackPlayer()->setDataCallback(writePCM);
    emit(SpotifyConnectEvent::Ready, 0, deviceName().c_str());

    while (true) {
      ctx->session->handlePacket();
      checkPcmStartTimeout();
      vTaskDelay(1);
    }
  } catch (const std::exception& e) {
    Serial.printf("[spotify] session exception: %s\n", e.what());
  } catch (...) {
    Serial.println("[spotify] session exception");
  }

  markInactive();
  s_spirc.reset();
  return false;
}

std::shared_ptr<cspot::LoginBlob> waitForZeroconfBlob() {
  Serial.println("[spotify] starting zeroconf pairing");

  unregisterSpotifyMDNS();

  auto blob = std::make_shared<cspot::LoginBlob>(deviceName());
  std::atomic<bool> gotBlob(false);
  auto server = std::make_unique<bell::BellHTTPServer>(CSPOT_ZEROCONF_PORT);

  server->registerGet("/spotify_info", [serverPtr = server.get(), blob](struct mg_connection*) {
    return serverPtr->makeJsonResponse(blob->buildZeroconfInfo());
  });

  server->registerPost("/spotify_info", [serverPtr = server.get(), blob, &gotBlob](struct mg_connection* conn) {
    std::string body;
    const auto* requestInfo = mg_get_request_info(conn);
    if (requestInfo && requestInfo->content_length > 0) {
      body.resize(requestInfo->content_length);
      mg_read(conn, body.data(), requestInfo->content_length);

      mg_header headers[12];
      const int num = mg_split_form_urlencoded(body.data(), headers, 12);
      std::map<std::string, std::string> queryMap;

      for (int i = 0; i < num; i++) {
        queryMap[headers[i].name] = headers[i].value;
      }

      try {
        blob->loadZeroconfQuery(queryMap);
        gotBlob = true;
        return serverPtr->makeJsonResponse("{\"status\":101,\"spotifyError\":0,\"statusString\":\"ERROR-OK\"}");
      } catch (const std::exception& e) {
        Serial.printf("[spotify] zeroconf rejected: %s\n", e.what());
      } catch (...) {
        Serial.println("[spotify] zeroconf rejected");
      }
    }

    return serverPtr->makeJsonResponse("{\"status\":202,\"spotifyError\":1,\"statusString\":\"ERROR-INVALID-BLOB\"}");
  });

  const esp_err_t mdnsErr = mdns_init();
  if (mdnsErr == ESP_OK || mdnsErr == ESP_ERR_INVALID_STATE) {
    mdns_hostname_set(hostName().c_str());
  }

  s_mdnsService = bell::MDNSService::registerService(
      blob->getDeviceName(), "_spotify-connect", "_tcp", "", CSPOT_ZEROCONF_PORT,
      {{"VERSION", "1.0"}, {"CPath", "/spotify_info"}, {"Stack", "SP"}});

  emit(SpotifyConnectEvent::Ready, 0, deviceName().c_str());

  while (!gotBlob.load()) {
    Serial.printf("[spotify] waiting for Spotify app, heap=%u psram=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  Serial.println("[spotify] zeroconf blob received");
  return blob;
}

void spotifyTask(void*) {
  Serial.printf("[spotify] task started, wifi=%d\n", (int)WiFi.status());
  bell::setDefaultLogger();
  Serial.println("[spotify] logger ready");

  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }

  Serial.printf("[spotify] device=%s\n", deviceName().c_str());

  std::string cachedJson;
  if (readAuthCache(cachedJson)) {
    auto cachedBlob = std::make_shared<cspot::LoginBlob>(deviceName());
    try {
      cachedBlob->loadJson(cachedJson);
      if (runSession(cachedBlob, false)) {
        vTaskDelete(nullptr);
        return;
      }
    } catch (const std::exception& e) {
      Serial.printf("[spotify] cached auth invalid: %s\n", e.what());
      LittleFS.remove(AUTH_CACHE_PATH);
    } catch (...) {
      Serial.println("[spotify] cached auth invalid");
      LittleFS.remove(AUTH_CACHE_PATH);
    }
  }

  while (true) {
    auto blob = waitForZeroconfBlob();
    runSession(blob, true);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}
} // namespace

void spotifyConnectBegin(Audio& audio, SpotifyConnectEventHandler handler) {
  if (s_started.exchange(true)) {
    Serial.println("[spotify] begin ignored: already started");
    return;
  }

  s_audio = &audio;
  s_handler = handler;
  deviceName();
  Serial.printf("[spotify] begin: device=%s wifi=%d heap=%u internal=%u psram=%u\n",
                s_deviceName.c_str(),
                (int)WiFi.status(),
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  const BaseType_t rc = xTaskCreatePinnedToCore(spotifyTask, "spotify", 24576, nullptr, 4, &s_task, 1);
  if (rc != pdPASS) {
    Serial.printf("[spotify] task create failed: rc=%ld\n", (long)rc);
    s_task = nullptr;
    s_started = false;
    return;
  }
  Serial.printf("[spotify] task created: handle=%p\n", (void*)s_task);
}

bool spotifyConnectActive() {
  return s_active.load();
}

void spotifyConnectSetLocalVolume(uint8_t volume, uint8_t maxVolume) {
  auto handler = s_spirc;
  if (!handler || maxVolume == 0) {
    return;
  }

  const uint32_t rawVolume = ((uint32_t)volume * 65535UL) / maxVolume;
  handler->setRemoteVolume((int)rawVolume);
}

const char* spotifyConnectDeviceName() {
  deviceName();
  return s_deviceName.c_str();
}

#else

namespace {
String s_deviceName;
}

void spotifyConnectBegin(Audio&, SpotifyConnectEventHandler) {}

bool spotifyConnectActive() {
  return false;
}

void spotifyConnectSetLocalVolume(uint8_t, uint8_t) {}

const char* spotifyConnectDeviceName() {
  if (s_deviceName.length() == 0) {
    const uint32_t suffix = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
    char name[24];
    snprintf(name, sizeof(name), "Muse Radio-%04X", (unsigned)suffix);
    s_deviceName = name;
  }
  return s_deviceName.c_str();
}

#endif
