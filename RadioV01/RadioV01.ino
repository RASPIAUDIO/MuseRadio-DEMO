#include "Arduino.h"
#include <PubSubClient.h>
#include <Audio.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "Free_Fonts.h"
#include <ESP32Encoder.h>
#include <PNGdec.h>
#include <ImprovWiFiLibrary.h>
#include "esp_wifi.h"
#include "qrcodeR.h"
#include <Arduino_ESP32_OTA.h>
#include "root_ca.h"
//#include "USB.h"
//#include <IRremoteESP8266.h>
#include <IRrecv.h>
//#include <IRutils.h>
#include <FS.h>
#include <SD_MMC.h>
#include "lwip/apps/sntp.h"
#if ENABLE_SPOTIFY_CONNECT || ENABLE_AIRPLAY
#include "MuseHTTPClient.h"
#else
#include <HTTPClient.h>
#endif
#include <ArduinoJson.h>
#include "SpotifyConnect.h"
#include "AirPlayService.h"
#include "UsbAudioService.h"
#include "UsbDisplayService.h"
#include "esp32s3/rom/tjpgd.h"
#if ENABLE_SPOTIFY_CONNECT || ENABLE_AIRPLAY
#include "esp_heap_caps.h"
#endif
#if ENABLE_SPOTIFY_CONNECT
#include "freertos/queue.h"
#endif

#ifndef ENABLE_LEGACY_FACTORY_I2S
#define ENABLE_LEGACY_FACTORY_I2S 0
#endif

#ifndef ENABLE_USB_AUDIO
#define ENABLE_USB_AUDIO 0
#endif

#ifndef ENABLE_USB_DISPLAY
#define ENABLE_USB_DISPLAY 0
#endif

// Provide a portable console alias: use USB CDC when available, else UART Serial
// On ESP32-S2/S3 with "USB CDC On Boot" enabled, the core exposes `USBSerial`.
// For other boards/configs, fall back to `Serial`.
#if !defined(ARDUINO_USB_CDC_ON_BOOT) || !ARDUINO_USB_CDC_ON_BOOT
#define USBSerial Serial
#endif

PNG png;
#define MAX_IMAGE_WDITH 320 // Adjust for your images




#ifndef MUSE_FIRMWARE_VERSION
#define MUSE_FIRMWARE_VERSION "V1.7"
#endif

#ifndef MUSE_IMPROV_VERSION
#define MUSE_IMPROV_VERSION "1.7"
#endif

#define version MUSE_FIRMWARE_VERSION

#define I2S_DOUT        17
#define I2S_BCLK        5
#define I2S_LRC         16
#define I2S_DIN         4
#define I2S_MCLK        0
#define I2CN (i2c_port_t)0
#define SDA 18
#define SCL 11
#define JACK_DETECT     GPIO_NUM_10
#define USB_DETECT      GPIO_NUM_21
#define EN_4G           GPIO_NUM_38
#define IR              GPIO_NUM_47
#define BAT_GAUGE_PIN   13
#define ENC_A2          6
#define ENC_B2          7
#define ENC_A1          42
#define ENC_B1          3
//Digital buttons
#define CLICK2      GPIO_NUM_45
#define CLICK1      GPIO_NUM_48
#define PA GPIO_NUM_46
#define backLight   GPIO_NUM_41

#define MAX_WAIT_TIME 10000

//Analog Buttons
#define KEYs_ADC        1
#define Mute_key        1
#define OK_key          4
#define LEFT_key        5
#define VolP_key        2
#define VolM_key        0
#define RIGHT_key       3

#define sw0             0
#define sw1             1
#define sw2             2
#define sw3             3
#define ADC_BUTTON_DEBUG 0
#define ADC_BUTTON_THRESHOLD 250
static const int ADC_BUTTON_REFERENCE[] = {1863, 2355, 486, 930};
static const int ADC_BUTTON_COUNT = sizeof(ADC_BUTTON_REFERENCE) / sizeof(ADC_BUTTON_REFERENCE[0]);

static char const OTA_FILE_LOCATION[] = "https://raw.githubusercontent.com/RASPIAUDIO/ota/main/RadioLast.ota";

TaskHandle_t radioH, keybH, batteryH, jackH, remoteH, displayONOFFH, improvWiFiInitH;
static TaskHandle_t captivePortalTaskH = nullptr;
static volatile bool captivePortalTaskActive = false;


Arduino_ESP32_OTA ota;
Arduino_ESP32_OTA::Error ota_err = Arduino_ESP32_OTA::Error::None;




WiFiClient espClient;
PubSubClient client(espClient);

time_t now;
struct tm timeinfo;
char timeStr[60];
Audio audio;
static void audioEvent(Audio::msg_t msg);
void settings(void);
int button_get_level(int nb);
static void debugAdcButtonsTick();
WiFiMulti wifiMulti;              //////////////////////////////////////
#define maxVol 31
#define pos360 31
int vol = maxVol;
int Pvol;
uint8_t mode;
uint8_t ssid[80];
uint8_t pwd[80];

// ---- Multi WiFi credentials support ----
#define WIFI_JSON_PATH "/wifi.json"
#define WIFI_MAX_NETWORKS 10
#define RADIO_PRESET_MAX 99
#define RADIO_NAME_MAX 48
#define RADIO_URL_MAX 192
#define RADIO_CATALOG_PATH "/radio_catalog.csv"
#define RADIO_NAME_PATH "/nameS"
#define RADIO_LINK_PATH "/linkS"
struct WifiCred { char ssid[33]; char pwd[65]; };

// Load stored WiFi credentials from LittleFS JSON into an array.
// Returns count loaded. Backward-compatible: if only /ssid and /pwd exist,
// migrate them into JSON on first run.
int loadWifiList(WifiCred list[], int maxItems)
{
  int count = 0;
  File f = LittleFS.open(WIFI_JSON_PATH, FILE_READ);
  if (f) {
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (!err && doc.containsKey("networks") && doc["networks"].is<JsonArray>()) {
      for (JsonObject obj : doc["networks"].as<JsonArray>()) {
        if (count >= maxItems) break;
        const char* s = obj["ssid"] | "";
        const char* p = obj["pwd"] | "";
        strncpy(list[count].ssid, s, sizeof(list[count].ssid));
        list[count].ssid[sizeof(list[count].ssid)-1] = 0;
        strncpy(list[count].pwd, p, sizeof(list[count].pwd));
        list[count].pwd[sizeof(list[count].pwd)-1] = 0;
        count++;
      }
      return count;
    }
  }

  // Backward-compat: migrate single /ssid + /pwd if present
  File sFile = LittleFS.open("/ssid", FILE_READ);
  File pFile = LittleFS.open("/pwd", FILE_READ);
  if (sFile && pFile) {
    size_t sn = sFile.readBytes((char*)ssid, sizeof(ssid)-1); ((char*)ssid)[sn] = 0;
    size_t pn = pFile.readBytes((char*)pwd, sizeof(pwd)-1); ((char*)pwd)[pn] = 0;
    sFile.close(); pFile.close();
    if (sn > 0 && pn >= 0) {
      strncpy(list[0].ssid, (char*)ssid, sizeof(list[0].ssid)); list[0].ssid[sizeof(list[0].ssid)-1]=0;
      strncpy(list[0].pwd, (char*)pwd, sizeof(list[0].pwd)); list[0].pwd[sizeof(list[0].pwd)-1]=0;
      count = 1;
    }
  }
  return count;
}

void saveWifiList(const WifiCred list[], int count)
{
  DynamicJsonDocument doc(3072);
  JsonArray arr = doc.createNestedArray("networks");
  for (int i = 0; i < count; i++) {
    JsonObject o = arr.createNestedObject();
    o["ssid"] = list[i].ssid;
    o["pwd"]  = list[i].pwd;
  }
  File f = LittleFS.open(WIFI_JSON_PATH, FILE_WRITE);
  if (f) {
    serializeJson(doc, f);
    f.close();
  }
}

int indexOfSsid(const WifiCred list[], int count, const char* target)
{
  for (int i = 0; i < count; i++) if (strcmp(list[i].ssid, target) == 0) return i;
  return -1;
}

// Adds new credential or updates password if SSID already exists.
int addOrUpdateWifi(WifiCred list[], int count, const char* s, const char* p)
{
  int idx = indexOfSsid(list, count, s);
  if (idx >= 0) {
    strncpy(list[idx].pwd, p, sizeof(list[idx].pwd));
    list[idx].pwd[sizeof(list[idx].pwd)-1] = 0;
    return count;
  }
  if (count < WIFI_MAX_NETWORKS) {
    strncpy(list[count].ssid, s, sizeof(list[count].ssid));
    list[count].ssid[sizeof(list[count].ssid)-1] = 0;
    strncpy(list[count].pwd, p, sizeof(list[count].pwd));
    list[count].pwd[sizeof(list[count].pwd)-1] = 0;
    return count + 1;
  }
  return count; // full, ignore
}

bool removeWifiByIndex(WifiCred list[], int &count, int idx)
{
  if (idx < 0 || idx >= count) return false;
  for (int i = idx; i < count - 1; i++) list[i] = list[i+1];
  count--;
  return true;
}

void wifiMultiFromList(WiFiMulti &wm, const WifiCred list[], int count)
{
  for (int i = 0; i < count; i++) {
    wm.addAP(list[i].ssid, list[i].pwd);
  }
}
char qrData[120];
char c[2];
int PL;
int PPL = -1;
File ln;
char b[8];
char s[80];
bool started = false;
uint32_t lastModTime;
bool modSta = false;
bool jackON = false;
bool Bvalid = false;
bool BOTA = false;
bool BSettings = false;
int toDisplay = 0;
int toDisplayP;
bool Bdonate = false;
#define TEMPO 30000
int displayT = TEMPO;

IRrecv irrecv(IR);
decode_results results;


void displayON(void)
{
  displayT = TEMPO;
  gpio_set_level(backLight, 1);
}

#define ES8388_ADDR 0x10
///////////////////////////////////////////////////////////////////////
// Write ES8388 register (using I2c)
///////////////////////////////////////////////////////////////////////
uint8_t ES8388_Write_Reg(uint8_t reg, uint8_t val)
{
  uint8_t buf[2], res;
  buf[0] = reg;
  buf[1] = val;
  Wire.beginTransmission(ES8388_ADDR);
  Wire.write(buf, 2);
  res = Wire.endTransmission();
  return res;
}

bool ES8388_Read_Reg(uint8_t reg, uint8_t *val)
{
  if (!val) return false;
  Wire.beginTransmission(ES8388_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint16_t)ES8388_ADDR, (uint8_t)1, true) != 1) return false;
  *val = Wire.read();
  return true;
}

void ES8388_Dump_ADC_Regs()
{
#if ENABLE_USB_MIC
  printf("ES8388 ADC regs:");
  for (uint8_t reg = 0x09; reg <= 0x16; reg++) {
    uint8_t val = 0;
    if (ES8388_Read_Reg(reg, &val)) {
      printf(" %02X=%02X", reg, val);
    } else {
      printf(" %02X=??", reg);
    }
  }
  printf("\n");
#endif
}

//////////////////////////////////////////////////////////////////
//
// init CODEC chip ES8388 (via I2C)
//
////////////////////////////////////////////////////////////////////
//
int ES8388_Init(void)
{
  // provides MCLK
  //    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
  //    WRITE_PERI_REG(PIN_CTRL, READ_PERI_REG(PIN_CTRL)& 0xFFFFFFF0);
  int st;
  st = 0;
  // reset
  st += ES8388_Write_Reg(0, 0x80);
  st += ES8388_Write_Reg(0, 0x00);
  // mute
  st += ES8388_Write_Reg(25, 0x04);
  st += ES8388_Write_Reg(1, 0x50);
  //powerup
  st += ES8388_Write_Reg(2, 0x00);
  // slave mode
  st += ES8388_Write_Reg(8, 0x00);
  // DAC powerdown
  st += ES8388_Write_Reg(4, 0xC0);
  // vmidsel/500k ADC/DAC idem
  st += ES8388_Write_Reg(0, 0x12);

  st += ES8388_Write_Reg(1, 0x00);
  // i2s 16 bits
  st += ES8388_Write_Reg(23, 0x18);
  // sample freq 256
  st += ES8388_Write_Reg(24, 0x02);
  // LIN2/RIN2 for mixer
  st += ES8388_Write_Reg(38, 0x09);
  // left DAC to left mixer
  st += ES8388_Write_Reg(39, 0x80);
  // right DAC to right mixer
  st += ES8388_Write_Reg(42, 0x80);
  // DACLRC ADCLRC idem
  st += ES8388_Write_Reg(43, 0x80);
  st += ES8388_Write_Reg(45, 0x00);
  // DAC volume max
  st += ES8388_Write_Reg(27, 0x00);
  st += ES8388_Write_Reg(26, 0x00);


  //mono (L+R)/2
  st += ES8388_Write_Reg(29, 0x20);

  // DAC power-up LOUT1/ROUT1 ET 2 enabled
  st += ES8388_Write_Reg(4, 0x3C);

  // DAC R phase inversion
  st += ES8388_Write_Reg(28, 0x10);


  // ADC poweroff
  ES8388_Write_Reg(3, 0xFF);


  // ADC PGA +21 dB. The +24 dB setting worked but raised the mic noise floor.
  ES8388_Write_Reg(9, 0x77);


  // Two differential microphone pairs: LADC = LIN1-RIN1, RADC = LIN2-RIN2.
  ES8388_Write_Reg(10, 0xFC);
  ES8388_Write_Reg(11, 0x02);


  //Select LIN2and RIN2 as differential input pairs
  //ES8388_Write_Reg(11,0x82);

  // ADC I2S, 32-bit serial slot. USB still exports the top 16 bits as PCM16.
  ES8388_Write_Reg(12, 0x10);
  //MCLK 256
  ES8388_Write_Reg(13, 0x02);
  // Keep ADC high-pass filters enabled and invert RADC polarity. The Muse Radio
  // differential mic pairs are opposite in phase when captured as stereo.
  ES8388_Write_Reg(14, 0x70);

  // ADC Volume LADC volume = 0dB
  ES8388_Write_Reg(16, 0x00);

  // ADC Volume RADC volume = 0dB
  ES8388_Write_Reg(17, 0x00);

  // Lower-noise ALC profile from the ES8388 guide: stereo, max gain +23.5 dB,
  // min gain 0 dB, voice target, fast attack/decay, noise gate enabled.
  ES8388_Write_Reg(0x12, 0xe2);
  ES8388_Write_Reg(0x13, 0xa0);
  ES8388_Write_Reg(0x14, 0x12);
  ES8388_Write_Reg(0x15, 0x06);
  ES8388_Write_Reg(0x16, 0xc3);
  ES8388_Write_Reg( 0x02, 0x55); // Reg 0x16 = 0x55 (Start up DLL, STM and Digital block for recording);

  // ES8388_Write_Reg(3, 0x09);
  ES8388_Write_Reg(3, 0x00);

  // reset power DAC and ADC
  st += ES8388_Write_Reg(2 , 0xF0);
  st += ES8388_Write_Reg(2 , 0x00);

  // unmute
  st += ES8388_Write_Reg(25, 0x00);
  // amp validation
  gpio_set_level(PA, 1);
  st += ES8388_Write_Reg(46, 15);
  st += ES8388_Write_Reg(47, 15);
  st += ES8388_Write_Reg(48, 33);
  st += ES8388_Write_Reg(49, 33);
  ES8388_Dump_ADC_Regs();
  return st;

}


int delay1 = 10;
int delay2 = 500;

int station = 0;
int previousStation;
int MS;
bool connected = true;
static volatile bool spotifyPlaybackActive = false;
static volatile uint32_t spotifyRadioResumeAtMs = 0;
static String spotifyTrackTitle;
static String spotifyTrackArtist;
static String spotifyTrackCoverUrl;
static volatile bool airPlayPlaybackActive = false;
static volatile bool airPlayVolumeUpdateFromRemote = false;
static volatile uint32_t airPlayRadioResumeAtMs = 0;
static String airPlayTrackTitle;
static String airPlayTrackArtist;
static String airPlayTrackAlbum;
static volatile bool usbAudioPlaybackActive = false;
static volatile uint32_t usbAudioRadioResumeAtMs = 0;
static volatile bool usbAudioVolumeUpdateFromHost = false;
static volatile bool usbDisplayPlaybackActive = false;
static volatile uint32_t usbDisplayRadioResumeAtMs = 0;
static bool littleFsMounted = false;
#if ENABLE_SPOTIFY_CONNECT
static const int SPOTIFY_COVER_X = 12;
static const int SPOTIFY_COVER_Y = 92;
static const int SPOTIFY_COVER_SIZE = 88;
static const size_t SPOTIFY_COVER_JPEG_MAX = 64 * 1024;
static const uint32_t SPOTIFY_COVER_DELAY_MS = 8000;
struct SpotifyCoverRequest {
  char url[128];
  uint32_t generation;
};
static String spotifyCoverPendingUrl;
static volatile uint32_t spotifyCoverRequestDueMs = 0;
static QueueHandle_t spotifyCoverQueue = nullptr;
static TaskHandle_t spotifyCoverTaskH = nullptr;
static uint16_t* spotifyCoverPixels = nullptr;
static volatile uint32_t spotifyCoverGeneration = 0;
static volatile bool spotifyCoverPixelsReady = false;
static volatile bool spotifyCoverNeedsDraw = false;
#endif
char* linkS;
char mes[200];
uint32_t sampleRate;
int iMes ;
int32_t V;
int32_t PV;
int32_t S;
int32_t VS;
int N;
bool CLICK1E    = false;
bool CLICK2E    = false;
bool Mute_keyE    = false;
bool muteB = false;
bool muteON = false;
#define Mute_keyB muteB
#define Mute_keyON muteON
#define CLICK1ON muteON
#define CLICK1B muteB
uint16_t REMOTE_KEY;
bool OK_keyE = false;
bool VALB = false;
#define CLICK2B VALB
#define OK_keyB VALB
bool jaugeB = false;
static volatile uint32_t adcButtonEvents = 0;

// remote keys
#define RET_rem       0x906F
#define MUTE_rem      0x807F
#define OK_rem        0x609F
#define UP_rem        0xA05F
#define DOWN_rem      0x20DF
#define LEFT_rem      0x40BF
#define RIGHT_rem     0x50AF
#define MAIN_rem      0xC03F
#define LIKE_rem      0xD02F
#define VOLP_rem      0x02FD
#define VOLM_rem      0x12ED
#define TRASH_rem     0x22DD


#define MAX_IMAGE_WDITH 320
#define TFT_GREY 0x5AEB
#define TFT_STATION 0x2BFF

TFT_eSPI tft = TFT_eSPI();

DNSServer captiveDns;
WebServer captiveServer(80);

ESP32Encoder volEncoder;
ESP32Encoder staEncoder;


static void forceTftPowerOn()
{
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif
#if defined(TFT_RST) && (TFT_RST >= 0)
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH);
  delay(5);
  digitalWrite(TFT_RST, LOW);
  delay(20);
  digitalWrite(TFT_RST, HIGH);
  delay(150);
#endif
}

static void wakeTftBacklight()
{
  displayON();
  gpio_set_level(backLight, 1);
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif
}




////////////////////////////////////////////////////////////////////////
//
// manages volume (via vol xOUT1, vol DAC, and vol xIN2)
//
////////////////////////////////////////////////////////////////////////

void ES8388vol_Set(uint8_t volx)
{
  if (volx > maxVol) volx = maxVol;
  const bool muted = (volx == 0);
  const uint8_t codecVol = muted ? 0 : (uint8_t)(volx + 2);

  ES8388_Write_Reg(25, muted ? 0x04 : 0x00);
  audio.setVolume(maxVol);
  if (jackON == true)
  {
    // LOUT2/ROUT2
    ES8388_Write_Reg(46, 0);
    ES8388_Write_Reg(47, 0);
    ES8388_Write_Reg(48, codecVol);
    ES8388_Write_Reg(49, codecVol);
  }
  else
  {
    // ROUT1/LOUT1
    ES8388_Write_Reg(48, 0);
    ES8388_Write_Reg(49, 0);
    ES8388_Write_Reg(46, codecVol);
    ES8388_Write_Reg(47, codecVol);
  }
  // RDAC/LDAC (digital)
  ES8388_Write_Reg(26, 0x00);
  ES8388_Write_Reg(27, 0x00);
  ES8388_Write_Reg(25, muted ? 0x04 : 0x00);

}
//////////////////////////////////////////////////////////////////////////
// Print the header for a display screen
//////////////////////////////////////////////////////////////////////////
void headerS(const char *string, uint16_t color)
{
  tft.fillScreen(color);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.fillRect(0, 0, 320, 36, TFT_BLUE);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(string, 160, 10, 4);
}
void headerL(const char *string1, const char *string2, uint16_t color)
{
  tft.fillScreen(color);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE
                   , TFT_BLUE);
  tft.fillRect(0, 0, 320, 60, TFT_BLUE);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(string1, 160, 10, 4);
  tft.drawString(string2, 160, 40, 2);
}

////////////////////////////////////////////////////////////////////////
// Draw the WiFi password and wrap it on two lines if needed
////////////////////////////////////////////////////////////////////////
static void drawPassword(const char *password)
{
  constexpr int MAX_PER_LINE = 16;
  int rectHeight = 30;
  if (strlen(password) > MAX_PER_LINE) rectHeight = 50;

  tft.fillRect(20, 120, 300, rectHeight, TFT_NAVY);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FSB12);
  tft.drawString("pwd:", 20, 120, GFXFF);

  tft.setTextColor(TFT_GREEN);
  if (strlen(password) <= MAX_PER_LINE)
  {
    tft.drawString(password, 80, 120, GFXFF);
  }
  else
  {
    char line1[MAX_PER_LINE + 1];
    strncpy(line1, password, MAX_PER_LINE);
    line1[MAX_PER_LINE] = '\0';
    tft.drawString(line1, 80, 120, GFXFF);
    tft.drawString(password + MAX_PER_LINE, 80, 140, GFXFF);
  }
}

static const byte CAPTIVE_DNS_PORT = 53;
static const char *CAPTIVE_PORTAL_URL = "http://192.168.4.1";
static const char *CAPTIVE_AP_PASSWORD = "museradio";
static IPAddress captivePortalIP(192, 168, 4, 1);
static String captiveApName;
static int captiveScanCount = 0;
static bool captiveRestartPending = false;
static bool captiveManualFallbackRequested = false;

static String htmlEscape(const String &value)
{
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); i++) {
    char ch = value[i];
    switch (ch) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += ch; break;
    }
  }
  return escaped;
}

static String wifiQrEscape(const String &value)
{
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); i++) {
    char ch = value[i];
    if (ch == '\\' || ch == ';' || ch == ',' || ch == ':' || ch == '"') escaped += '\\';
    escaped += ch;
  }
  return escaped;
}

static String captiveWifiQrPayload()
{
  String payload = F("WIFI:T:WPA;S:");
  payload += wifiQrEscape(captiveApName);
  payload += F(";P:");
  payload += wifiQrEscape(CAPTIVE_AP_PASSWORD);
  payload += F(";H:false;;");
  return payload;
}

static const char DEFAULT_RADIO_PRESETS[] =
  "RFI|http://live02.rfi.fr/rfimonde-96k.mp3\n"
  "France Culture|http://direct.franceculture.fr/live/franceculture-hifi.aac\n"
  "FIP|http://direct.fipradio.fr/live/fip-hifi.aac\n"
  "RTL|http://streaming.radio.rtl.fr/rtl-1-44-128?listen=webCwsBCggNCQgLDQUGBAcGBg\n"
  "RMC|https://audio.bfmtv.com/rmcradio_128.mp3\n"
  "France Info|http://direct.franceinfo.fr/live/franceinfo-hifi.aac\n"
  "Radio Classique|http://radioclassique.ice.infomaniak.ch/radioclassique-high.mp3\n"
  "France Musique|http://direct.franceinter.fr/live/francemusique-hifi.aac\n"
  "Radio Crooner|http://croonerradio.ice.infomaniak.ch/croonerradio-hifi.mp3\n"
  "Europe 1|http://ais-live.cloud-services.paris:8000/europe1.mp3\n"
  "Sud Radio|http://start-sud.ice.infomaniak.ch/start-sud-high.mp3\n"
  "France Inter|http://direct.franceinter.fr/live/franceinter-hifi.aac\n"
  "Frequence Jazz|http://broadcast.infomaniak.ch/frequencejazz-high.mp3\n"
  "Le Mouv|http://direct.mouv.fr/live/mouv-midfi.mp3\n"
  "Suisse Premiere|http://stream.srg-ssr.ch/m/la-1ere/mp3_128\n"
  "RTBF|http://broadcast.infomaniak.ch/belrtl-mp3-128.mp3\n"
  "BFM Business|https://audio.bfmtv.com/bfmbusiness_128.mp3\n"
  "Rires et Chansons|http://cdn.nrjaudio.fm/audio1/fr/30401/aac_64.mp3?origine=fluxradios\n";

static const char DEFAULT_RADIO_CATALOG[] =
  "France Inter|General|FR|http://direct.franceinter.fr/live/franceinter-hifi.aac\n"
  "France Info|News|FR|http://direct.franceinfo.fr/live/franceinfo-hifi.aac\n"
  "France Culture|Culture|FR|http://direct.franceculture.fr/live/franceculture-hifi.aac\n"
  "France Musique|Classical|FR|http://direct.franceinter.fr/live/francemusique-hifi.aac\n"
  "FIP|Music|FR|http://direct.fipradio.fr/live/fip-hifi.aac\n"
  "FIP Jazz|Jazz|FR|http://direct.fipradio.fr/live/fip-webradio2.mp3\n"
  "FIP Groove|Funk|FR|http://direct.fipradio.fr/live/fip-webradio3.mp3\n"
  "FIP Rock|Rock|FR|http://direct.fipradio.fr/live/fip-webradio1.mp3\n"
  "Mouv|Hip-hop|FR|http://direct.mouv.fr/live/mouv-midfi.mp3\n"
  "RFI Monde|News|FR|http://live02.rfi.fr/rfimonde-96k.mp3\n"
  "RTL|Talk|FR|http://streaming.radio.rtl.fr/rtl-1-44-128?listen=webCwsBCggNCQgLDQUGBAcGBg\n"
  "Europe 1|Talk|FR|http://ais-live.cloud-services.paris:8000/europe1.mp3\n"
  "RMC|Talk|FR|https://audio.bfmtv.com/rmcradio_128.mp3\n"
  "Sud Radio|Talk|FR|http://start-sud.ice.infomaniak.ch/start-sud-high.mp3\n"
  "BFM Business|News|FR|https://audio.bfmtv.com/bfmbusiness_128.mp3\n"
  "Radio Classique|Classical|FR|http://radioclassique.ice.infomaniak.ch/radioclassique-high.mp3\n"
  "Frequence Jazz|Jazz|FR|http://broadcast.infomaniak.ch/frequencejazz-high.mp3\n"
  "TSF Jazz|Jazz|FR|http://tsfjazz.ice.infomaniak.ch/tsfjazz-high.mp3\n"
  "Radio Nova|Eclectic|FR|http://novazz.ice.infomaniak.ch/novazz-128.mp3\n"
  "Radio Meuh|Eclectic|FR|http://radiomeuh.ice.infomaniak.ch/radiomeuh-128.mp3\n"
  "Radio Crooner|Jazz|FR|http://croonerradio.ice.infomaniak.ch/croonerradio-hifi.mp3\n"
  "Rires et Chansons|Comedy|FR|http://cdn.nrjaudio.fm/audio1/fr/30401/aac_64.mp3?origine=fluxradios\n"
  "Nostalgie|Oldies|FR|http://cdn.nrjaudio.fm/audio1/fr/30601/aac_64.mp3?origine=fluxradios\n"
  "Cherie FM|Pop|FR|http://cdn.nrjaudio.fm/audio1/fr/30201/aac_64.mp3?origine=fluxradios\n"
  "NRJ|Pop|FR|http://cdn.nrjaudio.fm/audio1/fr/30001/aac_64.mp3?origine=fluxradios\n"
  "RTL2|Rock|FR|http://streaming.radio.rtl2.fr/rtl2-1-44-128\n"
  "Fun Radio|Dance|FR|http://streaming.radio.funradio.fr/fun-1-44-128\n"
  "Oui FM|Rock|FR|http://ouifm.ice.infomaniak.ch/ouifm-high.mp3\n"
  "Skyrock|Hip-hop|FR|http://icecast.skyrock.net/s/natio_mp3_128k\n"
  "Latina|Latin|FR|http://start-latina.ice.infomaniak.ch/start-latina-high.mp3\n"
  "FG Radio|Electro|FR|http://radiofg.impek.com/fg\n"
  "Jazz Radio|Jazz|FR|http://jazzradio.ice.infomaniak.ch/jazzradio-high.mp3\n"
  "Classic and Jazz|Classical|FR|http://jazz-wr01.ice.infomaniak.ch/jazz-wr01-128.mp3\n"
  "Suisse La Premiere|General|CH|http://stream.srg-ssr.ch/m/la-1ere/mp3_128\n"
  "RTS Espace 2|Classical|CH|http://stream.srg-ssr.ch/m/espace-2/mp3_128\n"
  "Couleur 3|Rock|CH|http://stream.srg-ssr.ch/m/couleur3/mp3_128\n"
  "RTBF La Premiere|General|BE|http://radios.rtbf.be/laprem1ere-128.mp3\n"
  "RTBF Classic 21|Rock|BE|http://radios.rtbf.be/classic21-128.mp3\n"
  "RTBF Musiq3|Classical|BE|http://radios.rtbf.be/musiq3-128.mp3\n"
  "BBC World Service|News|UK|http://stream.live.vc.bbcmedia.co.uk/bbc_world_service\n"
  "BBC Radio 4|Talk|UK|http://stream.live.vc.bbcmedia.co.uk/bbc_radio_fourfm\n"
  "BBC Radio 6 Music|Alternative|UK|http://stream.live.vc.bbcmedia.co.uk/bbc_6music\n"
  "NPR News|News|US|https://npr-ice.streamguys1.com/live.mp3\n"
  "KEXP|Alternative|US|https://kexp-mp3-128.streamguys1.com/kexp128.mp3\n"
  "KCRW|Eclectic|US|https://kcrw.streamguys1.com/kcrw_192k_mp3_on_air\n"
  "WNYC FM|Talk|US|https://fm939.wnyc.org/wnycfm\n"
  "SomaFM Groove Salad|Ambient|US|https://ice2.somafm.com/groovesalad-128-mp3\n"
  "SomaFM Drone Zone|Ambient|US|https://ice2.somafm.com/dronezone-128-mp3\n"
  "SomaFM Secret Agent|Lounge|US|https://ice2.somafm.com/secretagent-128-mp3\n"
  "Radio Paradise Main|Eclectic|US|https://stream.radioparadise.com/mp3-128\n"
  "Radio Paradise Mellow|Eclectic|US|https://stream.radioparadise.com/mellow-128\n"
  "Radio Paradise Rock|Rock|US|https://stream.radioparadise.com/rock-128\n";

static bool fileMissingOrEmpty(const char *path)
{
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return true;
  bool empty = (f.size() == 0);
  f.close();
  return empty;
}

static bool writeDefaultPresets()
{
  File nameFile = LittleFS.open(RADIO_NAME_PATH, FILE_WRITE);
  File linkFile = LittleFS.open(RADIO_LINK_PATH, FILE_WRITE);
  if (!nameFile || !linkFile) {
    if (nameFile) nameFile.close();
    if (linkFile) linkFile.close();
    return false;
  }

  String data = DEFAULT_RADIO_PRESETS;
  int start = 0;
  int written = 0;
  while (start < (int)data.length() && written < RADIO_PRESET_MAX) {
    int end = data.indexOf('\n', start);
    if (end < 0) end = data.length();
    String line = data.substring(start, end);
    line.trim();
    int sep = line.indexOf('|');
    if (sep > 0) {
      String name = line.substring(0, sep);
      String url = line.substring(sep + 1);
      name.trim();
      url.trim();
      if (url.length()) {
        nameFile.println(name);
        linkFile.println(url);
        written++;
      }
    }
    start = end + 1;
  }

  nameFile.close();
  linkFile.close();
  return written > 0;
}

static bool writeDefaultCatalog()
{
  File f = LittleFS.open(RADIO_CATALOG_PATH, FILE_WRITE);
  if (!f) return false;
  f.write((const uint8_t*)DEFAULT_RADIO_CATALOG, sizeof(DEFAULT_RADIO_CATALOG) - 1);
  f.close();
  return true;
}

static void repairStationUrlPrefixes()
{
  File in = LittleFS.open(RADIO_LINK_PATH, FILE_READ);
  if (!in) return;

  String content;
  content.reserve(in.size() + 8);
  bool changed = false;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.startsWith("ttp://")) {
      line = "h" + line;
      changed = true;
    }
    content += line;
    content += '\n';
  }
  in.close();

  if (changed) {
    File out = LittleFS.open(RADIO_LINK_PATH, FILE_WRITE);
    if (out) {
      out.print(content);
      out.close();
    }
  }
}

static void ensureLocalRadioData()
{
  if (fileMissingOrEmpty(RADIO_NAME_PATH) || fileMissingOrEmpty(RADIO_LINK_PATH)) {
    writeDefaultPresets();
  }
  repairStationUrlPrefixes();
  if (fileMissingOrEmpty(RADIO_CATALOG_PATH)) {
    writeDefaultCatalog();
  }
}

static bool isValidRadioUrl(const String &url)
{
  return url.startsWith("http://") || url.startsWith("https://");
}

static int loadStationPresets(String names[], String urls[], int maxItems)
{
  File nameFile = LittleFS.open(RADIO_NAME_PATH, FILE_READ);
  File linkFile = LittleFS.open(RADIO_LINK_PATH, FILE_READ);
  if (!nameFile || !linkFile) {
    if (nameFile) nameFile.close();
    if (linkFile) linkFile.close();
    return 0;
  }

  int slot = 0;
  int highestUsedSlot = 0;
  while (slot < maxItems && (nameFile.available() || linkFile.available())) {
    names[slot] = nameFile.available() ? nameFile.readStringUntil('\n') : "";
    urls[slot] = linkFile.available() ? linkFile.readStringUntil('\n') : "";
    names[slot].trim();
    urls[slot].trim();
    if (urls[slot].length() > 0) {
      if (names[slot].length() == 0) names[slot] = "Preset " + String(slot + 1);
      highestUsedSlot = slot + 1;
    }
    slot++;
  }

  nameFile.close();
  linkFile.close();
  return highestUsedSlot;
}

static bool saveStationPresets(String names[], String urls[], int count)
{
  if (count < 0) count = 0;
  if (count > RADIO_PRESET_MAX) count = RADIO_PRESET_MAX;
  while (count > 0) {
    urls[count - 1].trim();
    if (urls[count - 1].length() > 0) break;
    count--;
  }
  if (count <= 0) return false;

  File nameFile = LittleFS.open(RADIO_NAME_PATH, FILE_WRITE);
  File linkFile = LittleFS.open(RADIO_LINK_PATH, FILE_WRITE);
  if (!nameFile || !linkFile) {
    if (nameFile) nameFile.close();
    if (linkFile) linkFile.close();
    return false;
  }

  for (int i = 0; i < count; i++) {
    names[i].trim();
    urls[i].trim();
    if (!urls[i].length()) {
      nameFile.println();
      linkFile.println();
    } else {
      if (names[i].length() == 0) names[i] = "Preset " + String(i + 1);
      if (names[i].length() >= RADIO_NAME_MAX) names[i] = names[i].substring(0, RADIO_NAME_MAX - 1);
      if (urls[i].length() >= RADIO_URL_MAX) urls[i] = urls[i].substring(0, RADIO_URL_MAX - 1);
      nameFile.println(names[i]);
      linkFile.println(urls[i]);
    }
  }

  nameFile.close();
  linkFile.close();
  return true;
}

static bool setStationPreset(int slotIndex, String name, String url)
{
  name.trim();
  url.trim();
  if (slotIndex < 0) slotIndex = 0;
  if (slotIndex >= RADIO_PRESET_MAX) slotIndex = RADIO_PRESET_MAX - 1;
  if (!name.length() || !isValidRadioUrl(url)) return false;
  if (name.length() >= RADIO_NAME_MAX) name = name.substring(0, RADIO_NAME_MAX - 1);
  if (url.length() >= RADIO_URL_MAX) return false;

  String names[RADIO_PRESET_MAX];
  String urls[RADIO_PRESET_MAX];
  int count = loadStationPresets(names, urls, RADIO_PRESET_MAX);
  if (slotIndex >= count) count = slotIndex + 1;
  names[slotIndex] = name;
  urls[slotIndex] = url;
  bool saved = saveStationPresets(names, urls, count);
  printf("Radio preset save slot=%d count=%d ok=%d name=%s url=%s\n",
         slotIndex + 1, count, saved ? 1 : 0, name.c_str(), url.c_str());
  return saved;
}

static bool clearStationPreset(int slotIndex)
{
  String names[RADIO_PRESET_MAX];
  String urls[RADIO_PRESET_MAX];
  int count = loadStationPresets(names, urls, RADIO_PRESET_MAX);
  if (slotIndex < 0 || slotIndex >= RADIO_PRESET_MAX) return false;
  if (slotIndex >= count) return false;
  names[slotIndex] = "";
  urls[slotIndex] = "";
  if (count <= 0) return writeDefaultPresets();
  bool saved = saveStationPresets(names, urls, count);
  if (!saved) return writeDefaultPresets();
  printf("Radio preset clear slot=%d count=%d ok=%d\n", slotIndex + 1, count, saved ? 1 : 0);
  return saved;
}

static bool splitCatalogLine(const String &line, String &name, String &category, String &country, String &url)
{
  int p1 = line.indexOf('|');
  int p2 = (p1 >= 0) ? line.indexOf('|', p1 + 1) : -1;
  int p3 = (p2 >= 0) ? line.indexOf('|', p2 + 1) : -1;
  if (p1 <= 0 || p2 <= p1 || p3 <= p2) return false;
  name = line.substring(0, p1);
  category = line.substring(p1 + 1, p2);
  country = line.substring(p2 + 1, p3);
  url = line.substring(p3 + 1);
  name.trim();
  category.trim();
  country.trim();
  url.trim();
  return name.length() && url.length();
}

static void saveWifiCredential(const char *newSsid, const char *newPassword)
{
  if (newSsid == nullptr || newSsid[0] == 0) return;
  if (newPassword == nullptr) newPassword = "";

  uint8_t bm = '1';
  File f = LittleFS.open("/mode", FILE_WRITE);
  if (f) {
    f.write(&bm, 1);
    f.close();
  }

  f = LittleFS.open("/pwd", "w");
  if (f) {
    f.write((const uint8_t*)newPassword, strlen(newPassword) + 1);
    f.close();
  }

  f = LittleFS.open("/ssid", FILE_WRITE);
  if (f) {
    f.write((const uint8_t*)newSsid, strlen(newSsid) + 1);
    f.close();
  }

  WifiCred list[WIFI_MAX_NETWORKS];
  int count = loadWifiList(list, WIFI_MAX_NETWORKS);
  count = addOrUpdateWifi(list, count, newSsid, newPassword);
  saveWifiList(list, count);
}

static void appendPortalMessage(String &html)
{
  String saved = captiveServer.arg("saved");
  if (!saved.length()) return;
  html += F("<div class='notice'>");
  if (saved == "wifi") html += F("Wi-Fi saved. Restart the radio when you are done.");
  else if (saved == "forget") html += F("Network forgotten. Restart the radio when you are done.");
  else if (saved == "radio") html += F("Radio preset saved locally.");
  else if (saved == "clear") html += F("Radio preset removed.");
  else if (saved == "defaults") html += F("Default local presets restored.");
  else html += F("Saved.");
  html += F("</div>");
}

static void appendCatalogOptions(String &html)
{
  File catalog = LittleFS.open(RADIO_CATALOG_PATH, FILE_READ);
  if (!catalog) return;

  int count = 0;
  while (catalog.available() && count < 120) {
    String line = catalog.readStringUntil('\n');
    line.trim();
    String name, category, country, url;
    if (splitCatalogLine(line, name, category, country, url)) {
      String value = name + "|" + url;
      html += F("<option data-cat='");
      html += htmlEscape(category);
      html += F("' value='");
      html += htmlEscape(value);
      html += F("'>");
      html += htmlEscape(name);
      html += F(" - ");
      html += htmlEscape(category);
      html += F(" (");
      html += htmlEscape(country);
      html += F(")</option>");
      count++;
    }
  }
  catalog.close();
}

static String captivePortalPage()
{
  ensureLocalRadioData();
  String html;
  html.reserve(22000);
  html += F("<!doctype html><html><head><meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>Muse Radio Settings</title>");
  html += F("<style>");
  html += F(":root{color-scheme:light;}body{margin:0;font-family:Arial,sans-serif;background:#eef2f7;color:#172033;}main{max-width:980px;margin:0 auto;padding:22px 14px 40px;}h1{font-size:30px;margin:0 0 4px;}h2{font-size:20px;margin:0 0 14px}.lead{margin:0 0 18px;color:#475569}.grid{display:grid;grid-template-columns:1fr;gap:14px}.panel{background:#fff;border:1px solid #d8dee8;border-radius:8px;padding:16px;box-shadow:0 1px 2px #0001}label{display:block;margin:12px 0 5px;font-weight:700;color:#263244}select,input,button{box-sizing:border-box;font-size:16px;border-radius:7px;padding:10px}select,input{width:100%;border:1px solid #aeb8c6;background:#fff;color:#111827}button{border:0;background:#166534;color:white;font-weight:700;cursor:pointer}.ghost{background:#e2e8f0;color:#172033}.danger{background:#b42318}.small{font-size:13px;padding:7px 9px;margin:0}.row{display:flex;gap:8px;align-items:end}.row>*{flex:1}.muted{color:#64748b;font-size:13px}.notice{background:#d1fae5;color:#064e3b;border:1px solid #86efac;border-radius:8px;padding:10px;margin:12px 0}table{width:100%;border-collapse:collapse;font-size:14px}td,th{border-bottom:1px solid #e2e8f0;padding:8px;text-align:left;vertical-align:top}.slot{width:42px;color:#475569}.url{color:#64748b;font-size:12px;word-break:break-all}.actions{width:84px}.restart{margin-top:14px;width:100%;background:#0f172a}.tabs{display:flex;gap:8px;margin:16px 0}.tabs a{flex:1;text-align:center;text-decoration:none;background:#dbeafe;color:#1e3a8a;border-radius:8px;padding:10px;font-weight:700}@media(min-width:760px){.grid{grid-template-columns:1fr 1fr}.wide{grid-column:1/-1}}");
  html += F("</style></head><body><main>");
  html += F("<h1>Muse Radio Settings</h1><p class='lead'>Manage Wi-Fi networks and the local radio presets stored on the Muse.</p>");
  appendPortalMessage(html);
  html += F("<div class='tabs'><a href='#wifi'>Wi-Fi</a><a href='#radio'>Radio presets</a></div>");
  html += F("<div class='grid'>");

  html += F("<section id='wifi' class='panel'><h2>Wi-Fi networks</h2>");
  html += F("<form method='POST' action='/wifi/save'><label for='ssid'>Scanned network</label><select id='ssid' name='ssid'>");
  if (captiveScanCount <= 0) {
    html += F("<option value=''>No network found</option>");
  } else {
    for (int i = 0; i < captiveScanCount; i++) {
      String ssidName = WiFi.SSID(i);
      if (!ssidName.length()) continue;
      String escaped = htmlEscape(ssidName);
      html += F("<option value='");
      html += escaped;
      html += F("'>");
      html += escaped;
      html += F(" (");
      html += String(WiFi.RSSI(i));
      html += F(" dBm");
      if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) html += F(", open");
      html += F(")</option>");
    }
  }
  html += F("</select><label for='ssid_manual'>SSID manual optionnel</label><input id='ssid_manual' name='ssid_manual' maxlength='32' placeholder='Use this if the network is not listed'>");
  html += F("<label for='pwd'>Password</label><input id='pwd' name='pwd' maxlength='64' type='password' autocomplete='current-password'>");
  html += F("<div class='row'><button type='submit'>Save network</button><a href='/rescan'><button class='ghost' type='button'>Scan again</button></a></div></form>");

  WifiCred wifiList[WIFI_MAX_NETWORKS];
  int wifiCount = loadWifiList(wifiList, WIFI_MAX_NETWORKS);
  html += F("<h2 style='margin-top:18px'>Saved networks</h2>");
  if (wifiCount == 0) {
    html += F("<p class='muted'>No saved network yet.</p>");
  } else {
    html += F("<table><tbody>");
    for (int i = 0; i < wifiCount; i++) {
      html += F("<tr><td>");
      html += htmlEscape(String(wifiList[i].ssid));
      html += F("</td><td class='actions'><form method='POST' action='/wifi/forget'><input type='hidden' name='idx' value='");
      html += String(i);
      html += F("'><button class='small danger' type='submit'>Forget</button></form></td></tr>");
    }
    html += F("</tbody></table>");
  }
  html += F("</section>");

  String presetNames[RADIO_PRESET_MAX];
  String presetUrls[RADIO_PRESET_MAX];
  int presetCount = loadStationPresets(presetNames, presetUrls, RADIO_PRESET_MAX);
  int nextSlot = 1;
  while (nextSlot <= RADIO_PRESET_MAX && presetUrls[nextSlot - 1].length() > 0) nextSlot++;
  if (nextSlot > RADIO_PRESET_MAX) nextSlot = RADIO_PRESET_MAX;

  html += F("<section id='radio' class='panel'><h2>Add radio</h2>");
  html += F("<form method='POST' action='/radio/pick'><div class='row'><div><label for='slot_pick'>Preset</label><input id='slot_pick' name='slot' type='number' min='1' max='99' value='");
  html += String(nextSlot);
  html += F("'></div><div><label for='cat'>Theme</label><select id='cat'><option value=''>All</option><option>News</option><option>Talk</option><option>General</option><option>Culture</option><option>Music</option><option>Pop</option><option>Rock</option><option>Jazz</option><option>Classical</option><option>Electro</option><option>Ambient</option><option>Eclectic</option></select></div></div>");
  html += F("<label for='search'>Search</label><input id='search' placeholder='Name, theme, country'><label for='catalog'>Local catalog</label><select id='catalog' name='catalog' size='9'>");
  appendCatalogOptions(html);
  html += F("</select><button type='submit'>Put selected radio in preset</button></form>");
  html += F("<form method='POST' action='/radio/save'><h2 style='margin-top:18px'>Manual radio</h2><div class='row'><div><label for='slot_manual'>Preset</label><input id='slot_manual' name='slot' type='number' min='1' max='99' value='");
  html += String(nextSlot);
  html += F("'></div><div><label for='radio_name'>Name</label><input id='radio_name' name='name' maxlength='47'></div></div><label for='radio_url'>Stream URL</label><input id='radio_url' name='url' maxlength='191' placeholder='https://...'><button type='submit'>Save manual radio</button></form>");
  html += F("</section>");

  html += F("<section class='panel wide'><h2>Current presets</h2>");
  bool hasPreset = false;
  for (int i = 0; i < presetCount; i++) {
    if (presetUrls[i].length() > 0) {
      hasPreset = true;
      break;
    }
  }
  if (!hasPreset) {
    html += F("<p class='muted'>No preset yet.</p>");
  } else {
    html += F("<table><thead><tr><th class='slot'>#</th><th>Radio</th><th class='actions'></th></tr></thead><tbody>");
    for (int i = 0; i < presetCount; i++) {
      if (presetUrls[i].length() == 0) continue;
      html += F("<tr><td class='slot'>");
      html += String(i + 1);
      html += F("</td><td><strong>");
      html += htmlEscape(presetNames[i]);
      html += F("</strong><div class='url'>");
      html += htmlEscape(presetUrls[i]);
      html += F("</div></td><td class='actions'><form method='POST' action='/radio/clear'><input type='hidden' name='slot' value='");
      html += String(i + 1);
      html += F("'><button class='small danger' type='submit'>Clear</button></form></td></tr>");
    }
    html += F("</tbody></table>");
  }
  html += F("<form method='POST' action='/radio/defaults'><button class='ghost' type='submit'>Restore default presets</button></form>");
  html += F("</section></div>");
  html += F("<form method='POST' action='/restart'><button class='restart' type='submit'>Restart radio</button></form>");
  html += F("<script>function f(){const q=document.getElementById('search').value.toLowerCase(),c=document.getElementById('cat').value;for(const o of document.getElementById('catalog').options){const t=o.text.toLowerCase();o.hidden=(c&&o.dataset.cat!==c)||(q&&!t.includes(q));}}document.getElementById('search').oninput=f;document.getElementById('cat').onchange=f;</script>");
  html += F("</main></body></html>");
  return html;
}

static void captiveHandleRoot()
{
  captiveServer.sendHeader("Cache-Control", "no-store");
  captiveServer.send(200, "text/html", captivePortalPage());
}

static void captiveHandleRescan()
{
  WiFi.scanDelete();
  captiveScanCount = WiFi.scanNetworks(false, true);
  captiveServer.sendHeader("Location", "/", true);
  captiveServer.send(302, "text/plain", "");
}

static void portalRedirect(const char *tag)
{
  String location = "/";
  if (tag && tag[0]) {
    location += "?saved=";
    location += tag;
  }
  captiveServer.sendHeader("Location", location, true);
  captiveServer.send(303, "text/plain", "");
}

static void captiveHandleSave()
{
  String selectedSsid = captiveServer.arg("ssid");
  String manualSsid = captiveServer.arg("ssid_manual");
  String password = captiveServer.arg("pwd");
  selectedSsid.trim();
  manualSsid.trim();

  String finalSsid = manualSsid.length() ? manualSsid : selectedSsid;
  if (!finalSsid.length() || finalSsid.length() > 32 || password.length() > 64) {
    captiveServer.send(400, "text/html", F("<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body><h1>Invalid WiFi data</h1><p>SSID must be 1-32 chars and password 0-64 chars.</p><a href='/'>Back</a></body>"));
    return;
  }

  saveWifiCredential(finalSsid.c_str(), password.c_str());
  printf("Portal WiFi saved ssid=%s\n", finalSsid.c_str());
  portalRedirect("wifi");
}

static void captiveHandleWifiForget()
{
  int idx = captiveServer.arg("idx").toInt();
  WifiCred list[WIFI_MAX_NETWORKS];
  int count = loadWifiList(list, WIFI_MAX_NETWORKS);
  if (removeWifiByIndex(list, count, idx)) saveWifiList(list, count);
  portalRedirect("forget");
}

static void captiveHandleRadioManual()
{
  int slot = captiveServer.arg("slot").toInt() - 1;
  String name = captiveServer.arg("name");
  String url = captiveServer.arg("url");
  printf("Portal radio manual slot=%d name=%s url=%s\n", slot + 1, name.c_str(), url.c_str());
  if (!setStationPreset(slot, name, url)) {
    captiveServer.send(400, "text/html", F("<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body><h1>Invalid radio</h1><p>Use a name and an http/https stream URL shorter than 191 chars.</p><a href='/'>Back</a></body>"));
    return;
  }
  portalRedirect("radio");
}

static void captiveHandleRadioPick()
{
  int slot = captiveServer.arg("slot").toInt() - 1;
  String selected = captiveServer.arg("catalog");
  int sep = selected.indexOf('|');
  if (sep <= 0) {
    captiveServer.send(400, "text/html", F("<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body><h1>No radio selected</h1><a href='/'>Back</a></body>"));
    return;
  }
  String name = selected.substring(0, sep);
  String url = selected.substring(sep + 1);
  printf("Portal radio pick slot=%d name=%s url=%s\n", slot + 1, name.c_str(), url.c_str());
  if (!setStationPreset(slot, name, url)) {
    captiveServer.send(400, "text/html", F("<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body><h1>Cannot save radio</h1><a href='/'>Back</a></body>"));
    return;
  }
  portalRedirect("radio");
}

static void captiveHandleRadioClear()
{
  int slot = captiveServer.arg("slot").toInt() - 1;
  clearStationPreset(slot);
  portalRedirect("clear");
}

static void captiveHandleRadioDefaults()
{
  writeDefaultPresets();
  portalRedirect("defaults");
}

static void captiveHandleRestart()
{
  captiveRestartPending = true;
  captiveServer.send(200, "text/html", F("<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body><h1>Restarting</h1><p>The radio will reboot now.</p></body>"));
}

static void captiveHandleNotFound()
{
  captiveServer.sendHeader("Location", CAPTIVE_PORTAL_URL, true);
  captiveServer.send(302, "text/plain", "");
}

static void drawCaptivePortalScreen()
{
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString("Settings portal", 160, 6, 4);
  tft.drawString(("AP: " + captiveApName).c_str(), 160, 34, 2);
  tft.drawString("PWD: museradio", 160, 52, 2);
  tft.drawString("Scan QR: connect WiFi", 160, 70, 2);
  tft.drawString("Portal: 192.168.4.1", 160, 86, 2);

  QRCode qrcode;
  String portalQr = captiveWifiQrPayload();
  printf("Captive WiFi QR payload: %s URL=%s\n", portalQr.c_str(), CAPTIVE_PORTAL_URL);
  uint8_t qrcodeData[qrcode_getBufferSize(4)];
  qrcode_initText(&qrcode, qrcodeData, 4, 0, portalQr.c_str());

  uint16_t scale = 3;
  uint16_t offsetX = (tft.width() - qrcode.size * scale) / 2;
  uint16_t offsetY = 104;

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      uint16_t color = qrcode_getModule(&qrcode, x, y) ? TFT_BLACK : TFT_WHITE;
      tft.fillRect(offsetX + x * scale, offsetY + y * scale, scale, scale, color);
    }
  }

  tft.setTextColor(TFT_GREY, TFT_WHITE);
  tft.drawString("Press knob: manual setup", 160, 222, 2);
}

static bool captiveManualButtonPressed()
{
  return gpio_get_level(CLICK2) == 0;
}

static void waitCaptiveManualButtonReleased()
{
  while (gpio_get_level(CLICK2) == 0) delay(10);
}

static void startWifiCaptivePortal()
{
  const bool displayOwnedByUsbAtStart = usbDisplayPlaybackActive || usbDisplayActive();
  if (!displayOwnedByUsbAtStart) {
    gpio_set_level(backLight, 1);
    forceTftPowerOn();
    tft.init();
  }
  ensureLocalRadioData();
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  delay(200);

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  if (mac.length() < 4) mac = "0000";
  captiveApName = "MuseRadio-" + mac.substring(mac.length() - 4);
  captiveRestartPending = false;
  captiveManualFallbackRequested = false;

  WiFi.softAPConfig(captivePortalIP, captivePortalIP, IPAddress(255, 255, 255, 0));
  bool apStarted = WiFi.softAP(captiveApName.c_str(), CAPTIVE_AP_PASSWORD);
  if (!apStarted) {
    if (!(usbDisplayPlaybackActive || usbDisplayActive())) {
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(TFT_RED);
      tft.drawString("WiFi setup AP failed", 160, 105, 4);
    }
    delay(2000);
    captiveManualFallbackRequested = true;
    return;
  }

  if (!(usbDisplayPlaybackActive || usbDisplayActive())) {
    drawCaptivePortalScreen();
  }
  captiveScanCount = WiFi.scanNetworks(false, true);

  captiveDns.start(CAPTIVE_DNS_PORT, "*", captivePortalIP);
  captiveServer.on("/", HTTP_GET, captiveHandleRoot);
  captiveServer.on("/rescan", HTTP_GET, captiveHandleRescan);
  captiveServer.on("/wifi/save", HTTP_POST, captiveHandleSave);
  captiveServer.on("/wifi/forget", HTTP_POST, captiveHandleWifiForget);
  captiveServer.on("/save", HTTP_POST, captiveHandleSave);
  captiveServer.on("/radio/save", HTTP_POST, captiveHandleRadioManual);
  captiveServer.on("/radio/pick", HTTP_POST, captiveHandleRadioPick);
  captiveServer.on("/radio/clear", HTTP_POST, captiveHandleRadioClear);
  captiveServer.on("/radio/defaults", HTTP_POST, captiveHandleRadioDefaults);
  captiveServer.on("/restart", HTTP_POST, captiveHandleRestart);
  captiveServer.on("/generate_204", HTTP_GET, captiveHandleRoot);
  captiveServer.on("/hotspot-detect.html", HTTP_GET, captiveHandleRoot);
  captiveServer.on("/connecttest.txt", HTTP_GET, captiveHandleRoot);
  captiveServer.on("/ncsi.txt", HTTP_GET, captiveHandleRoot);
  captiveServer.on("/fwlink", HTTP_GET, captiveHandleRoot);
  captiveServer.onNotFound(captiveHandleNotFound);
  captiveServer.begin();

  printf("Captive portal started: SSID=%s PWD=%s URL=%s\n", captiveApName.c_str(), CAPTIVE_AP_PASSWORD, CAPTIVE_PORTAL_URL);

  while (true) {
    captiveDns.processNextRequest();
    captiveServer.handleClient();
    debugAdcButtonsTick();
    if (captiveManualButtonPressed()) {
      waitCaptiveManualButtonReleased();
      captiveServer.stop();
      captiveDns.stop();
      WiFi.softAPdisconnect(true);
      captiveManualFallbackRequested = true;
      return;
    }
    if (captiveRestartPending) {
      if (!(usbDisplayPlaybackActive || usbDisplayActive())) {
        tft.fillScreen(TFT_NAVY);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TFT_GREEN);
        tft.drawString("Restarting...", 160, 105, 4);
      }
      delay(1000);
      esp_restart();
    }
    delay(10);
  }
}

static void captivePortalTask(void*)
{
  captivePortalTaskActive = true;
  startWifiCaptivePortal();
  captivePortalTaskActive = false;
  captivePortalTaskH = nullptr;
  vTaskDelete(nullptr);
}

static void startWifiCaptivePortalBackground()
{
  if (captivePortalTaskH || captivePortalTaskActive) return;
  const BaseType_t rc = xTaskCreatePinnedToCore(captivePortalTask, "wifi_portal", 8192,
                                                nullptr, 2, &captivePortalTaskH, 1);
  if (rc != pdPASS) {
    captivePortalTaskH = nullptr;
    Serial.printf("Captive portal task create failed rc=%ld\n", (long)rc);
  }
}

////////////////////////////////////////////////////////////////////////////////////////
// to change the volume
////////////////////////////////////////////////////////////////////////////////////////
void refreshVolume(void)
{
  // audio.setVolume(vol, 0);
  if (gpio_get_level(JACK_DETECT) == 1)
  {
    if (vol == 0) gpio_set_level(PA, 0);
    else gpio_set_level(PA, 1);
  }
  ES8388vol_Set(vol);
#if ENABLE_USB_AUDIO
  if (!usbAudioVolumeUpdateFromHost)
  {
    usbAudioSetLocalVolume((uint8_t)vol, (uint8_t)maxVol);
    usbAudioSetLocalMute(muteON || vol == 0);
  }
#endif
  if (spotifyPlaybackActive || spotifyConnectActive())
  {
    spotifyConnectSetLocalVolume((uint8_t)vol, (uint8_t)maxVol);
  }
  if ((airPlayPlaybackActive || airPlayActive()) && !airPlayVolumeUpdateFromRemote)
  {
    airPlaySetLocalVolume((uint8_t)vol, (uint8_t)maxVol);
  }
}

static void initAudioHardware()
{
  static bool initialized = false;
  if (initialized) return;
  initialized = true;

  Wire.setPins(SDA, SCL);
  Wire.begin();
  const uint8_t codecStatus = ES8388_Init();
  if (codecStatus == 0) printf("Codec init OK\n"); else printf("Codec init failed\n");

  Audio::audio_info_callback = audioEvent;
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_MCLK, I2S_DIN);
  audio.setVolumeSteps(maxVol + 1);
  audio.setVolume(maxVol);

  if (gpio_get_level(JACK_DETECT) == 0)
  {
    printf("Jack ON\n");
    gpio_set_level(PA, 0);
    ES8388_Write_Reg(29, 0x00);
    ES8388_Write_Reg(28, 0x04);
    ES8388_Write_Reg(4, 0x0C);
    jackON = true;
    refreshVolume();
  }
  else
  {
    printf("Jack OFF\n");
    gpio_set_level(PA, 1);
    ES8388_Write_Reg(29, 0x20);
    ES8388_Write_Reg(28, 0x14);
    ES8388_Write_Reg(4, 0x30);
    jackON = false;
    refreshVolume();
  }
}

static String spotifyMetadataLine(const char* text, uint8_t wantedLine)
{
  if (!text) return "";

  const char* lineStart = text;
  uint8_t currentLine = 0;
  for (const char* p = text; ; ++p)
  {
    if (*p == '\n' || *p == '\0')
    {
      if (currentLine == wantedLine)
      {
        String out;
        out.reserve((uint16_t)(p - lineStart + 1));
        for (const char* q = lineStart; q < p; ++q)
        {
          out += *q;
        }
        out.trim();
        return out;
      }

      if (*p == '\0') break;
      currentLine++;
      lineStart = p + 1;
    }
  }

  return "";
}

static void requestSpotifyCover(const String& url);
static void scheduleSpotifyCover(const String& url);

static void updateSpotifyTrackMetadata(const char* text)
{
  String previousCoverUrl = spotifyTrackCoverUrl;
  spotifyTrackTitle = spotifyMetadataLine(text, 0);
  spotifyTrackArtist = spotifyMetadataLine(text, 1);
  spotifyTrackCoverUrl = spotifyMetadataLine(text, 2);
  Serial.printf("[spotify] cover metadata: %s\n",
                spotifyTrackCoverUrl.length() > 0 ? spotifyTrackCoverUrl.c_str() : "(none)");
  if (spotifyTrackCoverUrl != previousCoverUrl)
  {
    scheduleSpotifyCover(spotifyTrackCoverUrl);
  }
}

static void clearSpotifyTrackMetadata()
{
  spotifyTrackTitle = "";
  spotifyTrackArtist = "";
  spotifyTrackCoverUrl = "";
#if ENABLE_SPOTIFY_CONNECT
  spotifyCoverPendingUrl = "";
  spotifyCoverRequestDueMs = 0;
  requestSpotifyCover("");
#endif
}

#if ENABLE_SPOTIFY_CONNECT
static bool downloadSpotifyCoverJpeg(const char* url, uint8_t** outData, size_t* outLen)
{
  *outData = nullptr;
  *outLen = 0;

  if (!url || !url[0] || WiFi.status() != WL_CONNECTED) return false;

  String requestUrl(url);
  if (requestUrl.startsWith("https://i.scdn.co/"))
  {
    requestUrl.replace("https://", "http://");
  }
  requestUrl.replace("00001e02", "00004851");
  requestUrl.replace("0000b273", "00004851");
  requestUrl.replace("000082c1", "00004851");
  Serial.printf("[spotify] cover request: %s\n", requestUrl.c_str());

  WiFiClient client;

  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, requestUrl))
  {
    Serial.println("[spotify] cover http begin failed");
    return false;
  }

  const int httpCode = http.GET();
  Serial.printf("[spotify] cover http code=%d len=%d\n", httpCode, http.getSize());
  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("[spotify] cover http code=%d\n", httpCode);
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (contentLength <= 0 || (size_t)contentLength > SPOTIFY_COVER_JPEG_MAX)
  {
    Serial.printf("[spotify] cover invalid size=%d\n", contentLength);
    http.end();
    return false;
  }

  uint8_t* data = (uint8_t*)heap_caps_malloc((size_t)contentLength, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!data)
  {
    data = (uint8_t*)malloc((size_t)contentLength);
  }
  if (!data)
  {
    Serial.println("[spotify] cover jpeg alloc failed");
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  int totalRead = 0;
  uint32_t lastProgressMs = millis();

  while (totalRead < contentLength)
  {
    if ((int32_t)(millis() - lastProgressMs) > 10000)
    {
      Serial.println("[spotify] cover download timeout");
      free(data);
      http.end();
      return false;
    }

    int available = stream->available();
    if (available <= 0)
    {
      delay(5);
      continue;
    }

    int chunk = contentLength - totalRead;
    if (chunk > available) chunk = available;
    if (chunk > 1024) chunk = 1024;

    const int readNow = stream->readBytes(data + totalRead, chunk);
    if (readNow > 0)
    {
      totalRead += readNow;
      lastProgressMs = millis();
    }
    else
    {
      delay(5);
    }
  }

  http.end();
  *outData = data;
  *outLen = (size_t)totalRead;
  Serial.printf("[spotify] cover downloaded %u bytes\n", (unsigned)*outLen);
  return true;
}

struct SpotifyJpegDecodeContext
{
  const uint8_t* data;
  size_t len;
  size_t pos;
  uint16_t* tempPixels;
  uint16_t tempWidth;
  uint16_t tempHeight;
};

static UINT spotifyJpegInput(JDEC* decoder, BYTE* buffer, UINT byteCount)
{
  SpotifyJpegDecodeContext* ctx = (SpotifyJpegDecodeContext*)decoder->device;
  if (!ctx) return 0;

  const size_t remaining = ctx->len - ctx->pos;
  UINT toRead = byteCount;
  if (toRead > remaining) toRead = (UINT)remaining;

  if (buffer && toRead > 0)
  {
    memcpy(buffer, ctx->data + ctx->pos, toRead);
  }
  ctx->pos += toRead;
  return toRead;
}

static UINT spotifyJpegOutput(JDEC* decoder, void* bitmap, JRECT* rect)
{
  SpotifyJpegDecodeContext* ctx = (SpotifyJpegDecodeContext*)decoder->device;
  if (!ctx || !bitmap || !rect) return 0;

  const uint16_t blockWidth = rect->right - rect->left + 1;
  const uint16_t blockHeight = rect->bottom - rect->top + 1;
  const uint8_t* src = (const uint8_t*)bitmap;

  for (uint16_t y = 0; y < blockHeight; y++)
  {
    const uint16_t dstY = rect->top + y;
    if (dstY >= ctx->tempHeight) continue;

    for (uint16_t x = 0; x < blockWidth; x++)
    {
      const uint16_t dstX = rect->left + x;
      if (dstX >= ctx->tempWidth) continue;

      const size_t srcIndex = ((size_t)y * blockWidth + x) * 3;
      const uint8_t r = src[srcIndex + 0];
      const uint8_t g = src[srcIndex + 1];
      const uint8_t b = src[srcIndex + 2];
      uint16_t color = ((uint16_t)(r & 0xF8) << 8) |
                       ((uint16_t)(g & 0xFC) << 3) |
                       (uint16_t)(b >> 3);
      color = (uint16_t)((color << 8) | (color >> 8));
      ctx->tempPixels[(size_t)dstY * ctx->tempWidth + dstX] = color;
    }
  }

  return 1;
}

static uint8_t spotifyJpegScaleFor(uint16_t width, uint16_t height)
{
  uint16_t maxDim = width > height ? width : height;
  uint8_t scale = 0;
  while (scale < 3 && (maxDim >> scale) > (SPOTIFY_COVER_SIZE * 2))
  {
    scale++;
  }
  return scale;
}

static bool decodeSpotifyCoverJpeg(const uint8_t* jpegData, size_t jpegLen, uint16_t* scaledPixels)
{
  constexpr size_t WORK_POOL_SIZE = 8192;
  void* workPool = heap_caps_malloc(WORK_POOL_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!workPool)
  {
    workPool = malloc(WORK_POOL_SIZE);
  }
  if (!workPool)
  {
    Serial.println("[spotify] cover work pool alloc failed");
    return false;
  }

  SpotifyJpegDecodeContext ctx = {};
  ctx.data = jpegData;
  ctx.len = jpegLen;

  JDEC decoder = {};
  JRESULT result = jd_prepare(&decoder, spotifyJpegInput, workPool, WORK_POOL_SIZE, &ctx);
  if (result != JDR_OK)
  {
    Serial.printf("[spotify] cover jpeg prepare failed=%d\n", (int)result);
    free(workPool);
    return false;
  }

  const uint8_t scale = spotifyJpegScaleFor(decoder.width, decoder.height);
  ctx.tempWidth = (decoder.width + (1U << scale) - 1U) >> scale;
  ctx.tempHeight = (decoder.height + (1U << scale) - 1U) >> scale;

  const size_t tempPixelsCount = (size_t)ctx.tempWidth * ctx.tempHeight;
  if (tempPixelsCount == 0 || tempPixelsCount > (220U * 220U))
  {
    Serial.printf("[spotify] cover temp size rejected %ux%u\n", ctx.tempWidth, ctx.tempHeight);
    free(workPool);
    return false;
  }

  ctx.tempPixels = (uint16_t*)heap_caps_malloc(tempPixelsCount * sizeof(uint16_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!ctx.tempPixels)
  {
    ctx.tempPixels = (uint16_t*)malloc(tempPixelsCount * sizeof(uint16_t));
  }
  if (!ctx.tempPixels)
  {
    Serial.println("[spotify] cover temp pixels alloc failed");
    free(workPool);
    return false;
  }
  memset(ctx.tempPixels, 0, tempPixelsCount * sizeof(uint16_t));

  result = jd_decomp(&decoder, spotifyJpegOutput, scale);
  free(workPool);

  if (result != JDR_OK)
  {
    Serial.printf("[spotify] cover jpeg decode failed=%d\n", (int)result);
    free(ctx.tempPixels);
    return false;
  }

  for (int y = 0; y < SPOTIFY_COVER_SIZE; y++)
  {
    const uint32_t srcY = ((uint32_t)y * ctx.tempHeight) / SPOTIFY_COVER_SIZE;
    for (int x = 0; x < SPOTIFY_COVER_SIZE; x++)
    {
      const uint32_t srcX = ((uint32_t)x * ctx.tempWidth) / SPOTIFY_COVER_SIZE;
      scaledPixels[y * SPOTIFY_COVER_SIZE + x] =
        ctx.tempPixels[srcY * ctx.tempWidth + srcX];
    }
  }

  Serial.printf("[spotify] cover decoded %ux%u scale=%u -> %dx%d\n",
                decoder.width, decoder.height, scale, SPOTIFY_COVER_SIZE, SPOTIFY_COVER_SIZE);
  free(ctx.tempPixels);
  return true;
}

static void spotifyCoverTask(void*)
{
  SpotifyCoverRequest req = {};
  while (true)
  {
    if (xQueueReceive(spotifyCoverQueue, &req, portMAX_DELAY) != pdTRUE) continue;

    SpotifyCoverRequest latest = {};
    while (xQueueReceive(spotifyCoverQueue, &latest, 0) == pdTRUE)
    {
      req = latest;
    }

    uint16_t* scaledPixels = (uint16_t*)heap_caps_malloc(
      SPOTIFY_COVER_SIZE * SPOTIFY_COVER_SIZE * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!scaledPixels)
    {
      scaledPixels = (uint16_t*)malloc(SPOTIFY_COVER_SIZE * SPOTIFY_COVER_SIZE * sizeof(uint16_t));
    }

    uint8_t* jpegData = nullptr;
    size_t jpegLen = 0;
    bool ok = scaledPixels &&
              downloadSpotifyCoverJpeg(req.url, &jpegData, &jpegLen) &&
              decodeSpotifyCoverJpeg(jpegData, jpegLen, scaledPixels);

    if (jpegData) free(jpegData);

    if (ok && req.generation == spotifyCoverGeneration)
    {
      if (!spotifyCoverPixels)
      {
        spotifyCoverPixels = (uint16_t*)heap_caps_malloc(
          SPOTIFY_COVER_SIZE * SPOTIFY_COVER_SIZE * sizeof(uint16_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!spotifyCoverPixels)
        {
          spotifyCoverPixels = (uint16_t*)malloc(SPOTIFY_COVER_SIZE * SPOTIFY_COVER_SIZE * sizeof(uint16_t));
        }
      }

      if (spotifyCoverPixels)
      {
        memcpy(spotifyCoverPixels, scaledPixels, SPOTIFY_COVER_SIZE * SPOTIFY_COVER_SIZE * sizeof(uint16_t));
        spotifyCoverPixelsReady = true;
        spotifyCoverNeedsDraw = true;
      }
      else
      {
        ok = false;
      }
    }

    if (!ok && req.generation == spotifyCoverGeneration)
    {
      spotifyCoverPixelsReady = false;
      spotifyCoverNeedsDraw = true;
      Serial.println("[spotify] cover unavailable");
    }

    if (scaledPixels) free(scaledPixels);
  }
}

static void requestSpotifyCover(const String& url)
{
  spotifyCoverGeneration++;
  spotifyCoverPixelsReady = false;
  spotifyCoverNeedsDraw = true;

  if (url.length() == 0)
  {
    Serial.println("[spotify] cover request cleared");
    return;
  }

  if (!spotifyCoverQueue)
  {
    spotifyCoverQueue = xQueueCreate(1, sizeof(SpotifyCoverRequest));
  }
  if (!spotifyCoverQueue)
  {
    Serial.println("[spotify] cover queue create failed");
    return;
  }

  if (!spotifyCoverTaskH)
  {
    const BaseType_t rc = xTaskCreatePinnedToCore(
      spotifyCoverTask, "spotifyCover", 12288, nullptr, 1, &spotifyCoverTaskH, 0);
    if (rc != pdPASS)
    {
      Serial.printf("[spotify] cover task create failed rc=%ld\n", (long)rc);
      spotifyCoverTaskH = nullptr;
      return;
    }
  }

  SpotifyCoverRequest req = {};
  url.toCharArray(req.url, sizeof(req.url));
  req.generation = spotifyCoverGeneration;
  Serial.printf("[spotify] cover queued gen=%lu url=%s\n",
                (unsigned long)req.generation, req.url);
  xQueueOverwrite(spotifyCoverQueue, &req);
}

static void scheduleSpotifyCover(const String& url)
{
  spotifyCoverPendingUrl = "";
  spotifyCoverRequestDueMs = 0;

  if (url.length() == 0)
  {
    requestSpotifyCover("");
    return;
  }

  spotifyCoverPixelsReady = false;
  spotifyCoverNeedsDraw = true;
  spotifyCoverPendingUrl = url;
  spotifyCoverRequestDueMs = millis() + SPOTIFY_COVER_DELAY_MS;
  Serial.printf("[spotify] cover scheduled in %lu ms\n",
                (unsigned long)SPOTIFY_COVER_DELAY_MS);
}

static void drawSpotifyCoverPlaceholder()
{
  tft.fillRect(SPOTIFY_COVER_X, SPOTIFY_COVER_Y, SPOTIFY_COVER_SIZE, SPOTIFY_COVER_SIZE, TFT_BLACK);
  tft.drawRect(SPOTIFY_COVER_X, SPOTIFY_COVER_Y, SPOTIFY_COVER_SIZE, SPOTIFY_COVER_SIZE, TFT_GREY);
}

static void drawSpotifyCover()
{
  if (spotifyCoverPixelsReady && spotifyCoverPixels)
  {
    const bool oldSwap = tft.getSwapBytes();
    tft.setSwapBytes(false);
    tft.pushImage(SPOTIFY_COVER_X, SPOTIFY_COVER_Y, SPOTIFY_COVER_SIZE, SPOTIFY_COVER_SIZE, spotifyCoverPixels);
    tft.setSwapBytes(oldSwap);
    tft.drawRect(SPOTIFY_COVER_X, SPOTIFY_COVER_Y, SPOTIFY_COVER_SIZE, SPOTIFY_COVER_SIZE, TFT_GREY);
  }
  else
  {
    drawSpotifyCoverPlaceholder();
  }
  spotifyCoverNeedsDraw = false;
}

static void serviceSpotifyCoverDisplay()
{
  if (spotifyCoverPendingUrl.length() > 0 &&
      spotifyPlaybackActive &&
      spotifyCoverRequestDueMs != 0 &&
      (int32_t)(millis() - spotifyCoverRequestDueMs) >= 0)
  {
    String url = spotifyCoverPendingUrl;
    spotifyCoverPendingUrl = "";
    spotifyCoverRequestDueMs = 0;
    requestSpotifyCover(url);
  }

  if (!spotifyCoverNeedsDraw || Bdonate) return;
  if (!spotifyPlaybackActive && !spotifyConnectActive())
  {
    spotifyCoverNeedsDraw = false;
    return;
  }

  tft.setRotation(1);
  drawSpotifyCover();
}
#else
static void requestSpotifyCover(const String&) {}
static void scheduleSpotifyCover(const String&) {}
static void drawSpotifyCoverPlaceholder() {}
static void serviceSpotifyCoverDisplay() {}
#endif

static String fitSpotifyText(String text, uint8_t font, int maxWidth)
{
  text.trim();
  if (text.length() == 0) return "";
  if (tft.textWidth(text, font) <= maxWidth) return text;

  while (text.length() > 0)
  {
    text.remove(text.length() - 1);
    text.trim();

    String candidate = text;
    candidate += "...";
    if (tft.textWidth(candidate, font) <= maxWidth)
    {
      return candidate;
    }
  }

  return "...";
}

static void drawSpotifyTextLine(const String& text, int y, uint8_t font, uint16_t color)
{
  const bool hasCover = spotifyTrackCoverUrl.length() > 0;
  const int centerX = hasCover ? 212 : 200;
  const int maxWidth = hasCover ? 202 : 232;
  String fitted = fitSpotifyText(text, font, maxWidth);
  if (fitted.length() == 0) return;

  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(fitted, centerX, y, font);
}

static void drawSpotifyStatus(const char* line)
{
  tft.setRotation(1);
  tft.fillRect(8, 78, 312, 112, TFT_BLACK);
#if ENABLE_SPOTIFY_CONNECT
  if (spotifyTrackCoverUrl.length() > 0)
  {
    drawSpotifyCoverPlaceholder();
    if (spotifyCoverPixelsReady) spotifyCoverNeedsDraw = true;
  }
#endif
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Spotify Connect", spotifyTrackCoverUrl.length() > 0 ? 212 : 200, 86, 4);

  const bool hasTrack = spotifyTrackTitle.length() > 0 || spotifyTrackArtist.length() > 0;
  if (hasTrack)
  {
    drawSpotifyTextLine(spotifyTrackTitle.length() > 0 ? spotifyTrackTitle : "Unknown title", 122, 4, TFT_WHITE);
    drawSpotifyTextLine(spotifyTrackArtist.length() > 0 ? spotifyTrackArtist : "Unknown artist", 151, 2, TFT_GREY);

    if (line && strcmp(line, "Spotify paused") == 0)
    {
      drawSpotifyTextLine("Paused", 172, 2, TFT_YELLOW);
    }
  }
  else
  {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    if (line && line[0])
    {
      drawSpotifyTextLine(line, 135, 2, TFT_WHITE);
    }
    else
    {
      drawSpotifyTextLine(spotifyConnectDeviceName(), 135, 2, TFT_WHITE);
    }
  }
}

static void updateAirPlayMetadata(const char* text)
{
  airPlayTrackTitle = spotifyMetadataLine(text, 0);
  airPlayTrackArtist = spotifyMetadataLine(text, 1);
  airPlayTrackAlbum = spotifyMetadataLine(text, 2);
}

static void clearAirPlayMetadata()
{
  airPlayTrackTitle = "";
  airPlayTrackArtist = "";
  airPlayTrackAlbum = "";
}

static void drawAirPlayTextLine(const String& text, int y, uint8_t font, uint16_t color)
{
  String fitted = fitSpotifyText(text, font, 242);
  if (fitted.length() == 0) return;

  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(fitted, 200, y, font);
}

static void drawAirPlayStatus(const char* line)
{
  tft.setRotation(1);
  tft.fillRect(8, 78, 312, 112, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("AirPlay", 200, 86, 4);

  const bool hasTrack = airPlayTrackTitle.length() > 0 || airPlayTrackArtist.length() > 0;
  if (hasTrack)
  {
    drawAirPlayTextLine(airPlayTrackTitle.length() > 0 ? airPlayTrackTitle : "AirPlay", 122, 4, TFT_WHITE);
    drawAirPlayTextLine(airPlayTrackArtist.length() > 0 ? airPlayTrackArtist : "Unknown artist", 151, 2, TFT_GREY);
    if (airPlayTrackAlbum.length() > 0) drawAirPlayTextLine(airPlayTrackAlbum, 172, 2, TFT_SILVER);
  }
  else if (line && line[0])
  {
    drawAirPlayTextLine(line, 135, 2, TFT_WHITE);
  }
  else
  {
    drawAirPlayTextLine(airPlayDeviceName(), 135, 2, TFT_WHITE);
  }
}

#if !ENABLE_USB_DISPLAY
static void drawUsbAudioStatus(const char* line)
{
  tft.setRotation(1);
  tft.fillRect(8, 78, 312, 112, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  tft.drawString("USB Audio", 200, 86, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(line && line[0] ? line : usbAudioDeviceName(), 200, 128, 2);
  tft.setTextColor(TFT_GREY, TFT_BLACK);
  tft.drawString("Windows sound card - 44.1 kHz", 200, 154, 2);
}
#endif

static void handleUsbAudioEvent(UsbAudioEvent event, uint32_t value, const char* text)
{
  switch (event)
  {
    case UsbAudioEvent::Ready:
      printf("[usb-audio] ready: %s\n", text ? text : usbAudioDeviceName());
      break;
    case UsbAudioEvent::Active:
      usbAudioPlaybackActive = true;
      usbAudioRadioResumeAtMs = 0;
      audio.stopSong();
      connected = false;
      refreshVolume();
#if !ENABLE_USB_DISPLAY
      if (!(usbDisplayPlaybackActive || usbDisplayActive()))
      {
        drawUsbAudioStatus(text ? text : usbAudioDeviceName());
      }
#endif
      break;
    case UsbAudioEvent::Inactive:
      usbAudioRadioResumeAtMs = millis() + 1500;
      usbAudioPlaybackActive = false;
      connected = false;
      refreshVolume();
#if !ENABLE_USB_DISPLAY
      if (!(usbDisplayPlaybackActive || usbDisplayActive()))
      {
        drawUsbAudioStatus(text ? text : "Radio resumes...");
      }
#endif
      break;
    case UsbAudioEvent::Volume:
    {
      int newVol = (int)((value * maxVol + 50UL) / 100UL);
      if (newVol < 0) newVol = 0;
      if (newVol > maxVol) newVol = maxVol;
      if (newVol != vol || (newVol > 0 && muteON) || (newVol == 0 && !muteON))
      {
        usbAudioVolumeUpdateFromHost = true;
        if (newVol == 0 && !muteON) {
          Pvol = vol;
          muteON = true;
        } else if (newVol > 0 && muteON) {
          muteON = false;
        }
        vol = newVol;
        V = vol * pos360 / maxVol;
        PV = V;
        volEncoder.setCount(V);
        refreshVolume();
        usbAudioVolumeUpdateFromHost = false;
        toDisplay = 3;
      }
      break;
    }
    case UsbAudioEvent::Mute:
      if (value && !muteON)
      {
        usbAudioVolumeUpdateFromHost = true;
        Pvol = vol;
        vol = 0;
        muteON = true;
        V = 0;
        PV = V;
        volEncoder.setCount(V);
        refreshVolume();
        usbAudioVolumeUpdateFromHost = false;
        toDisplay = 1;
      }
      else if (!value && muteON)
      {
        usbAudioVolumeUpdateFromHost = true;
        vol = Pvol;
        muteON = false;
        V = vol * pos360 / maxVol;
        PV = V;
        volEncoder.setCount(V);
        refreshVolume();
        usbAudioVolumeUpdateFromHost = false;
        toDisplay = 2;
      }
      break;
    default:
      break;
  }
}

static void handleUsbDisplayEvent(UsbDisplayEvent event, uint32_t value, const char* text)
{
  switch (event)
  {
    case UsbDisplayEvent::Ready:
      printf("[usb-display] ready: %s\n", text ? text : usbDisplayDeviceName());
      break;
    case UsbDisplayEvent::Active:
      usbDisplayPlaybackActive = true;
      usbDisplayRadioResumeAtMs = 0;
      audio.stopSong();
      connected = false;
      displayON();
      break;
    case UsbDisplayEvent::Inactive:
      usbDisplayPlaybackActive = true;
      usbDisplayRadioResumeAtMs = 0;
      connected = false;
      displayON();
      break;
    case UsbDisplayEvent::Dropped:
      if ((value % 25) == 1)
      {
        printf("[usb-display] dropped frames=%lu\n", (unsigned long)value);
      }
      break;
    default:
      break;
  }
}

static void handleAirPlayEvent(AirPlayEvent event, uint32_t value, const char* text)
{
  switch (event)
  {
    case AirPlayEvent::Ready:
      printf("[airplay] ready: %s\n", text ? text : airPlayDeviceName());
      break;
    case AirPlayEvent::Active:
      airPlayPlaybackActive = true;
      airPlayRadioResumeAtMs = 0;
      audio.stopSong();
      connected = false;
      refreshVolume();
      drawAirPlayStatus(text ? text : airPlayDeviceName());
      break;
    case AirPlayEvent::Inactive:
      airPlayRadioResumeAtMs = millis() + 1500;
      airPlayPlaybackActive = false;
      connected = false;
      clearAirPlayMetadata();
      refreshVolume();
      drawAirPlayStatus(text ? text : "Radio resumes...");
      break;
    case AirPlayEvent::Metadata:
      updateAirPlayMetadata(text);
      drawAirPlayStatus(nullptr);
      break;
    case AirPlayEvent::Volume:
    {
      int newVol = (int)((value * maxVol + 32767UL) / 65535UL);
      if (newVol < 0) newVol = 0;
      if (newVol > maxVol) newVol = maxVol;
      if (newVol != vol)
      {
        airPlayVolumeUpdateFromRemote = true;
        vol = newVol;
        V = vol * pos360 / maxVol;
        PV = V;
        volEncoder.setCount(V);
        refreshVolume();
        airPlayVolumeUpdateFromRemote = false;
        toDisplay = 3;
      }
      break;
    }
    default:
      break;
  }
}

static void handleSpotifyConnectEvent(SpotifyConnectEvent event, uint32_t value, const char* text)
{
  switch (event)
  {
    case SpotifyConnectEvent::Ready:
      printf("[spotify] ready: %s\n", text ? text : spotifyConnectDeviceName());
      break;
    case SpotifyConnectEvent::Active:
      spotifyPlaybackActive = true;
      spotifyRadioResumeAtMs = 0;
      audio.stopSong();
      connected = false;
      refreshVolume();
      drawSpotifyStatus(text ? text : spotifyConnectDeviceName());
      break;
    case SpotifyConnectEvent::Paused:
      spotifyPlaybackActive = true;
      refreshVolume();
      drawSpotifyStatus(text ? text : "Spotify paused");
      break;
    case SpotifyConnectEvent::Inactive:
      spotifyRadioResumeAtMs = millis() + 1500;
      spotifyPlaybackActive = false;
      connected = false;
      clearSpotifyTrackMetadata();
      refreshVolume();
      drawSpotifyStatus(text ? text : "Radio resumes...");
      break;
    case SpotifyConnectEvent::Track:
      updateSpotifyTrackMetadata(text);
      drawSpotifyStatus(nullptr);
      break;
    case SpotifyConnectEvent::Volume:
    {
      int newVol = (int)((value * maxVol + 32767UL) / 65535UL);
      if (newVol < 0) newVol = 0;
      if (newVol > maxVol) newVol = maxVol;
      if (newVol != vol)
      {
        vol = newVol;
        V = vol * pos360 / maxVol;
        PV = V;
        volEncoder.setCount(V);
        refreshVolume();
        toDisplay = 3;
      }
      break;
    }
    default:
      break;
  }
}

int16_t xpos = 0;
int16_t ypos = 0;

//=========================================v==========================================
//                                      pngDraw
//====================================================================================
// This next function will be called during decoding of the png file to
// render each image line to the TFT.  If you use a different TFT library
// you will need to adapt this function to suit.
// Callback function to draw pixels to the display
void pngDraw(PNGDRAW *pDraw) {
  uint16_t lineBuffer[MAX_IMAGE_WDITH];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(xpos, ypos + pDraw->y, pDraw->iWidth, 1, lineBuffer);
}

void drawImage(char* f, int x, int y)
{
  xpos = x; ypos = y;
  int16_t rc = png.open(f, pngOpen, pngClose, pngRead, pngSeek, pngDraw);
  printf("%s\n", f);
  //  printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
  if (rc == PNG_SUCCESS) {
    tft.startWrite();
    printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
    rc = png.decode(NULL, 0);
    png.close();
    tft.endWrite();
    delay(10);
  }
  printf("drawImage end\n");

}
//////////////////////////////////////////////////////////////////////
//adc buttons
//////////////////////////////////////////////////////////////////////

int button_get_level(int nb)
{
#define maxB 3
  static uint32_t lastAdcDebugMs = 0;
  if ((nb > maxB) || (nb < 0))return -1;
  int adcValue = 0;
  for (int i = 0; i < 4; i++) {
    adcValue += analogRead(KEYs_ADC);
    delayMicroseconds(60);
  }
  adcValue /= 4;

  int nearest = -1;
  int bestDiff = 4096;
  for (int i = 0; i < ADC_BUTTON_COUNT; i++) {
    int diff = abs(ADC_BUTTON_REFERENCE[i] - adcValue);
    if (diff < bestDiff) {
      bestDiff = diff;
      nearest = i;
    }
  }

  if (adcValue < 3300 && (millis() - lastAdcDebugMs) > 250) {
    lastAdcDebugMs = millis();
    printf("ADC keys raw=%d nearest=sw%d diff=%d\n", adcValue, nearest, bestDiff);
  }

  return (nearest == nb && bestDiff < ADC_BUTTON_THRESHOLD) ? 0 : 1;
}

static void initAdcButtons()
{
  pinMode(KEYs_ADC, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(KEYs_ADC, ADC_11db);
  analogRead(KEYs_ADC);
}

static int readAdcButtonsRaw()
{
  int adcValue = 0;
  for (int i = 0; i < 8; i++) {
    adcValue += analogRead(KEYs_ADC);
    delayMicroseconds(60);
  }
  return adcValue / 8;
}

static int nearestAdcButton(int adcValue, int *bestDiff)
{
  int nearest = -1;
  int diffBest = 4096;
  for (int i = 0; i < ADC_BUTTON_COUNT; i++) {
    int diff = abs(ADC_BUTTON_REFERENCE[i] - adcValue);
    if (diff < diffBest) {
      diffBest = diff;
      nearest = i;
    }
  }
  if (bestDiff) *bestDiff = diffBest;
  return nearest;
}

static void debugAdcButtonsTick()
{
#if ADC_BUTTON_DEBUG
  static uint32_t lastAdcDebugMs = 0;
  if ((millis() - lastAdcDebugMs) < 250) return;
  lastAdcDebugMs = millis();
  int adcValue = readAdcButtonsRaw();
  int bestDiff = 0;
  int nearest = nearestAdcButton(adcValue, &bestDiff);
  printf("ADCDBG raw=%d nearest_button=%d diff=%d refs=%d,%d,%d,%d\n",
         adcValue,
         nearest + 1,
         bestDiff,
         ADC_BUTTON_REFERENCE[0],
         ADC_BUTTON_REFERENCE[1],
         ADC_BUTTON_REFERENCE[2],
         ADC_BUTTON_REFERENCE[3]);
#endif
}

static int readAdcButtonIndex(int *rawOut = nullptr, int *diffOut = nullptr)
{
  int adcValue = readAdcButtonsRaw();
  int bestDiff = 0;
  int nearest = nearestAdcButton(adcValue, &bestDiff);
  if (rawOut) *rawOut = adcValue;
  if (diffOut) *diffOut = bestDiff;
  return (nearest >= 0 && bestDiff < ADC_BUTTON_THRESHOLD) ? nearest : -1;
}

static void pushAdcButtonEvent(int index)
{
  if (index < 0 || index >= ADC_BUTTON_COUNT) return;
  adcButtonEvents |= (1UL << index);
}

static bool consumeAdcButtonEvent(int index)
{
  if (index < 0 || index >= ADC_BUTTON_COUNT) return false;
  uint32_t mask = (1UL << index);
  if ((adcButtonEvents & mask) == 0) return false;
  adcButtonEvents &= ~mask;
  return true;
}

///////////////////////////////////////////////////////////////////////
// task managing buttons
//
/////////://///////////////////////////////////////////////////////////
static void keyb(void* pdata)
{
  int adcHeldButton = -1;

  while (1)
  {
    int adcRaw = 0;
    int adcDiff = 0;
    int adcButton = readAdcButtonIndex(&adcRaw, &adcDiff);
    if (adcButton >= 0 && adcHeldButton < 0) {
      pushAdcButtonEvent(adcButton);
      printf("ADC event button=%d raw=%d diff=%d\n", adcButton + 1, adcRaw, adcDiff);
    }
    adcHeldButton = adcButton;

    //   printf("CLICK1 = %d   CLICK2 = %d\n", gpio_get_level(CLICK1), gpio_get_level(CLICK2));
    if ((gpio_get_level(CLICK1) == 0) && (CLICK1B == false)) CLICK1E = true;
    if ((gpio_get_level(CLICK1) == 1) && (CLICK1E == true)) {
      CLICK1B = true;
      CLICK1E = false;
    }

    if ((gpio_get_level(CLICK2) == 0) && (CLICK2B == false)) CLICK2E = true;
    if ((gpio_get_level(CLICK2) == 1) && (CLICK2E == true)) {
      CLICK2B = true;
      CLICK2E = false;
    }

    if (REMOTE_KEY == MUTE_rem) {
      muteB = true;
      REMOTE_KEY = 0;
    }
    if (REMOTE_KEY == OK_rem) {
      CLICK2B = true;
      REMOTE_KEY = 0;
    }
    delay(30);
  }
}


/////////////////////////////////////////////////////////////////////////////
// gets station link from LittleFS file "/linkS"
//
/////////////////////////////////////////////////////////////////////////////
char* Rlink(int st)
{
  static char out[RADIO_URL_MAX];
  out[0] = 0;
  if (st < 0 || st >= RADIO_PRESET_MAX) return out;
  File ln = LittleFS.open(RADIO_LINK_PATH, FILE_READ);
  if (!ln) return out;

  int slot = 0;
  while (ln.available() && slot <= st) {
    String line = ln.readStringUntil('\n');
    line.trim();
    if (slot == st && line.length() > 0) {
      line.toCharArray(out, sizeof(out));
      break;
    }
    slot++;
  }
  ln.close();
  return out;
}
/////////////////////////////////////////////////////////////////////////////////
//  gets station name from LittleFS file "/namS"
//
/////////////////////////////////////////////////////////////////////////////////
char* Rname(int st)
{
  static char out[RADIO_NAME_MAX];
  snprintf(out, sizeof(out), "Preset %d", st + 1);
  if (st < 0 || st >= RADIO_PRESET_MAX) return out;
  File ln = LittleFS.open(RADIO_NAME_PATH, FILE_READ);
  if (!ln) return out;

  int slot = 0;
  while (ln.available() && slot <= st) {
    String line = ln.readStringUntil('\n');
    line.trim();
    if (slot == st && line.length() > 0) {
      line.toCharArray(out, sizeof(out));
      break;
    }
    slot++;
  }
  ln.close();
  return out;
}
/////////////////////////////////////////////////////////////////////////
//  defines how many stations in LittleFS file "/linkS"
//
////////////////////////////////////////////////////////////////////////
int maxStation(void)
{
  File ln = LittleFS.open(RADIO_LINK_PATH, FILE_READ);
  if (!ln) {
    printf("=========> 0 \n");
    return 0;
  }
  int slot = 0;
  int highestUsedSlot = 0;
  while (ln.available() && slot < RADIO_PRESET_MAX) {
    String line = ln.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) highestUsedSlot = slot + 1;
    slot++;
  }
  ln.close();
  printf("=========> %d \n", highestUsedSlot);
  return highestUsedSlot;
}

static bool stationSlotHasUrl(int slot)
{
  return Rlink(slot)[0] != 0;
}

static int normalizeStationSlot(int slot, int direction, int maxSlots)
{
  if (maxSlots <= 0) return 0;
  if (direction == 0) direction = 1;
  while (slot < 0) slot += maxSlots;
  while (slot >= maxSlots) slot -= maxSlots;

  for (int i = 0; i < maxSlots; i++) {
    if (stationSlotHasUrl(slot)) return slot;
    slot += (direction > 0) ? 1 : -1;
    if (slot < 0) slot = maxSlots - 1;
    if (slot >= maxSlots) slot = 0;
  }
  return 0;
}

static void deleteTaskIfRunning(TaskHandle_t &taskHandle)
{
  if (taskHandle != NULL) {
    vTaskDelete(taskHandle);
    taskHandle = NULL;
  }
}

static void stopRuntimeTasksForSettings()
{
  deleteTaskIfRunning(radioH);
  deleteTaskIfRunning(keybH);
  deleteTaskIfRunning(batteryH);
  deleteTaskIfRunning(jackH);
  deleteTaskIfRunning(remoteH);
  deleteTaskIfRunning(displayONOFFH);
}

static void enterSettingsFromButton()
{
  printf("ADC action button1: open settings portal\n");
  BSettings = true;
  toDisplay = 0;
  displayON();
  audio.stopSong();
  connected = false;
  stopRuntimeTasksForSettings();
  settings();
}

static void drawStationSelectScreen()
{
  toDisplay = 0;
  wakeTftBacklight();
  tft.setRotation(1);
  tft.fillRect(0, 72, 320, 132, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Radio select", 160, 78, 2);
  const int slot = S / 2;
  if (stationSlotHasUrl(slot)) {
    tft.setTextColor(TFT_SILVER, TFT_BLACK);
    tft.drawString(Rname(slot), 160, 105, 4);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString(("Preset " + String(slot + 1) + " empty").c_str(), 160, 105, 4);
  }
  tft.setTextColor(TFT_GREY, TFT_BLACK);
  tft.drawString("Encoder: choose  Press knob: OK", 160, 165, 2);
}

/////////////////////////////////////////////////////////////////////
// play station task (core 0)
//
/////////////////////////////////////////////////////////////////////
static void playRadio(void* data)
{
  while (started == false) delay(100);
  while (1)
  {
    if (spotifyPlaybackActive || spotifyConnectActive() ||
        airPlayPlaybackActive || airPlayActive() ||
        usbAudioPlaybackActive || usbAudioActive() ||
        usbDisplayPlaybackActive || usbDisplayActive() ||
        (spotifyRadioResumeAtMs != 0 && (int32_t)(millis() - spotifyRadioResumeAtMs) < 0) ||
        (airPlayRadioResumeAtMs != 0 && (int32_t)(millis() - airPlayRadioResumeAtMs) < 0) ||
        (usbAudioRadioResumeAtMs != 0 && (int32_t)(millis() - usbAudioRadioResumeAtMs) < 0) ||
        (usbDisplayRadioResumeAtMs != 0 && (int32_t)(millis() - usbDisplayRadioResumeAtMs) < 0))
    {
      vTaskDelay(20 / portTICK_PERIOD_MS);
      continue;
    }
    // printf("st %d prev %d\n",station,previousStation);
    if ((station != previousStation) || (connected == false))
    {
      printf("station no %d %s\n", station, Rname(station));
      delay(500);
      audio.stopSong();
      connected = false;
      //delay(100);
      linkS = Rlink(station);
      if (!linkS || linkS[0] == 0) {
        printf("station no %d empty, skipping\n", station);
        station = normalizeStationSlot(station, 1, maxStation());
        previousStation = -1;
        vTaskDelay(50 / portTICK_PERIOD_MS);
        continue;
      }
      mes[0] = 0;
      audio.connecttohost(linkS);
      previousStation = station;
      refreshVolume();
      tft.setRotation(1);
      tft.fillRect(80, 90, 240, 60, TFT_BLACK);
      tft.setTextColor(TFT_STATION);
      tft.setTextDatum(TC_DATUM);
      tft.drawString(Rname(station), 180, 105, 4);

      //    staEncoder.setCount(station);
      if (connected == false) delay(50);
      toDisplay = 2;
    }
    audio.loop();
    vTaskDelay(1);
  }
}
///////////////////////////////////////////////////////////////////
// Task managing the audio jack
//////////////////////////////////////////////////////////////////
static void jack(void* data)
{
  while (true)
  {
    //printf("----------------- %d\n",gpio_get_level(JACK_DETECT));

    if (gpio_get_level(JACK_DETECT) == 0)
    {

      if (jackON == false)
      {
        printf("Jack ON\n");
        gpio_set_level(PA, 0);          // amp off
        ES8388_Write_Reg(29, 0x00);     // stereo
        ES8388_Write_Reg(28, 0x08);     // no phase inversion + click free power up/down
        ES8388_Write_Reg(4, 0x0C);     // Rout2/Lout2
        jackON = true;
        refreshVolume();
      }
    }
    else
    {
      if (jackON == true)
      {
        printf("Jack OFF\n");
        gpio_set_level(PA, 1);          // amp on
        ES8388_Write_Reg(29, 0x20);     // mono (L+R)/2
        ES8388_Write_Reg(28, 0x18);     // Right DAC phase inversion + click free power up/down
        ES8388_Write_Reg(4, 0x30);      // Rout1/Lout1
        jackON = false;
        refreshVolume();
      }
    }
    delay(1000);
  }
}

////////////////////////////////////////////////////////////////////
// Task displaying the battery status (jauge)
////////////////////////////////////////////////////////////////////
static void battery(void* data)
{
#define nominalVal 2700
#define alertVal  350
#define zeroVal   2000

  int adcValue;
  int PadcValue = 0;
  bool display;

  while (true)
  {
    display = false;
    adcValue = analogRead(BAT_GAUGE_PIN) - zeroVal;
    /*
       printf("adcValue = %d\n", adcValue);
       tft.fillRect(140, 50, 30, 20, TFT_BLACK);
       tft.setCursor(140, 50);
       tft.println(adcValue);
    */
    if ((gpio_get_level(USB_DETECT) == 1) && (PadcValue != 1))
    {
      display = true;
      PadcValue = 1;
    }

    if (gpio_get_level(USB_DETECT) == 0)
    {
      if ((PadcValue > (adcValue * 110 / 100)) || (PadcValue < (adcValue * 90 / 100)))
      {
        display = true;
        PadcValue = adcValue;
      }
    }

    if ((display == true) && !(usbDisplayPlaybackActive || usbDisplayActive()))
    {
      tft.setRotation(1);
      tft.fillCircle(280, 35, 40, TFT_BLACK);
      tft.fillRect(260, 25, 32, 18, TFT_WHITE);
      tft.fillRect(292, 30, 4, 8, TFT_WHITE);
      if (gpio_get_level(USB_DETECT) == 0)
        if (adcValue > alertVal)
          tft.fillRect(263, 28, 26 * adcValue / (nominalVal - zeroVal), 12, TFT_BLUE);
        else
          tft.fillRect(263, 28, 26 * adcValue / (nominalVal - zeroVal), 12, TFT_RED);
      else
      {
        tft.fillTriangle(264, 34, 280, 34, 280, 40, TFT_GREY);
        tft.fillTriangle(272, 34, 272, 28, 288, 34, TFT_GREY);
      }
    }
    display = false;
    delay(10000);
  }
}
//////////////////////////////////////////////////////////////////////
// Task monitoring the IR remote
////////////////////////////////////////////////////////////////////////
static void remote(void* data)
{
  while (true)
  {
    if (irrecv.decode(&results)) {
      if (results.decode_type == NEC)
      {
        uint32_t v = results.value;
        if (v != 0xffffffff)REMOTE_KEY = v & 0xFFFF;
      }
      irrecv.resume();  // Receive the next value
    }
    delay(100);
  }
}
//////////////////////////////////////////////////////////////////////
// Task managing display on/off
////////////////////////////////////////////////////////////////////////
static void displayONOFF(void* data)
{
  (void)data;
  while (true)
  {
    displayT = TEMPO;
    gpio_set_level(backLight, 1);
#ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif
    delay(1000);
  }
}
////////////////////////////////////////////////////////////////////
// retrieve display after interrupting Settings or Donate
////////////////////////////////////////////////////////////////////
void retrieveDisplay(void)
{

  // draw "wallpaper screen" and internet source
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  drawImage("/screenV.png", 0, 5);
  tft.setRotation(1);
  tft.fillCircle(50, 50, 40, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TC_DATUM);
  tft.drawCircle(50, 35, 18, TFT_WHITE);
  if (mode == '0')  tft.drawString("4G", 50, 27, 2);
  else tft.drawString("WiFi", 50, 27, 2);


  tft.setTextColor(TFT_GREY, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);  // Set text datum to top-left
  tft.drawString("1-Settings portal", 20, 220, 2);
  tft.drawString(version, 280, 220, 2);

  // restart battery display and display on/off
  xTaskCreatePinnedToCore(battery, "battery", 5000, NULL, 5, &batteryH, 1);
  xTaskCreatePinnedToCore(displayONOFF, "displayONOFF", 5000, NULL, 5, &displayONOFFH, 1);
  // write station name
  tft.setRotation(1);
  tft.fillRect(80, 90, 240, 60, TFT_BLACK);
  tft.setTextColor(TFT_STATION);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(Rname(station), 180, 105, 4);
  // draw mute/unmute icon
  if (muteON == true)
  {
    tft.setRotation(1);
    tft.fillRect(80, 180, 160, 40, TFT_BLACK);
    tft.fillCircle(160, 190, 17, TFT_WHITE);
    tft.fillTriangle(150, 175, 150, 205, 175, 190, TFT_GREY);
    jaugeB = false;
  }
  else
  {
    tft.setRotation(1);
    tft.fillRect(80, 180, 160, 40, TFT_BLACK);
    tft.fillCircle(160, 190, 17, TFT_WHITE);
    tft.fillRect(150, 179, 21, 24, TFT_GREY);
    tft.fillRect(156, 175, 9, 30, TFT_WHITE);
    jaugeB = false;
  }

}



void settingsDisplay(int pos)
{
  tft.setTextDatum(TL_DATUM);
  /*
    if(pos == 0)tft.setTextColor(TFT_GREEN); else tft.setTextColor(TFT_WHITE);
    tft.drawString("- 4G", 110, 90, 4);
    if(pos > 1) tft.setTextColor(TFT_GREEN);else tft.setTextColor(TFT_WHITE);
    tft.drawString("- WiFi :", 110, 140, 4);
  */
  if (pos == 0)tft.setTextColor(TFT_GREEN); else tft.setTextColor(TFT_WHITE);
  tft.drawString("- New connection", 10, 90, 4);
  if (pos == 1)tft.setTextColor(TFT_GREEN); else tft.setTextColor(TFT_WHITE);
  tft.drawString("- Dismiss", 10, 140, 4);
}
///////////////////////////////////////////////////////////////////////
// settings init
///////////////////////////////////////////////////////////////////////
void settings(void)
{
  printf("SETTINGS portal\n");
  startWifiCaptivePortal();
  if (!captiveManualFallbackRequested) return;

  int j;
  int pos;
  char charSet[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+=%*&-_(){}[]@,;:?./X";
  int charSetLength = strlen(charSet);
  char selectedChar[2] = {0}; // To store the currently selected character
  gpio_set_level(backLight, 1);
  printf("SETTINGS!!!\n");
  tft.setRotation(1);
  headerL("SETTINGS", "1- Select your WiFi access", TFT_NAVY);
  settingsDisplay(0);
  //  delay(2000);
  pos = 0;
  staEncoder.setCount(0);
  while (gpio_get_level(CLICK2) == 1)
  {
    /*
        if(button_get_level(sw0) == 0)
        {
          if(button_get_level(sw0) == 0) delay(50);
          retrieveDisplay();
          return;
        }
    */
    int V;
    V = staEncoder.getCount();
    if (V > 0)
    {
      pos++;
      if (pos > 1)pos = 1;
      settingsDisplay(pos);
      staEncoder.setCount(0);
    }

    if (V < 0)
    {
      pos--;
      if (pos < 0) pos = 0;
      settingsDisplay(pos);
      staEncoder.setCount(0);
    }
    delay(100);
  }


  uint8_t bm = 0x31;
  File ln = LittleFS.open("/mode", FILE_WRITE);
  ln.write(&bm, 1);
  ln.close();

  if (pos == 0)
  {
    headerL("SETTINGS", "2- Select your Wifi credentials", TFT_NAVY);
    tft.setTextColor(TFT_RED);
    tft.setTextDatum(TC_DATUM);
    tft.setFreeFont(FSB9);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(500);

    int nSsid = WiFi.scanNetworks();
    printf("ssid# %d\n", nSsid);
    if (nSsid <= 0) {
      tft.fillRect(0, 70, 320, 140, TFT_NAVY);
      tft.setTextColor(TFT_RED);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("No WiFi network found", 160, 105, 4);
      tft.setTextColor(TFT_YELLOW);
      tft.drawString("Use the web portal manual SSID", 160, 145, 2);
      delay(2500);
      captiveManualFallbackRequested = false;
      startWifiCaptivePortal();
      return;
    }
    if (nSsid > 6) nSsid = 6;
    j = 0;
    staEncoder.setCount(0);
    while (gpio_get_level(CLICK2) == 1)
    {
      for (int i = 0; i < nSsid; i++)
      {
        j = staEncoder.getCount() / 2;
        if (j > (nSsid - 1)) {
          j = nSsid - 1;
          staEncoder.setCount(j);
        }
        if (j < 0) {
          j = 0;
          staEncoder.setCount(j);
        }
        printf("j = %d\n", j);
        tft.setTextDatum(TL_DATUM);
        if (i == j)tft.setTextColor(TFT_GREEN); else tft.setTextColor(TFT_WHITE);
        tft.setFreeFont(FSB12);
        snprintf(s, sizeof(s), "%d- %s", i + 1, WiFi.SSID(i).c_str());
        tft.drawString(s, 20, 80 + i * 25, GFXFF);
      }
      delay(100);
    }
    strcpy((char*)ssid, WiFi.SSID(j).c_str());
    printf("ssid = %s\n", ssid);
    // Keep legacy file for compatibility but main storage is wifi.json
    ln = LittleFS.open("/ssid", FILE_WRITE);
    ln.write(ssid, strlen((char*)ssid) + 1);
    ln.close();

    delay(1000);
    headerL("SETTINGS", "2- Select your Wifi credentials", TFT_NAVY);
    tft.setTextColor(TFT_RED);
    tft.setTextDatum(TC_DATUM);
    tft.setFreeFont(FSB9);
    tft.drawString("2 = delete   check = done ", 160, 60, GFXFF);
    c[1] = 0;
    pwd[0] = 0;
    staEncoder.setCount(0);
    Bvalid = false;
    while ( (button_get_level(sw3) == 1) && (Bvalid == false))
    {
      tft.setTextColor(TFT_YELLOW);
      tft.setTextDatum(TL_DATUM);
      tft.setFreeFont(FSB12);
      tft.drawString("ssid:", 20, 80, GFXFF);
        tft.setTextColor(TFT_GREEN);
        tft.drawString((char*)ssid, 80, 80, GFXFF);
        drawPassword((char*)pwd);


      tft.setTextColor(TFT_YELLOW);
      tft.setTextDatum(TL_DATUM);

      while ((gpio_get_level(CLICK2) == 1) && (button_get_level(sw1) == 1) && (button_get_level(sw3) == 1))
      {
        PL = staEncoder.getCount() / 2;
        if (PL < 0) {
          PL = 0;
          staEncoder.setCount(PL);
        }
        if (PL > (charSetLength - 1)) {
          PL = charSetLength - 1;
          staEncoder.setCount(PL * 2);
        }

        // Update the selected character
        selectedChar[0] = (PL != charSetLength - 1) ? charSet[PL] : 'X';

        if (PL != PPL)
        {
          PPL = PL;

            // Display current password
            drawPassword((char*)pwd);

          // Display carousel
          tft.fillRect(100, 170, 120, 40, TFT_NAVY);
          for (int i = -2; i <= 2; i++) {
            int index = (PL + i + charSetLength) % charSetLength;
            char displayChar = charSet[index];
            int xPos = 160 + i * 25;
            int yPos = 180;
            int textSize = (i == 0) ? 4 : 2;

            if (index == charSetLength - 1) {
              // Special case for 'X' (end of charSet)
              tft.setTextColor(TFT_GREEN);
              tft.drawString("ok", xPos - 10, yPos, textSize);
            } else {
              tft.setTextColor((i == 0) ? TFT_YELLOW : TFT_WHITE);
              tft.drawChar(displayChar, xPos, yPos, textSize);
            }
          }
        }
        delay(100);
      }

      if (gpio_get_level(CLICK2) == 0)
      {
        if (PL != (charSetLength - 1))
        {
          if (strlen((char*)pwd) < 64) strcat((char*)pwd, selectedChar);
          printf("pwd = %s\n", pwd);
        }
        else
        {
          Bvalid = true;
          printf("Bvalid\n");
        }
        while (gpio_get_level(CLICK2) == 0) delay(10);
      }


      if (button_get_level(sw1) == 0)
      {
        if (strlen((char*)pwd) > 0) pwd[strlen((char*)pwd) - 1] = 0;
        while (button_get_level(sw1) == 0) delay(10);
      }

    }
    printf("ssid: %s   pwd: %s\n", ssid, pwd);

    saveWifiCredential((char*)ssid, (char*)pwd);
  }
  //  }
  tft.fillScreen(TFT_NAVY);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("Restarting...", 150, 105, 4);

  delay(1000);
  esp_restart();

  /*
    gpio_set_level(backLight, 1);
    tft.fillScreen(TFT_NAVY);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Settings modified!", 150, 85, 4);
    tft.setTextColor(TFT_RED);
    tft.drawString("Please, restart!", 150, 125, 4);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("(power switch => OFF/ON)", 150, 155, 2);
    for(;;);
  */
}
///////////////////////////////////////////////////////////////////////
// init WiFi credentials using Improv
////////////////////////////////////////////////////////////////////////
ImprovWiFi improvSerial(&USBSerial);

void WiFiConnected(const char *ssid, const char *password)
{
  //  printf("%s   %s\n", ssid, password);
  saveWifiCredential(ssid, password);
  if (radioH) vTaskDelete(radioH);
  if (keybH) vTaskDelete(keybH);
  if (batteryH) vTaskDelete(batteryH);
  if (jackH) vTaskDelete(jackH);
  if (remoteH) vTaskDelete(remoteH);
  if (displayONOFFH) vTaskDelete(displayONOFFH);


  tft.fillScreen(TFT_NAVY);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("Restarting...", 150, 105, 4);

  delay(1000);
  esp_restart();
  /*
    gpio_set_level(backLight, 1);
    tft.fillScreen(TFT_NAVY);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Settings modified!", 150, 85, 4);
    tft.setTextColor(TFT_RED);
    tft.drawString("Please, restart!", 150, 125, 4);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("(power switch => OFF/ON)", 150, 155, 2);
    for(;;);
  */
}
static void improvWiFiInit(void* data)
{
  improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32_S3, "Radio", MUSE_IMPROV_VERSION, "Raspiaudio Radio");
  improvSerial.onImprovConnected(WiFiConnected);
  delay(500);
  while (true)
  {
    improvSerial.handleSerial();
    vTaskDelay(10 / portTICK_PERIOD_MS); // Use FreeRTOS delay
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// .ota bin loading
//////////////////////////////////////////////////////////////////////////////////////////////////
void loadLastOTA(void)
{
  delay(1000);
  // Configure custom Root CA
  tft.fillRect(0, 0, 320, 240, TFT_BLACK);
  tft.setTextColor(TFT_RED);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Loading last binary...", 180, 105, 4);
  ota.setCACert(root_ca);
  printf("6\n");
  printf("Initializing OTA storage\n");
  if ((ota_err = ota.begin()) != Arduino_ESP32_OTA::Error::None)
  {
    printf  ("Arduino_ESP_OTA::begin() failed with error code: %d\n", (int)ota_err);
    return;
  }
  printf("7\n");

  printf("Starting download to flash ...\n");
  int const ota_download = ota.download(OTA_FILE_LOCATION);
  if (ota_download <= 0)
  {
    printf  ("Arduino_ESP_OTA::download failed with error code: %d\n ", ota_download);
    return;
  }
  printf  ("%d bytes stored \n", ota_download);


  printf("Verify update integrity and apply ...\n");
  if ((ota_err = ota.update()) != Arduino_ESP32_OTA::Error::None)
  {
    printf  ("ota.update() failed with error code: %d\n ", (int)ota_err);
    return;
  }

  printf("Performing a reset after which the bootloader will start the new firmware.\n");
  tft.fillRect(0, 0, 320, 240, TFT_BLACK);
  tft.setTextColor(TFT_BLUE);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Restarting...", 180, 105, 4);
  delay(2000); // Make sure the serial message gets out before the reset.
  ota.reset();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup() {

#if !ENABLE_USB_AUDIO
  USBSerial.begin(115200);
#endif
  printf("PSRAM: %s, size=%u, free=%u\n",
         psramFound() ? "yes" : "no",
         (unsigned)ESP.getPsramSize(),
         (unsigned)ESP.getFreePsram());

#if !ENABLE_USB_AUDIO
  // Start the Improv Wi-Fi over Serial task immediately
  xTaskCreatePinnedToCore(improvWiFiInit, "improvWiFiInit", 5000, NULL, 5, &improvWiFiInitH, 1);

  // Small delay to ensure Improv Serial is ready
  vTaskDelay(100 / portTICK_PERIOD_MS);
#endif

  /////////////////////////////////////////////////////
  // Little FS init
  /////////////////////////////////////////////////////
  littleFsMounted = LittleFS.begin();
  if (!littleFsMounted) {
    Serial.println("LittleFS initialisation failed; continuing without data partition");
  } else {
    //    LittleFS.format();
    File root = LittleFS.open("/", "r");
    File file = root.openNextFile();
    while (file) {
      printf("FILE: /%s\n", file.name());
      file = root.openNextFile();
      delay(100);
    }
    ensureLocalRadioData();
  }

  //////////////////////////////////////////////////
  //Encoders init
  //////////////////////////////////////////////////
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  volEncoder.attachHalfQuad(ENC_B1, ENC_A1);
  staEncoder.attachHalfQuad(ENC_A2, ENC_B2);

  ///////////////////////////////////////////////////////
  // init gpios
  ///////////////////////////////////////////////////////
  //gpio_reset_pin
  gpio_reset_pin(CLICK1);
  gpio_reset_pin(CLICK2);
  gpio_reset_pin(JACK_DETECT);
  gpio_reset_pin(USB_DETECT);
  gpio_pullup_dis(USB_DETECT);
  gpio_pulldown_dis(USB_DETECT);
  gpio_reset_pin(EN_4G);
  gpio_reset_pin(PA);
  gpio_reset_pin(backLight);

  //gpio_set_direction
  gpio_set_direction(CLICK1, GPIO_MODE_INPUT);
  gpio_set_direction(CLICK2, GPIO_MODE_INPUT);
  gpio_set_direction(JACK_DETECT, GPIO_MODE_INPUT);
  gpio_set_direction(USB_DETECT, GPIO_MODE_INPUT);

  gpio_set_direction(EN_4G, GPIO_MODE_OUTPUT);
  gpio_set_direction(PA, GPIO_MODE_OUTPUT);
  gpio_set_direction(backLight, GPIO_MODE_OUTPUT);
  gpio_set_level(backLight, 1);

  //gpio_set_pull_mode
  gpio_set_pull_mode(CLICK1, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(CLICK2, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(JACK_DETECT, GPIO_PULLUP_ONLY);

  gpio_set_pull_mode(EN_4G, GPIO_PULLUP_ONLY);
  initAdcButtons();

  //////////////////////////////////////////////////////
  // request to load the last version
  //////////////////////////////////////////////////////
  delay(100);
  BOTA = false;
  if (gpio_get_level(CLICK1) == 0) BOTA = true;

  ///////////////////////////////////////////////////////
  // to run the Factory Test
  //////////////////////////////////////////////////////
  if (gpio_get_level(CLICK2) == 0) FactoryTest();

  ///////////////////////////////////////////////////////
  //enable remote
  ///////////////////////////////////////////////////////
  irrecv.enableIRIn();
  forceTftPowerOn();
  tft.init();



  File ln = LittleFS.open("/mode", FILE_READ);
  // If mode is not defined, default to WiFi and let the captive portal handle credentials.
  if (!ln) {
    mode = '1';
    File modeFile = LittleFS.open("/mode", FILE_WRITE);
    if (modeFile) {
      modeFile.write(&mode, 1);
      modeFile.close();
    }
  } else {
    ln.read(&mode, 1);
    ln.close();
  }
  printf("mode = %c\n", mode);
  //////////////////////////////////////////////////////
  //Screen init
  //////////////////////////////////////////////////////
  printf("screen init...\n");
  //  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);
  drawImage("/Raspiaudio.png", 0, 0);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(version, 280, 220, 2);
  delay(2000);

  // Initialize USB Audio before Wi-Fi so Windows sees the sound card even if setup enters the captive portal.
  initAudioHardware();
  usbDisplayBegin(tft, handleUsbDisplayEvent);
  usbAudioBegin(audio, handleUsbAudioEvent);

  {
    printf("WiFi\n");
    gpio_set_level(EN_4G, 0);
  }
  ////////////////////////////////////////////////
  // WiFi init
  ////////////////////////////////////////////////
  started = false;
  bool wifiSetupPortalNeeded = false;
  WifiCred wifiList[WIFI_MAX_NETWORKS];
  int wifiCount = loadWifiList(wifiList, WIFI_MAX_NETWORKS);
  if (wifiCount == 0) {
    printf("No WiFi credentials; starting background portal and continuing USB services\n");
    wifiSetupPortalNeeded = true;
  } else {
    WiFi.useStaticBuffers(true);
    WiFi.mode(WIFI_STA);
    wifiMultiFromList(wifiMulti, wifiList, wifiCount);
    //   wifiMulti.run();
  }
  const uint32_t connectTimeoutMs = 20000;
  if (!wifiSetupPortalNeeded && wifiMulti.run(connectTimeoutMs) == WL_CONNECTED) {
#if !ENABLE_USB_AUDIO
    USBSerial.print("WiFi connected: ");
    USBSerial.print(WiFi.SSID());
    USBSerial.print(" ");
    USBSerial.println(WiFi.RSSI());
#else
    printf("WiFi connected: %s %d\n", WiFi.SSID().c_str(), WiFi.RSSI());
#endif
    started = true;

    ////////////////////////////////////////////////////////////////
    //last version .ota loading
    ////////////////////////////////////////////////////////////////
    if (BOTA == true)loadLastOTA();

    fetchStationList();


  }
  else {
    if (!wifiSetupPortalNeeded) {
      Serial.println("WiFi not connected; starting background portal and continuing USB services");
      wifiSetupPortalNeeded = true;
    }
  }


  //  delay(2000);
  ///////////////////////////////////////////////////////////////
  // Audio init
  //////////////////////////////////////////////////////////////
  initAudioHardware();

  ///////////////////////////////////////////////////////////////
  // recovering params (station & vol)
  ///////////////////////////////////////////////////////////////

  // previous station
  ln = LittleFS.open("/station", "r");
  station = 0;
  if (ln) {
    ln.read((uint8_t*)b, 2);
    b[2] = 0;
    station = atoi(b);
    ln.close();
  }
  // previous volume
  ln = LittleFS.open("/volume", "r");
  vol = maxVol;
  if (ln) {
    ln.read((uint8_t*)b, 2);
    b[2] = 0;
    vol = atoi(b);
    ln.close();
  }
  if (vol < 0 || vol > maxVol) vol = maxVol;
  MS = maxStation() - 1;
  if (MS < 0) {
    writeDefaultPresets();
    MS = maxStation() - 1;
  }
  if (station < 0 || station > MS || !stationSlotHasUrl(station)) {
    station = normalizeStationSlot(0, 1, MS + 1);
    File stationFile = LittleFS.open("/station", "w");
    if (stationFile) {
      sprintf(b, "%02d", station);
      stationFile.write((uint8_t*)b, 2);
      stationFile.close();
    }
  }
  previousStation = -1;
  printf("station = %d    vol = %d\n", station, vol);
  // volume encoder init
  V = vol * pos360 / maxVol;
  volEncoder.setCount(V);
  PV = V;
  refreshVolume();
  spotifyConnectBegin(audio, handleSpotifyConnectEvent);
  airPlayBegin(audio, handleAirPlayEvent);
  // station encoder init
  staEncoder.setCount(station * 2);
  S = VS = station * 2;

  ////////////////////////////////////////////////////////////////
  // draw "wallpaper screen" and internet
  ////////////////////////////////////////////////////////////////
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  drawImage("/screenV.png", 0, 5);
  tft.setRotation(1);
  tft.fillCircle(50, 50, 40, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TC_DATUM);
  // tft.fillCircle(50, 35, 18, TFT_WHITE);
  //tft.fillCircle(50, 35, 16, TFT_BLACK);
  tft.drawCircle(50, 35, 18, TFT_WHITE);
  if (mode == '0')  tft.drawString("4G", 50, 27, 2);
  else tft.drawString("WiFi", 50, 27, 2);

  tft.setTextColor(TFT_GREY, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);  // Set text datum to top-left
  tft.drawString("1-Settings portal", 20, 220, 2);
  tft.drawString(version, 280, 220, 2);
  toDisplay = 0;
  displayON();
  Bdonate = false;
  /////////////////////////////////////////////////////////////////////////
  // Starting tasks
  /////////////////////////////////////////////////////////////////////////
  // Task playing radio station (core 0)
  xTaskCreatePinnedToCore(playRadio, "radio", 5000, NULL, 5, &radioH, 0);
  // Task managing buttons  (core 1)
  xTaskCreatePinnedToCore(keyb, "keyb", 5000, NULL, 5, &keybH, 1);
  // Task monitoring the battery
  xTaskCreatePinnedToCore(battery, "battery", 5000, NULL, 5, &batteryH, 1);
  // Task managing the jack switch
  xTaskCreatePinnedToCore(jack, "jack", 5000, NULL, 5, &jackH, 1);
  // Task managing the IR remote
  xTaskCreatePinnedToCore(remote, "remote", 5000, NULL, 5, &remoteH, 1);
  // Task managing display turn on:turn off
  xTaskCreatePinnedToCore(displayONOFF, "displayONOFF", 5000, NULL, 5, &displayONOFFH, 1);
  if (wifiSetupPortalNeeded) {
    startWifiCaptivePortalBackground();
  }
}


void loop() {


  if (consumeAdcButtonEvent(sw0))
  {
    if (BSettings == false) enterSettingsFromButton();
    else printf("ADC action button1 ignored: settings already active\n");
  }

  serviceSpotifyCoverDisplay();
  debugAdcButtonsTick();
  const bool usbDisplayBusy = usbDisplayPlaybackActive || usbDisplayActive();

  //////////////////////////////////////////////////////////////////////
  // Volume via encoder
  //////////////////////////////////////////////////////////////////////
  //  printf("------%x\n",REMOTE_KEY);
  V = volEncoder.getCount();
  if (V != PV)
  {
    const int previousVol = vol;
    PV = V;
    if (V < 0) V = 0;
    if (V > pos360) V = pos360;
    volEncoder.setCount(V);
    vol = V * maxVol / pos360;
    if (vol == 0 && !muteON) {
      Pvol = 1;
      muteON = true;
    } else if (vol > 0 && muteON) {
      muteON = false;
    }
    refreshVolume();
#if ENABLE_USB_AUDIO
    usbAudioSendHostVolumeDelta((int8_t)(vol - previousVol));
#endif
    printf("============> %d  %d\n", V, vol);
    sprintf(b, "%02d", vol);
    File ln = LittleFS.open("/volume", "w");
    ln.write((uint8_t*)b, 2);
    ln.close();
    if (!usbDisplayBusy) toDisplay = 3;
  }
  //////////////////////////////////////////////////////////////////////
  // volume via remote keys
  //////////////////////////////////////////////////////////////////////
  if ((REMOTE_KEY == VOLP_rem) || (REMOTE_KEY == VOLM_rem))
  {
    const int previousVol = vol;
    if (muteON == true) {
      vol = Pvol;
      muteON = false;
    }
    if (REMOTE_KEY == VOLP_rem)vol++;
    if (REMOTE_KEY == VOLM_rem)vol--;
    REMOTE_KEY = 0;
    if (vol > maxVol) vol = maxVol;
    if (vol < 0) vol = 0;
    if (vol == 0 && !muteON) {
      Pvol = 1;
      muteON = true;
    } else if (vol > 0 && muteON) {
      muteON = false;
    }
    refreshVolume();
#if ENABLE_USB_AUDIO
    usbAudioSendHostVolumeDelta((int8_t)(vol - previousVol));
#endif
    volEncoder.setCount(vol * pos360 / maxVol);
    sprintf(b, "%02d", vol);
    File ln = LittleFS.open("/volume", "w");
    ln.write((uint8_t*)b, 2);
    ln.close();
    if (!usbDisplayBusy) toDisplay = 3;
  }

  ///////////////////////////////////////////////////////////////////////
  // mute / unmute
  //////////////////////////////////////////////////////////////////////
  if ((muteB == true) && (muteON == false))
  {
    Pvol = vol;
    vol = 0;
    muteON = true;
    V = 0;
    PV = V;
    volEncoder.setCount(V);
    refreshVolume();
#if ENABLE_USB_AUDIO
    usbAudioSendHostMuteToggle();
#endif
    muteB = false;
    if (!usbDisplayBusy) toDisplay = 1;
  }
  if ((muteB == true) && (muteON == true))
  {
    vol = Pvol;
    muteON = false;
    V = vol * pos360 / maxVol;
    PV = V;
    volEncoder.setCount(V);
    refreshVolume();
#if ENABLE_USB_AUDIO
    usbAudioSendHostMuteToggle();
#endif
    muteB = false;
    if (!usbDisplayBusy) toDisplay = 2;
  }


  if ((Bdonate == false) && !usbDisplayBusy)
  {
    ////////////////////////////////////////////////////////////////////////
    //  station search via encoder
    ////////////////////////////////////////////////////////////////////////
    S = staEncoder.getCount();
    delay(50);
    if ((S != VS) && (S == staEncoder.getCount()))
    {
      printf(">>>> %d\n", S);
      displayON();
      modSta = true;
      CLICK2B = false;
      lastModTime = millis();
      int direction = (S > VS) ? 1 : -1;
      if (S > MS * 2) S = 0;
      if (S < 0) S = MS * 2;
      S = normalizeStationSlot(S / 2, direction, MS + 1) * 2;
      VS = S;
      staEncoder.setCount(S);
      drawStationSelectScreen();
    }


    ////////////////////////////////////////////////////////////////////////////
    // station search via remote keys
    ////////////////////////////////////////////////////////////////////////////
    if ((REMOTE_KEY == LEFT_rem) || (REMOTE_KEY == RIGHT_rem))
    {
      displayON();
      if (modSta == false) S = station * 2;
      int direction = (REMOTE_KEY == LEFT_rem) ? -1 : 1;
      if (direction < 0) S -= 2;
      else S += 2;
      REMOTE_KEY = 0;
      modSta = true;
      CLICK2B = false;
      lastModTime = millis();
      if (S > MS * 2) S = 0;
      if (S < 0) S = MS * 2;
      S = normalizeStationSlot(S / 2, direction, MS + 1) * 2;
      staEncoder.setCount(S);
      VS = S;
      drawStationSelectScreen();
    }


    /////////////////////////////////////////////////////////////////////////
    // new station validation
    /////////////////////////////////////////////////////////////////////////
    if (CLICK2B == true)
    {
      displayON();
      if (modSta == true)
      {
        modSta = false;
        int selectedStation = S / 2;
        if (!stationSlotHasUrl(selectedStation)) {
          selectedStation = normalizeStationSlot(selectedStation, 1, MS + 1);
          S = selectedStation * 2;
        }
        station = selectedStation;
        printf("station = %d\n", station);
        staEncoder.setCount(S);
        char b[4];
        sprintf(b, "%02d", station);
        File ln = LittleFS.open("/station", "w");
        ln.write((uint8_t*)b, 2);
        ln.close();
        CLICK2B = false;
        if (muteON == true)
        {
          vol = Pvol;
          muteON = false;
          V = vol * pos360 / maxVol;
          PV = V;
          volEncoder.setCount(V);
          refreshVolume();
          muteB = false;
        }
      }
    }
    ////////////////////////////////////////////////////////////////////////
    // new station search give up
    ////////////////////////////////////////////////////////////////////////
    if (modSta == true)
    {
      if (millis() > (lastModTime + 4000))
      {
        displayON();
        modSta = false;
        tft.setRotation(1);
        tft.fillRect(80, 90, 240, 60, TFT_BLACK);
        tft.setTextColor(TFT_STATION);
        tft.setTextDatum(TC_DATUM);
        tft.drawString(Rname(station), 180, 105, 4);
      }
    }

  }

  if (consumeAdcButtonEvent(sw1))
  {
    printf("ADC action button2: unused\n");
  }



  /////////////////////////////////////////////////////////////////////////
  // Display refresh
  /////////////////////////////////////////////////////////////////////////
  if ((toDisplay != 0) && (Bdonate == false) && !usbDisplayBusy)
  {
    displayON();
    switch (toDisplay)
    {
      case 1 :
        // playing...
        tft.setRotation(1);
        tft.fillRect(80, 180, 160, 40, TFT_BLACK);
        //        tft.fillCircle(160, 190, 22, TFT_GREY);
        //        tft.fillCircle(160, 190, 20, TFT_NAVY);
        tft.fillCircle(160, 190, 17, TFT_WHITE);
        tft.fillTriangle(150, 175, 150, 205, 175, 190, TFT_GREY);
        jaugeB = false;
        toDisplay = 0;
        break;
      case 2 :
        // waiting...
        tft.setRotation(1);
        tft.fillRect(80, 180, 160, 40, TFT_BLACK);
        //       tft.fillCircle(160, 190, 22, TFT_NAVY);
        //        tft.fillCircle(160, 190, 20, TFT_NAVY);
        tft.fillCircle(160, 190, 17, TFT_WHITE);
        tft.fillRect(150, 179, 21, 24, TFT_GREY);
        tft.fillRect(156, 175, 9, 30, TFT_WHITE);
        jaugeB = false;
        toDisplay = 0;
        break;
      case 3 :
        // modifiying volume...
        if (jaugeB == false)
        {
          tft.setRotation(1);
          tft.fillCircle(160, 190, 25, TFT_BLACK);
          tft.fillRoundRect(80, 180, 160, 20, 10, TFT_WHITE);
          jaugeB = true;
        }
        int V2;
        //       V2 = V + V;
        V2 = V;
        if (V2 > pos360) V2 = pos360;

        tft.fillRect(90 + 140 * V2 / pos360, 182, 140 - 140 * V2 / pos360, 16, TFT_WHITE);

        tft.fillRect(90, 182, 140 * V2 / pos360, 16, TFT_GREY);
        N = 0;
        toDisplay = 4;
        break;
      case 4 :
        //
        N++;
        if (N > 20)toDisplay = 2;
        break;
    }


  }
  /*
    ///////////////////////////////////////////////////////////////////////////////////
    //test remote keys
    ///////////////////////////////////////////////////////////////////////////////////
    if (irrecv.decode(&results)) {
      if(results.decode_type == NEC)
      {
        uint32_t v = results.value;
        if(v != 0xffffffff)printf("%08x %04x %04x\n", v, v>>16, v&0xffff);
      }
      irrecv.resume();  // Receive the next value
    }
  */
  delay(100);
}

void audio_info(const char *info) {
#define maxRetries 4
  // Serial.print("info        "); Serial.println(info);
  if (strstr(info, "SampleRate=") != nullptr)
  {
    sscanf(info, "SampleRate=%d", &sampleRate);
    printf("==================>>>>>>>>>>%d\n", sampleRate);
  }
  connected = true;
  if (strstr(info, "failed") != nullptr) {
    connected = false;
    printf("failed\n");

    tft.fillRect(80, 90, 240, 60, TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Connecting...", 200, 105, 4);
    delay(500);
    printf("RSSI = %d dB\n", WiFi.RSSI());


    //    printf("RSSI = %d dB\n", WiFi.RSSI());
    wifiMulti.run();


    if (WiFi.status() != WL_CONNECTED )
    {
      WiFi.disconnect(true);
      wifiMulti.run();
      delay(1500);
    }

  }
}

void QRcreate(void)
{
  tft.fillScreen(TFT_WHITE);  // Changed to white background for better contrast

  // Read the URL stored in the file
  File ln = LittleFS.open("/QR", FILE_READ);
  if (!ln) {
    printf("Error: Unable to open file /QR\n");
    return;
  }

  // Read file data and store URL in a buffer
  char baseUrl[120];  // Maximum URL size
  ln.read((uint8_t*)baseUrl, sizeof(baseUrl) - 1);
  baseUrl[ln.size()] = 0;  // Terminate the string
  ln.close();

  // Get ESP32 MAC address
  String macAddress = WiFi.macAddress();
  macAddress.replace(":", "");  // Remove colons

  // Build complete URL with MAC address
  String fullUrl = String(baseUrl) + "?serial=" + macAddress;

  // Log to verify complete URL
  printf("Complete URL: %s\n", fullUrl.c_str());

  // Convert string to char array for QRCode library
  char qrData[256];  // Maximum QR buffer size, increase if necessary
  fullUrl.toCharArray(qrData, sizeof(qrData));

  // Generate QR code from complete URL
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(4)];  // Use version 4 for more capacity
  qrcode_initText(&qrcode, qrcodeData, 4, 0, qrData);

  // Display text above QR code

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setTextDatum(TC_DATUM);

  tft.setCursor((tft.width() - tft.textWidth("Scan to add radio stations")) / 2, 20);
  tft.print("Scan to add radio stations");
  tft.setTextSize(1);
  // Display QR code on TFT screen
  uint16_t scale = 5;  // Increase scale to make QR code more readable
  uint16_t offsetX = (tft.width() - qrcode.size * scale) / 2;
  uint16_t offsetY = (tft.height() - qrcode.size * scale) / 2 + 20;  // Add 20 pixels to move QR code down

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      uint16_t color = qrcode_getModule(&qrcode, x, y) ? TFT_BLACK : TFT_WHITE;
      tft.fillRect(offsetX + x * scale, offsetY + y * scale, scale, scale, color);
    }
  }
}

void audio_id3data(const char *info) { //id3 metadata
  Serial.print("id3data     "); Serial.println(info);
}
void audio_eof_mp3(const char *info) { //end of file
  Serial.print("eof_mp3     "); Serial.println(info);
}
void audio_showstation(const char *info) {
  Serial.print("station     "); Serial.println(info);
}
void audio_showstreaminfo(const char *info) {
  Serial.print("streaminfo  "); Serial.println(info);
}
void audio_showstreamtitle(const char *info) {
  Serial.print("streamtitle "); Serial.println(info);
  if (strlen(info) != 0)
  {
    //  convToAscii((char*)info, mes);
    iMes = 0;
  }
  else mes[0] = 0;
}
void audio_bitrate(const char *info) {
  Serial.print("bitrate     "); Serial.println(info);
}
void audio_commercial(const char *info) { //duration in sec
  Serial.print("commercial  "); Serial.println(info);
}
void audio_icyurl(const char *info) { //homepage
  // Serial.print("icyurl      ");Serial.println(info);
}
void audio_lasthost(const char *info) { //stream URL played
  Serial.print("lasthost    "); Serial.println(info);
}
void audio_eof_speech(const char *info) {
  Serial.print("eof_speech  "); Serial.println(info);
}

static void audioEvent(Audio::msg_t msg) {
  const char* info = msg.msg ? msg.msg : "";
  switch (msg.e) {
    case Audio::evt_info:
    case Audio::evt_log:
      audio_info(info);
      break;
    case Audio::evt_id3data:
      audio_id3data(info);
      break;
    case Audio::evt_eof:
      audio_eof_mp3(info);
      break;
    case Audio::evt_name:
      audio_showstation(info);
      break;
    case Audio::evt_icydescription:
      audio_showstreaminfo(info);
      break;
    case Audio::evt_streamtitle:
      audio_showstreamtitle(info);
      break;
    case Audio::evt_bitrate:
      audio_bitrate(info);
      break;
    case Audio::evt_icyurl:
      audio_icyurl(info);
      break;
    case Audio::evt_lasthost:
      audio_lasthost(info);
      break;
    default:
      break;
  }
}

void fetchStationList() {
  ensureLocalRadioData();
  printf("Local radio presets/catalog ready; remote station server disabled.\n");
  return;
#if 0
  // Lire l'URL de base à partir du fichier /QR
  File ln = LittleFS.open("/QR", FILE_READ);
  if (!ln) {
    printf("Erreur : Impossible d'ouvrir le fichier /QR\n");
    return;
  }
  char baseUrl[120];  // Ajustez la taille si nécessaire
  size_t len = ln.readBytes(baseUrl, sizeof(baseUrl) - 1);
  baseUrl[len] = '\0';  // Terminer la chaîne de caractères
  ln.close();

  // Obtenir l'adresse MAC
  String macAddress = WiFi.macAddress();
  macAddress.replace(":", "");  // Supprimer les deux-points

  // Construire l'URL complète
  String fullUrl = String(baseUrl) + "?serial=" + macAddress + "&action=get";

  printf("Récupération de la liste des stations à partir de: %s\n", fullUrl.c_str());

  // Effectuer une requête HTTP GET
  HTTPClient http;
  http.begin(fullUrl);

  int httpCode = http.GET();
  if (httpCode > 0) {
    printf("Code retour HTTP GET : %d\n", httpCode);

    // Si le serveur retourne un code 302 (Redirection)
    if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) { // 302 is HTTP_CODE_FOUND
      String newLocation = http.header("Location"); // Lire l'en-tête "Location"
      if (newLocation.length() > 0) {
        printf("Redirection vers: %s\n", newLocation.c_str());

        // Suivre la redirection
        http.end(); // Fermer la première connexion
        http.begin(newLocation); // Effectuer une nouvelle requête vers la nouvelle URL

        httpCode = http.GET(); // Envoyer la nouvelle requête
        printf("Code retour après redirection: %d\n", httpCode);
      } else {
        printf("Erreur: Aucun en-tête 'Location' trouvé pour la redirection\n");
        http.end();
        return;
      }
    }

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      printf("Payload reçu :\n%s\n", payload.c_str());

      // Parser la réponse JSON
      DynamicJsonDocument doc(2048);  // Ajustez la taille si nécessaire

      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        printf("Erreur de désérialisation JSON: %s\n", error.c_str());
        return;
      }

      if (doc.is<JsonArray>()) {
        JsonArray stations = doc.as<JsonArray>();
        if (stations.size() > 0) {
          // Mettre à jour les fichiers /linkS et /nameS
          File linkFile = LittleFS.open("/linkS", FILE_WRITE);
          File nameFile = LittleFS.open("/nameS", FILE_WRITE);
          if (!linkFile || !nameFile) {
            printf("Erreur : Impossible d'ouvrir les fichiers /linkS ou /nameS en écriture\n");
            return;
          }

          for (JsonObject station : stations) {
            const char* name = station["name"];
            const char* url = station["url"];
            // Écrire dans les fichiers
            nameFile.println(name);
            linkFile.println(url);
          }

          linkFile.close();
          nameFile.close();

          printf("Liste des stations mise à jour depuis le serveur\n");
        } else {
          printf("Aucune station trouvée dans la réponse du serveur, utilisation des stations par défaut\n");
        }
      } else {
        printf("La réponse du serveur n'est pas un tableau JSON\n");
      }

    } else {
      printf("Code HTTP inattendu après redirection: %d\n", httpCode);
    }
  } else {
    printf("Échec de la requête HTTP GET, erreur: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
#endif
}
void FactoryTest() {

  int FILESIZE;
#define bytesToRead 128
  uint8_t b[bytesToRead];
  //bool started = false;
  // SD 1 wire
#define clk         14
#define cmd         15
#define d0          2

#define SDA         18
#define SCL         11


#define I2S_DOUT      17
#define I2S_BCLK      5
#define I2S_LRC       16
#define I2S_DIN       4
#if ENABLE_LEGACY_FACTORY_I2S
#define I2SW (i2s_port_t)0
#define I2SR (i2s_port_t)1
#endif

  // SD 1 wire
#define clk         14
#define cmd         15
#define d0          2
#define EN_4G           GPIO_NUM_38
#define nominalVal 2700
#define alertVal  350
#define zeroVal   2000

  int adcValue;
  int res;
  int vol = 20;
#define maxVol 31


  gpio_set_level(EN_4G, 1);

  IRrecv irrecv(IR);
  decode_results results;


  WiFiClient espClient;
  PubSubClient client(espClient);
  bool testON;
  time_t now;
  struct tm timeinfo;
  char timeStr[60];

  uint8_t header[] = {
    0x52, 0x49, 0x46, 0x46, //"RIFF"
    0x24, 0x7D, 0x00, 0x00, //taille fichier - 8 (little endian)
    0x57, 0x41, 0x56, 0x45, //"WAVE"
    0x66, 0x6d, 0x74, 0x20, //"fmt "
    0x10, 0x00, 0x00, 0x00, //nb d'octets du bloc
    0x01, 0x00,             //format PCM
    0x02, 0x00,             //nombre de canaux
    0x40, 0x1F, 0x00, 0x00, //frequence d'echantillonnage 8000
    0x00, 0x7D, 0x00, 0x00, //nombre d'octets a lire par seconde   32000
    0x02, 0x00,             //nombre d'octets par bloc d'échantillonnage
    0x10, 0x00,             //nb de bits par echantillon
    0x64, 0x61, 0x74, 0x61, //"data"
    0x00, 0x7D, 0x00, 0x00
  };   //nombre d'octets de donnees

#define BLOCK_SIZE 128

  // Configuration pour I2S0 (écriture)
#if ENABLE_LEGACY_FACTORY_I2S
  i2s_config_t i2s_config_write = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 8000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 128/*,

    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
*/
  };

  // Configuration pour I2S1 (lecture)
  i2s_config_t i2s_config_read = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 8000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 128/*,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
*/
  };

  // Configuration des broches pour I2S0 (écriture)
  i2s_pin_config_t pin_config_write = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  // Configuration des broches pour I2S1 (lecture)
  i2s_pin_config_t pin_config_read = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DIN
  };
#endif



#define RECB CLICK1B
#define bytesToRead 128
  //  uint8_t b[bytesToRead];
  char buf[20];
  Serial.begin(115200);

  ///////////////////////////////////////////////////////
  // init gpios
  ///////////////////////////////////////////////////////
  //gpio_reset_pin
  gpio_reset_pin(CLICK1);
  gpio_reset_pin(CLICK2);
  gpio_reset_pin(EN_4G);
  gpio_reset_pin(JACK_DETECT);
  gpio_reset_pin(USB_DETECT);
  //gpio_set_direction
  gpio_set_direction(CLICK1, GPIO_MODE_INPUT);
  gpio_set_direction(CLICK2, GPIO_MODE_INPUT);
  gpio_set_direction(JACK_DETECT, GPIO_MODE_INPUT);
  gpio_set_direction(USB_DETECT, GPIO_MODE_INPUT);
  //gpio_set_pull_mode
  gpio_set_pull_mode(CLICK1, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(CLICK2, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(JACK_DETECT, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(USB_DETECT, GPIO_PULLUP_ONLY);
  // power enable
  gpio_reset_pin(PA);
  gpio_set_direction(PA, GPIO_MODE_OUTPUT);
  // 4G enable
  gpio_set_direction(EN_4G, GPIO_MODE_OUTPUT);

  //////////////////////////////////////////////////
  //Encoders init
  //////////////////////////////////////////////////
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  volEncoder.attachHalfQuad(ENC_A1, ENC_B1);
  staEncoder.attachHalfQuad(ENC_A2, ENC_B2);


  xTaskCreatePinnedToCore(keyb, "keyb", 5000, NULL, 5, NULL, 0);


  //////////////////////////////////////////////////////
  //Screen init
  //////////////////////////////////////////////////////
  printf("screen init...\n");
  tft.init();
  tft.setRotation(1);
  headerS("Ros&Co", TFT_NAVY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TC_DATUM);
  //  tft.fillScreen(TFT_NAVY);
  tft.drawString("FACTORY TEST", 160, 115, 4);


  ///////////////////////////////////////////////////////
  // test#1 buttons
  //////////////////////////////////////////////////////

  RECB = false;
  delay(2000);
  headerL("test#1 : Buttons", "try each, from left to right bottom to top", TFT_NAVY);
  tft.fillRect(130, 100, 70, 70, TFT_WHITE);
  tft.fillRect(132, 102, 66, 66, TFT_NAVY);
  tft.setTextColor(TFT_RED);
  tft.setTextDatum(TC_DATUM);
  for (int i = 0; i < 4; i++)
  {
    while (button_get_level(i) == 1) delay(100);
    sprintf(buf, "SW%d\n", i + 1);
    tft.fillRect(132, 102, 66, 66, TFT_NAVY);
    tft.drawString(buf, 170, 125, 4);

    delay(100);
  }
  RECB = false;
  delay(500);
  tft.fillRect(132, 102, 66, 66, TFT_NAVY);
  delay(500);
  tft.drawString("OK", 170, 125, 4);
  delay(1000);


  ///////////////////////////////////////////////////////
  //test#2 Jack detect
  ///////////////////////////////////////////////////////
  headerL("test#2 : Jack detect", "plug an audio cable", TFT_NAVY);
  tft.fillRect(130, 100, 70, 70, TFT_WHITE);
  tft.fillRect(132, 102, 66, 66, TFT_NAVY);
  int ndj = 0;
  while (gpio_get_level(JACK_DETECT) == 1) delay(50);
  delay(500);
  tft.fillRect(132, 102, 66, 66, TFT_NAVY);
  delay(500);
  tft.setTextColor(TFT_RED);
  tft.drawString("OK", 170, 125, 4);
  delay(2000);



  ///////////////////////////////////////////////////////
  //test#2 USB detect
  ///////////////////////////////////////////////////////
  headerL("test#2 : USB detect", "plug an usb cable", TFT_NAVY);
  tft.fillRect(130, 100, 70, 70, TFT_WHITE);
  tft.fillRect(132, 102, 66, 66, TFT_NAVY);
  while (gpio_get_level(USB_DETECT) == 0) delay(50);
  delay(500);
  tft.fillRect(132, 102, 66, 66, TFT_NAVY);
  delay(500);
  tft.setTextColor(TFT_RED);
  tft.drawString("OK", 170, 125, 4);
  delay(2000);


  ///////////////////////////////////////////////////////
  //test#3 volume encoder
  ///////////////////////////////////////////////////////
  headerL("test#3 : Volume encoder", "Turn right and left then click to terminate", TFT_NAVY);
  tft.fillRect(130, 100, 70, 70, TFT_WHITE);
  tft.fillRect(132, 102, 66, 66, TFT_NAVY);
#define maxC 10
  volEncoder.setCount(0);
  tft.setTextColor(TFT_RED);
  tft.setTextDatum(TC_DATUM);
  while (volEncoder.getCount() < maxC)
  {
    sprintf(buf, "%d\n", volEncoder.getCount());
    tft.fillRect(150, 110, 35, 35, TFT_NAVY);
    tft.drawString(buf, 170, 125, 4);
    delay(100);
  }
  volEncoder.setCount(maxC);
  while (volEncoder.getCount() > -maxC)
  {
    if (volEncoder.getCount() > maxC) continue;
    sprintf(buf, "%d\n", volEncoder.getCount());
    tft.fillRect(150, 110, 35, 35, TFT_NAVY);
    tft.drawString(buf, 170, 125, 4);
    delay(100);
  }
  sprintf(buf, "%d\n", -maxC);
  tft.fillRect(150, 110, 35, 35, TFT_NAVY);
  tft.drawString(buf, 170, 125, 4);

  while (gpio_get_level(CLICK1) == 1) delay(50);
  tft.fillRect(150, 110, 35, 35, TFT_NAVY);
  tft.drawString("OK", 170, 125, 4);
  delay(1000);
  tft.fillRect(132, 102, 66, 66, TFT_NAVY);


  ///////////////////////////////////////////////////////
  // test#4 stations encoder
  ///////////////////////////////////////////////////////
  headerL("test#4 : Stations encoder", "Turn right and left then click to terminate", TFT_NAVY);
  tft.fillRect(130, 100, 70, 70, TFT_WHITE);
  tft.fillRect(132, 102, 66, 66, TFT_NAVY);
  staEncoder.setCount(0);
  tft.setTextColor(TFT_RED);
  tft.setTextDatum(TC_DATUM);
  while (staEncoder.getCount() < maxC)
  {
    sprintf(buf, "%d\n", staEncoder.getCount());
    tft.fillRect(150, 110, 35, 35, TFT_NAVY);
    tft.drawString(buf, 170, 125, 4);
    delay(100);
  }
  staEncoder.setCount(maxC);
  while (staEncoder.getCount() > -maxC)
  {
    if (staEncoder.getCount() > maxC) continue;
    sprintf(buf, "%d\n", staEncoder.getCount());
    tft.fillRect(150, 110, 35, 35, TFT_NAVY);
    tft.drawString(buf, 170, 125, 4);
    delay(100);
  }

  sprintf(buf, "%d\n", -maxC);
  tft.fillRect(150, 110, 35, 35, TFT_NAVY);
  tft.drawString(buf, 170, 125, 4);
  while (gpio_get_level(CLICK2) == 1) delay(50);
  tft.fillRect(150, 110, 35, 35, TFT_NAVY);
  tft.drawString("OK", 170, 125, 4);
  delay(1000);


  ///////////////////////////////////////////////////////
  // test#5 Battery
  ///////////////////////////////////////////////////////
#define adcVMAX  1000
#define adcVMIN   200
  headerL("test#5 : Battery adc line", "Nothing to do...", TFT_NAVY);
  tft.fillRect(100, 100, 130, 70, TFT_WHITE);
  tft.fillRect(102, 102, 126, 66, TFT_NAVY);
  adcValue = analogRead(BAT_GAUGE_PIN) - zeroVal;
  sprintf((char*)b, "V = %d\n", adcValue);
  tft.setTextColor(TFT_RED);
  tft.drawString((const char*)b, 170, 125, 4);
  delay(2000);
  if ((adcValue < adcVMAX) && (adcValue > adcVMIN))
  {
    tft.fillRect(102, 102, 126, 66, TFT_NAVY);
    tft.drawString("OK", 170, 125, 4);
    delay(1000);
  }
  else return;


  ///////////////////////////////////////////////////////
  // test#6 Remote
  ///////////////////////////////////////////////////////
  headerL("test#6: Remote", "Press the OK button", TFT_NAVY);
  tft.fillRect(100, 100, 130, 70, TFT_WHITE);
  tft.fillRect(102, 102, 126, 66, TFT_NAVY);
  tft.setTextColor(TFT_RED, TFT_NAVY);
  tft.setTextDatum(TC_DATUM);
  irrecv.enableIRIn();
  uint32_t v;
  while (v != 0x609F)
  {
    if (irrecv.decode(&results)) {
      if (results.decode_type == NEC)
      {
        v = results.value;
        if (v != 0xffffffff) v = v & 0xFFFF;
      }
      irrecv.resume();  // Receive the next value

      delay(100);
    }
  }
  tft.fillRect(102, 102, 126, 66, TFT_NAVY);
  tft.drawString("OK", 170, 125, 4);
  delay(1000);

  ///////////////////////////////////////////////////////
  // test#7 SD
  ///////////////////////////////////////////////////////
  headerL("test#7: SD", "Nothing todo...", TFT_NAVY);
  tft.fillRect(50, 100, 220, 40, TFT_WHITE);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.setTextColor(TFT_RED, TFT_NAVY);
  tft.setTextDatum(TC_DATUM);

  if (! SD_MMC.setPins(clk, cmd, d0)) {
    printf("Pin change failed!\n");
    tft.drawString("SD Pin change failed", 160, 110, 2);
    return;
  }
  if (!SD_MMC.begin("/sdcard", true)) {
    printf("Card Mount Failed\n");
    tft.drawString("Card mount failed", 160, 110, 2);
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    printf("No SD_MMC card attached\n");
    tft.drawString("No SD_MMC card attached", 160, 110, 2);
    return;
  }
  printf("SD init OK\n");
  tft.drawString("SD init OK", 160, 110, 4);
  File f = SD_MMC.open("/test", FILE_WRITE);
  f.write((const uint8_t*)"1234567890", 11);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.drawString("Writing...", 160, 110, 4);
  f.close();
  delay(1000);
  f = SD_MMC.open("/test", FILE_READ);
  char bsd[15];
  f.read((uint8_t*)bsd, 15);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.drawString("Reading...", 160, 110, 4);
  f.close();
  delay(1000);
  if (strcmp(bsd, "1234567890") != 0) return;
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.drawString("OK", 160, 110, 4);


  ///////////////////////////////////////////////////////
  // test#8 WiFi
  ///////////////////////////////////////////////////////
  headerL("test#8: WiFi-> xhkap", "Nothing todo...", TFT_NAVY);
  tft.fillRect(50, 100, 220, 40, TFT_WHITE);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.setTextColor(TFT_RED, TFT_NAVY);
  tft.setTextDatum(TC_DATUM);

  tft.drawString("Connecting...", 160, 110, 4);
#define TOMax 22

  WiFi.mode(WIFI_STA);
  WiFi.begin("xhkap", "12345678");
  int to = 0;
  while ((!WiFi.isConnected()) && (to < TOMax))
  {
    to++;
    delay(1000);
  }
  printf("%d   %d\n", to, WiFi.isConnected());
  if (to < TOMax)
  {
    printf("Connected to xhkap(4G)\n");
    tft.fillRect(52, 102, 216, 36, TFT_NAVY);
    tft.drawString("Connected...", 160, 110, 4);
  }
  else
  {
    tft.fillRect(52, 102, 216, 36, TFT_NAVY);
    tft.drawString("Connection failed...", 160, 110, 4);
    for (;;);
  }
  delay(1000);
  sprintf((char*)b, "RSSI = %d dB\n", WiFi.RSSI());
  tft.setTextColor(TFT_RED, TFT_NAVY);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.drawString((const char*)b, 160, 110, 4);
  delay(2000);

  ///////////////////////////////////////////////////////
  // test#9 Audio => speaker
  ///////////////////////////////////////////////////////
#if ENABLE_LEGACY_FACTORY_I2S
  headerL("test#9 Audio => Speaker", "nothing to do...", TFT_NAVY);
  tft.fillRect(50, 100, 220, 40, TFT_WHITE);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.setTextColor(TFT_RED, TFT_NAVY);
  tft.setTextDatum(TC_DATUM);
  //I2S init
  i2s_driver_install(I2SW, &i2s_config_write, 0, NULL);
  i2s_set_pin(I2SW, &pin_config_write);
  i2s_set_clk(I2SW, 8000, (i2s_bits_per_sample_t)16, (i2s_channel_t)2);
  i2s_zero_dma_buffer(I2SW);
  i2s_stop(I2SW);
  delay(1000);


  //ES8388 codec init
  Wire.setPins(SDA, SCL);
  Wire.begin();
  res = ES8388_Init();
  delay(500);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.drawString("Playing...", 160, 110, 4);
  printf("mono\n");
  gpio_set_level(PA, 1);
  //mono (L+R)/2
  ES8388_Write_Reg(29, 0x20);
  // Right DAC phase inversion + click free power up/down
  ES8388_Write_Reg(28, 0x14);
  // DAC power-up LOUT1/ROUT1 enabled
  ES8388_Write_Reg(4, 0x30);
  i2s_start(I2SW);
  LittleFS.begin();
  int n;
  size_t t;
  f = LittleFS.open("/leftright.wav", FILE_READ);
  f.seek(44);
  do
  {
    n = f.read(b, BLOCK_SIZE );
    i2s_write(I2SW, b, n, &t, portMAX_DELAY);
  } while (n > 0);
  i2s_zero_dma_buffer(I2SW);

  f.close();
  delay(500);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.drawString("  OK", 160, 110, 4);
  delay(1000);

  ///////////////////////////////////////////////////////
  // test#10 Audio => headphones
  ///////////////////////////////////////////////////////
  headerL("test#10 Audio => HP", "plug in HP ", TFT_NAVY);
  tft.fillRect(50, 100, 220, 40, TFT_WHITE);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.setTextColor(TFT_RED, TFT_NAVY);
  tft.drawString("Playing...", 160, 110, 4);
  tft.setTextDatum(TC_DATUM);
  while (gpio_get_level(JACK_DETECT) == 1) delay(50);
  delay(1000);
  gpio_set_level(PA, 0);
  //stereo
  ES8388_Write_Reg(29, 0x00);
  // no Right DAC phase inversion + click free power up/down
  ES8388_Write_Reg(28, 0x10);
  // DAC power-up LOUT2/ROUT2 enabled
  ES8388_Write_Reg(4, 0x0C);
  f = LittleFS.open("/leftright.wav", FILE_READ);
  f.seek(44);
  do
  {
    n = f.read(b, BLOCK_SIZE );
    i2s_write(I2SW, b, n, &t, portMAX_DELAY);
  } while (n > 0);
  i2s_zero_dma_buffer(I2SW);
  i2s_stop(I2SW);
  f.close();
  delay(500);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.drawString("  OK", 160, 110, 4);
  delay(1000);


  ///////////////////////////////////////////////////////////////////
  // test #11 Microphones
  ///////////////////////////////////////////////////////////////////
  headerL("test#11 : Microphones", "Click1 and speak", TFT_NAVY);
  tft.fillRect(50, 100, 220, 40, TFT_WHITE);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.setTextColor(TFT_RED, TFT_NAVY);
  tft.setTextDatum(TC_DATUM);
  i2s_driver_install(I2SR, &i2s_config_read, 0, NULL);
  i2s_set_pin(I2SR, &pin_config_read);
  i2s_set_clk(I2SR, 8000, (i2s_bits_per_sample_t)16, (i2s_channel_t)2);
  i2s_stop(I2SR);
  i2s_start(I2SR);
  i2s_zero_dma_buffer(I2SR);
  printf("mono\n");
  gpio_set_level(PA, 1);
  //mono (L+R)/2
  ES8388_Write_Reg(29, 0x20);
  // Right DAC phase inversion + click free power up/down
  ES8388_Write_Reg(28, 0x14);
  // DAC power-up LOUT1/ROUT1 enabled
  ES8388_Write_Reg(4, 0x30);
#define bytesToRead 128
  // uint8_t b[bytesToRead];
  printf("0\n");
  RECB = false;
  while (RECB == false) delay(10);

  i2s_start(I2SR);
  RECB = false;
  testON = true;
  printf("1\n");
  f = SD_MMC.open("/record.wav", FILE_WRITE);
  f.seek(44);
  //    int n;
  int i;
  //   size_t t;
  uint32_t s = 44;
  headerL("test#11 : Microphones", "Click1 to stop recording", TFT_NAVY);
  while (RECB == false)
  {
    i = 0;
    n = 0;
    do
    {
      while (n == 0) i2s_read(I2SR, &b[i], BLOCK_SIZE, (size_t*)&n, portMAX_DELAY);
      i = i + n;
    } while ((i < bytesToRead) && (RECB == false));

    f.write(b, i);
    s += i;
  }

  printf("writing header...\n");
  f.seek(0);
  header[40] = s & 0xFF;
  header[41] = (s >> 8) & 0xFF;
  header[42] = (s >> 16) & 0xFF;
  header[43] = (s >> 24) & 0xFF;
  header[4] = (s - 8) & 0xFF;
  header[5] = ((s - 8) >> 8) & 0xFF;
  header[6] = ((s - 8) >> 16) & 0xFF;
  header[7] = ((s - 8) >> 24) & 0xFF;
  f.write(header, 44);
  printf("end\n");
  f.seek(s);
  f.close();
  delay(100);
  RECB = false;
  //   while (RECB == false) delay(10);
  headerL("test#11 : Microphones", "Playing...", TFT_NAVY);
  i2s_stop(I2SR);
  i2s_set_pin(I2SW, &pin_config_write);
  i2s_set_clk(I2SW, 8000, (i2s_bits_per_sample_t)16, (i2s_channel_t)2);
  i2s_start(I2SW);
  f = SD_MMC.open("/record.wav", FILE_READ);
  f.seek(44);
  do
  {
    n = f.read(b, BLOCK_SIZE);
    i2s_write(I2SW, b, n, &t, portMAX_DELAY);
  } while (n > 0);
  i2s_zero_dma_buffer(I2SW);
  i2s_stop(I2SW);
  f.close();
  RECB = false;
  testON = false;
#else
  headerL("test#9-11 Audio I2S", "disabled on Arduino 3", TFT_NAVY);
  delay(1000);
#endif


  delay(500);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(TC_DATUM);
  tft.fillScreen(TFT_NAVY);

  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.drawString("  OK", 160, 110, 4);
  delay(1000);

  //////////////////////////////////////////////////////////////////
  // initialisation temps NTP
  //
  ////////////////////////////////////////////////////////////////////
  // time zone init
  setenv("TZ", "CEST-1", 1);
  tzset();
  //sntp init
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, "pool.ntp.org");
  sntp_init();
  int retry = 0;
  while ((timeinfo.tm_year < (2016 - 1900)) && (++retry < 20))
  {
    delay(500);
    time(&now);
    localtime_r(&now, &timeinfo);
  }
  time(&now);
  localtime_r(&now, &timeinfo);
  sprintf(timeStr, "ID: %s Ok on : %4d %02d %02d  %02d:%02d:%02d\n", WiFi.macAddress().c_str(), timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  printf("==>> %s\n", timeStr);



  ////////////////////////////////////////////////////////////
  // connecting to Google sheets
  ////////////////////////////////////////////////////////////
  delay(3000);
  printf("Google sheets\n");
  //    String uniqueID = String((uint32_t)(ESP.getEfuseMac() >> 32), HEX) + String((uint32_t)ESP.getEfuseMac(), HEX) ;
  String uniqueID = String(timeStr);
  String data = "id=" + uniqueID;
  printf("1\n");
  const char* googleScriptURL = "https://script.google.com/macros/s/AKfycbyNtmE7-G77xtEAAL8aoCWozyrNJcV2hoqTdlHBWbix1YImqDyb7lI8znLH3R11cq_s/exec";
  printf("11\n");
  HTTPClient http;
  printf("12\n");
  http.begin(googleScriptURL);
  printf("13\n");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  printf("14\n");
  int httpResponseCode = http.POST(data);
  printf("15\n");
  delay(3000);
  printf("2\n");
  if (httpResponseCode > 0) {
    String response = http.getString();
    printf("3\n");
    printf("%x\n", httpResponseCode);
    Serial.println(response);
  } else {
    Serial.print("Error on sending POST: ");
    Serial.println(httpResponseCode);
  }

  http.end();

  tft.fillRect(50, 100, 220, 40, TFT_WHITE);
  tft.fillRect(52, 102, 216, 36, TFT_NAVY);
  tft.drawString("Wait...", 160, 110, 4);
  delay(2000);
  tft.setRotation(1);
  headerS("Ros&Co", TFT_NAVY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("TEST OK", 160, 115, 4);
  delay(2000);
  ESP.restart();
}
