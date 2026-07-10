#include "Arduino.h"
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
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "qrcodeR.h"
//#include "USB.h"
//#include <IRremoteESP8266.h>
#include <IRrecv.h>
//#include <IRutils.h>
#include <FS.h>
#include <ArduinoJson.h>

PNG png;
#define MAX_IMAGE_WDITH 320 // Adjust for your images




#define version "V1.0-radio"

#define I2S_DOUT        17
#define I2S_BCLK        5
#define I2S_LRC         16
#define I2S_MCLK        0
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
#define ADC_SETTINGS_HOLD_MS 5000
#define CAPTIVE_IDLE_RETURN_MS 60000
static const int ADC_BUTTON_REFERENCE[] = {1863, 2355, 486, 930};
static const int ADC_BUTTON_COUNT = sizeof(ADC_BUTTON_REFERENCE) / sizeof(ADC_BUTTON_REFERENCE[0]);

TaskHandle_t radioH, keybH, batteryH, jackH, remoteH, displayONOFFH;
SemaphoreHandle_t codecMutex = nullptr;
Audio audio;
static void audioEvent(Audio::msg_t msg);
void settings(void);
int button_get_level(int nb);
static void debugAdcButtonsTick();
static int readAdcButtonIndex(int *rawOut, int *diffOut);
WiFiMulti wifiMulti;              //////////////////////////////////////
#define maxVol 31
#define pos360 31
int vol = maxVol;
int Pvol;
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
struct CachedRadioPreset {
  char name[RADIO_NAME_MAX];
  char url[RADIO_URL_MAX];
};
static CachedRadioPreset *radioPresetCache = nullptr;
static int radioPresetCacheCount = 0;
static bool radioPresetCacheInPsram = false;
static bool reloadRadioPresetCache();

static bool commitTempFile(const String &tempPath, const char *targetPath)
{
  String backupPath = String(targetPath) + ".bak";
  LittleFS.remove(backupPath);
  const bool hadTarget = LittleFS.exists(targetPath);
  if (hadTarget && !LittleFS.rename(targetPath, backupPath)) {
    LittleFS.remove(tempPath);
    return false;
  }
  if (!LittleFS.rename(tempPath, targetPath)) {
    if (hadTarget) LittleFS.rename(backupPath, targetPath);
    LittleFS.remove(tempPath);
    return false;
  }
  if (hadTarget) LittleFS.remove(backupPath);
  return true;
}
static bool writeTextAtomically(const char *path, const String &content)
{
  String tempPath = String(path) + ".tmp";
  LittleFS.remove(tempPath);
  File f = LittleFS.open(tempPath, FILE_WRITE);
  if (!f) return false;
  const size_t written = f.write((const uint8_t *)content.c_str(), content.length());
  f.flush();
  f.close();
  if (written != content.length()) {
    LittleFS.remove(tempPath);
    return false;
  }
  return commitTempFile(tempPath, path);
}

static bool stageTextFile(const String &tempPath, const String &content)
{
  LittleFS.remove(tempPath);
  File f = LittleFS.open(tempPath, FILE_WRITE);
  if (!f) return false;
  const size_t written = f.write((const uint8_t *)content.c_str(), content.length());
  f.flush();
  f.close();
  if (written == content.length()) return true;
  LittleFS.remove(tempPath);
  return false;
}

static bool writeTextPairAtomically(const char *firstPath, const String &firstContent,
                                    const char *secondPath, const String &secondContent)
{
  String firstTemp = String(firstPath) + ".tmp";
  String secondTemp = String(secondPath) + ".tmp";
  String firstBackup = String(firstPath) + ".bak";
  String secondBackup = String(secondPath) + ".bak";
  if (!stageTextFile(firstTemp, firstContent) || !stageTextFile(secondTemp, secondContent)) {
    LittleFS.remove(firstTemp);
    LittleFS.remove(secondTemp);
    return false;
  }

  LittleFS.remove(firstBackup);
  LittleFS.remove(secondBackup);
  const bool hadFirst = LittleFS.exists(firstPath);
  const bool hadSecond = LittleFS.exists(secondPath);
  if (hadFirst && !LittleFS.rename(firstPath, firstBackup)) goto rollback_staged;
  if (hadSecond && !LittleFS.rename(secondPath, secondBackup)) {
    if (hadFirst) LittleFS.rename(firstBackup, firstPath);
    goto rollback_staged;
  }
  if (!LittleFS.rename(firstTemp, firstPath)) goto rollback_backups;
  if (!LittleFS.rename(secondTemp, secondPath)) {
    LittleFS.remove(firstPath);
    goto rollback_backups;
  }

  if (hadFirst) LittleFS.remove(firstBackup);
  if (hadSecond) LittleFS.remove(secondBackup);
  return true;

rollback_backups:
  LittleFS.remove(firstPath);
  LittleFS.remove(secondPath);
  if (hadFirst) LittleFS.rename(firstBackup, firstPath);
  if (hadSecond) LittleFS.rename(secondBackup, secondPath);
rollback_staged:
  LittleFS.remove(firstTemp);
  LittleFS.remove(secondTemp);
  return false;
}

static void recoverAtomicFile(const char *path)
{
  const String tempPath = String(path) + ".tmp";
  const String backupPath = String(path) + ".bak";
  LittleFS.remove(tempPath);
  if (!LittleFS.exists(path) && LittleFS.exists(backupPath)) {
    LittleFS.rename(backupPath, path);
  } else if (LittleFS.exists(path)) {
    LittleFS.remove(backupPath);
  }
}

static void recoverAtomicPair(const char *firstPath, const char *secondPath)
{
  const String firstTemp = String(firstPath) + ".tmp";
  const String secondTemp = String(secondPath) + ".tmp";
  const String firstBackup = String(firstPath) + ".bak";
  const String secondBackup = String(secondPath) + ".bak";
  LittleFS.remove(firstTemp);
  LittleFS.remove(secondTemp);

  const bool pairIncomplete = !LittleFS.exists(firstPath) || !LittleFS.exists(secondPath);
  const bool hasBackup = LittleFS.exists(firstBackup) || LittleFS.exists(secondBackup);
  if (pairIncomplete && hasBackup) {
    LittleFS.remove(firstPath);
    LittleFS.remove(secondPath);
    if (LittleFS.exists(firstBackup)) LittleFS.rename(firstBackup, firstPath);
    if (LittleFS.exists(secondBackup)) LittleFS.rename(secondBackup, secondPath);
  } else {
    LittleFS.remove(firstBackup);
    LittleFS.remove(secondBackup);
  }
}

static void recoverStoredFiles()
{
  recoverAtomicFile(WIFI_JSON_PATH);
  recoverAtomicFile(RADIO_CATALOG_PATH);
  recoverAtomicFile("/station");
  recoverAtomicFile("/volume");
  recoverAtomicPair(RADIO_NAME_PATH, RADIO_LINK_PATH);
}

static int loadStoredInt(const char *path, int fallback, int minimum, int maximum)
{
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return fallback;
  String value = f.readString();
  f.close();
  value.trim();
  if (!value.length()) return fallback;
  char *end = nullptr;
  long parsed = strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != 0 || parsed < minimum || parsed > maximum) return fallback;
  return (int)parsed;
}

static bool saveStoredInt(const char *path, int value)
{
  return writeTextAtomically(path, String(value));
}

// Load stored WiFi credentials from LittleFS JSON into an array.
// Returns count loaded. Backward-compatible: if only /ssid and /pwd exist,
// migrate them into JSON on first run.
int loadWifiList(WifiCred list[], int maxItems)
{
  int count = 0;
  File f = LittleFS.open(WIFI_JSON_PATH, FILE_READ);
  if (f) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    JsonArray networks = doc["networks"].as<JsonArray>();
    if (!err && !networks.isNull()) {
      for (JsonObject obj : networks) {
        if (count >= maxItems) break;
        const char* s = obj["ssid"] | "";
        const char* p = obj["pwd"] | "";
        const size_t ssidLength = strlen(s);
        const size_t passwordLength = strlen(p);
        if (ssidLength == 0 || ssidLength > 32 || passwordLength > 64) continue;
        bool duplicate = false;
        for (int i = 0; i < count; i++) {
          if (strcmp(list[i].ssid, s) == 0) {
            duplicate = true;
            break;
          }
        }
        if (duplicate) continue;
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
    if (sn > 0 && sn <= 32 && pn <= 64) {
      strncpy(list[0].ssid, (char*)ssid, sizeof(list[0].ssid)); list[0].ssid[sizeof(list[0].ssid)-1]=0;
      strncpy(list[0].pwd, (char*)pwd, sizeof(list[0].pwd)); list[0].pwd[sizeof(list[0].pwd)-1]=0;
      count = 1;
    }
  }
  return count;
}

bool saveWifiList(const WifiCred list[], int count)
{
  JsonDocument doc;
  JsonArray arr = doc["networks"].to<JsonArray>();
  for (int i = 0; i < count; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = list[i].ssid;
    o["pwd"]  = list[i].pwd;
  }
  String json;
  json.reserve(2048);
  if (serializeJson(doc, json) == 0) return false;
  return writeTextAtomically(WIFI_JSON_PATH, json);
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
char b[8];
char s[80];
bool started = false;
uint32_t lastModTime;
bool modSta = false;
bool jackON = false;
bool Bvalid = false;
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
}

#define ES8388_ADDR 0x10
///////////////////////////////////////////////////////////////////////
// Write ES8388 register (using I2c)
///////////////////////////////////////////////////////////////////////
uint8_t ES8388_Write_Reg(uint8_t reg, uint8_t val)
{
  if (codecMutex) xSemaphoreTakeRecursive(codecMutex, portMAX_DELAY);
  uint8_t buf[2], res;
  buf[0] = reg;
  buf[1] = val;
  Wire.beginTransmission(ES8388_ADDR);
  Wire.write(buf, 2);
  res = Wire.endTransmission();
  if (codecMutex) xSemaphoreGiveRecursive(codecMutex);
  return res;
}

//////////////////////////////////////////////////////////////////
//
// init CODEC chip ES8388 (via I2C)
//
////////////////////////////////////////////////////////////////////
//
int ES8388_Init(void)
{
  if (codecMutex) xSemaphoreTakeRecursive(codecMutex, portMAX_DELAY);
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


  // ADC amp 24dB
  ES8388_Write_Reg(9, 0x88);


  // differential input
  ES8388_Write_Reg(10, 0xFC);
  ES8388_Write_Reg(11, 0x02);


  //Select LIN2and RIN2 as differential input pairs
  //ES8388_Write_Reg(11,0x82);

  //i2S 16b
  ES8388_Write_Reg(12, 0x0C);
  //MCLK 256
  ES8388_Write_Reg(13, 0x02);
  // ADC high pass filter
  // ES8388_Write_Reg(14,0x30);

  // ADC Volume LADC volume = 0dB
  ES8388_Write_Reg(16, 0x00);

  // ADC Volume RADC volume = 0dB
  ES8388_Write_Reg(17, 0x00);

  // ALC
  ES8388_Write_Reg(0x12, 0xfd); // Reg 0x12 = 0xe2 (ALC enable, PGA Max. Gain=23.5dB, Min. Gain=0dB)
  //ES8388_Write_Reg(0x12, 0x22); // Reg 0x12 = 0xe2 (ALC enable, PGA Max. Gain=23.5dB, Min. Gain=0dB)
  ES8388_Write_Reg( 0x13, 0xF9); // Reg 0x13 = 0xc0 (ALC Target=-4.5dB, ALC Hold time =0 mS)
  ES8388_Write_Reg( 0x14, 0x02); // Reg 0x14 = 0x12(Decay time =820uS , Attack time = 416 uS)
  ES8388_Write_Reg( 0x15, 0x06); // Reg 0x15 = 0x06(ALC mode)
  ES8388_Write_Reg( 0x16, 0xc3); // Reg 0x16 = 0xc3(nose gate = -40.5dB, NGG = 0x01(mute ADC))
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
  if (codecMutex) xSemaphoreGiveRecursive(codecMutex);
  return st;

}


int delay1 = 10;
int delay2 = 500;

volatile int station = 0;
int previousStation;
int MS;
volatile bool connected = false;
volatile bool stationDisplayPending = false;
bool volumeSavePending = false;
uint32_t volumeChangedAtMs = 0;
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
  if (codecMutex) xSemaphoreTakeRecursive(codecMutex, portMAX_DELAY);
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
  if (codecMutex) xSemaphoreGiveRecursive(codecMutex);
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
static uint32_t captiveRestartAtMs = 0;
static bool captiveRestartToRadio = false;
static bool captiveReturnToRadioEnabled = false;
static bool captiveManualFallbackRequested = false;
static WifiCred captiveWifiList[WIFI_MAX_NETWORKS];

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
  "France Culture|https://stream.radiofrance.fr/franceculture/franceculture_hifi.m3u8\n"
  "FIP|http://direct.fipradio.fr/live/fip-hifi.aac\n"
  "RTL|http://icecast.rtl.fr/rtl-1-44-128\n"
  "RMC|https://audio.bfmtv.com/rmcradio_128.mp3\n"
  "France Info|http://direct.franceinfo.fr/live/franceinfo-hifi.aac\n"
  "Radio Classique|http://radioclassique.ice.infomaniak.ch/radioclassique-high.mp3\n"
  "France Musique|http://direct.franceinter.fr/live/francemusique-hifi.aac\n"
  "Radio Crooner|http://croonerradio.ice.infomaniak.ch/croonerradio-hifi.mp3\n"
  "Europe 1|https://europe1.lmn.fm/europe1.mp3\n"
  "Sud Radio|http://start-sud.ice.infomaniak.ch/start-sud-high.mp3\n"
  "France Inter|http://direct.franceinter.fr/live/franceinter-hifi.aac\n"
  "Frequence Jazz|http://broadcast.infomaniak.ch/frequencejazz-high.mp3\n"
  "Le Mouv|http://direct.mouv.fr/live/mouv-midfi.mp3\n"
  "Suisse Premiere|http://stream.srg-ssr.ch/m/la-1ere/mp3_128\n"
  "RTBF|http://broadcast.infomaniak.ch/belrtl-mp3-128.mp3\n"
  "BFM Business|https://audio.bfmtv.com/bfmbusiness_128.mp3\n"
  "Rires et Chansons|https://streaming.nrjaudio.fm/ou8o8xgk7oiu?origine=fluxradios\n";

static const char DEFAULT_RADIO_CATALOG[] =
  "France Inter|General|FR|http://direct.franceinter.fr/live/franceinter-hifi.aac\n"
  "France Info|News|FR|http://direct.franceinfo.fr/live/franceinfo-hifi.aac\n"
  "France Culture|Culture|FR|https://stream.radiofrance.fr/franceculture/franceculture_hifi.m3u8\n"
  "France Musique|Classical|FR|http://direct.franceinter.fr/live/francemusique-hifi.aac\n"
  "FIP|Music|FR|http://direct.fipradio.fr/live/fip-hifi.aac\n"
  "FIP Jazz|Jazz|FR|http://direct.fipradio.fr/live/fip-webradio2.mp3\n"
  "FIP Groove|Funk|FR|http://direct.fipradio.fr/live/fip-webradio3.mp3\n"
  "FIP Rock|Rock|FR|http://direct.fipradio.fr/live/fip-webradio1.mp3\n"
  "Mouv|Hip-hop|FR|http://direct.mouv.fr/live/mouv-midfi.mp3\n"
  "RFI Monde|News|FR|http://live02.rfi.fr/rfimonde-96k.mp3\n"
  "RTL|Talk|FR|http://icecast.rtl.fr/rtl-1-44-128\n"
  "Europe 1|Talk|FR|https://europe1.lmn.fm/europe1.mp3\n"
  "RMC|Talk|FR|https://audio.bfmtv.com/rmcradio_128.mp3\n"
  "Sud Radio|Talk|FR|http://start-sud.ice.infomaniak.ch/start-sud-high.mp3\n"
  "BFM Business|News|FR|https://audio.bfmtv.com/bfmbusiness_128.mp3\n"
  "Radio Classique|Classical|FR|http://radioclassique.ice.infomaniak.ch/radioclassique-high.mp3\n"
  "Frequence Jazz|Jazz|FR|http://broadcast.infomaniak.ch/frequencejazz-high.mp3\n"
  "TSF Jazz|Jazz|FR|http://tsfjazz.ice.infomaniak.ch/tsfjazz-high.mp3\n"
  "Radio Nova|Eclectic|FR|http://novazz.ice.infomaniak.ch/novazz-128.mp3\n"
  "Radio Meuh|Eclectic|FR|http://radiomeuh.ice.infomaniak.ch/radiomeuh-128.mp3\n"
  "Radio Crooner|Jazz|FR|http://croonerradio.ice.infomaniak.ch/croonerradio-hifi.mp3\n"
  "Rires et Chansons|Comedy|FR|https://streaming.nrjaudio.fm/ou8o8xgk7oiu?origine=fluxradios\n"
  "Nostalgie|Oldies|FR|https://streaming.nrjaudio.fm/oug7girb92oc?origine=tingfm\n"
  "Cherie FM|Pop|FR|https://streaming.nrjaudio.fm/ou2n2epw5hs7\n"
  "NRJ|Pop|FR|http://cdn.nrjaudio.fm/audio1/fr/30001/aac_64.mp3?origine=fluxradios\n"
  "RTL2|Rock|FR|http://icecast.rtl2.fr/rtl2-1-44-128\n"
  "Fun Radio|Dance|FR|http://icecast.funradio.fr/fun-1-44-128\n"
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
  "BBC Radio 4|Talk|UK|http://as-hls-ww-live.akamaized.net/pool_55057080/live/ww/bbc_radio_fourfm/bbc_radio_fourfm.isml/bbc_radio_fourfm-audio%3d128000.norewind.m3u8\n"
  "BBC Radio 6 Music|Alternative|UK|http://as-hls-ww-live.akamaized.net/pool_81827798/live/ww/bbc_6music/bbc_6music.isml/bbc_6music-audio%3d128000.norewind.m3u8\n"
  "NPR News|News|US|https://npr-ice.streamguys1.com/live.mp3\n"
  "KEXP|Alternative|US|https://kexp-mp3-128.streamguys1.com/kexp128.mp3\n"
  "KCRW|Eclectic|US|https://streams.kcrw.com/kcrw_mp3\n"
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
  String data = DEFAULT_RADIO_PRESETS;
  String nameContent;
  String linkContent;
  nameContent.reserve(512);
  linkContent.reserve(1024);
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
        nameContent += name;
        nameContent += '\n';
        linkContent += url;
        linkContent += '\n';
        written++;
      }
    }
    start = end + 1;
  }

  return written > 0 && writeTextPairAtomically(RADIO_NAME_PATH, nameContent, RADIO_LINK_PATH, linkContent);
}

static bool writeDefaultCatalog()
{
  return writeTextAtomically(RADIO_CATALOG_PATH, String(DEFAULT_RADIO_CATALOG));
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
    writeTextAtomically(RADIO_LINK_PATH, content);
  }
}

static void migrateFranceCultureStream()
{
  static const char *legacyUrl =
    "http://direct.franceculture.fr/live/franceculture-hifi.aac";
  static const char *officialUrl =
    "https://stream.radiofrance.fr/franceculture/franceculture_hifi.m3u8";
  const char *paths[] = {RADIO_LINK_PATH, RADIO_CATALOG_PATH};

  for (const char *path : paths) {
    File in = LittleFS.open(path, FILE_READ);
    if (!in) continue;
    String content = in.readString();
    in.close();
    if (content.indexOf(legacyUrl) < 0) continue;
    content.replace(legacyUrl, officialUrl);
    const bool saved = writeTextAtomically(path, content);
    printf("France Culture stream migration path=%s ok=%d\n", path, saved ? 1 : 0);
  }

  File names = LittleFS.open(RADIO_NAME_PATH, FILE_READ);
  if (!names) return;
  String nameContent = names.readString();
  names.close();
  if (nameContent.indexOf("France-Culture") >= 0) {
    nameContent.replace("France-Culture", "France Culture");
    writeTextAtomically(RADIO_NAME_PATH, nameContent);
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
  migrateFranceCultureStream();
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

static bool saveRadioPresetCache(int count)
{
  if (radioPresetCache == nullptr) return false;
  if (count < 0) count = 0;
  if (count > RADIO_PRESET_MAX) count = RADIO_PRESET_MAX;
  while (count > 0 && radioPresetCache[count - 1].url[0] == '\0') count--;
  if (count <= 0) return false;

  String nameContent;
  String linkContent;
  nameContent.reserve(count * 24);
  linkContent.reserve(count * 96);

  for (int i = 0; i < count; i++) {
    CachedRadioPreset &preset = radioPresetCache[i];
    if (preset.url[0] == '\0') {
      nameContent += '\n';
      linkContent += '\n';
    } else {
      if (preset.name[0] == '\0') {
        snprintf(preset.name, sizeof(preset.name), "Preset %d", i + 1);
      }
      nameContent += preset.name;
      nameContent += '\n';
      linkContent += preset.url;
      linkContent += '\n';
    }
  }
  const bool saved = writeTextPairAtomically(
    RADIO_NAME_PATH, nameContent, RADIO_LINK_PATH, linkContent);
  if (saved) radioPresetCacheCount = count;
  return saved;
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

  if (!reloadRadioPresetCache() && radioPresetCache == nullptr) return false;
  int count = radioPresetCacheCount;
  if (slotIndex >= count) count = slotIndex + 1;
  name.toCharArray(radioPresetCache[slotIndex].name,
                   sizeof(radioPresetCache[slotIndex].name));
  url.toCharArray(radioPresetCache[slotIndex].url,
                  sizeof(radioPresetCache[slotIndex].url));
  bool saved = saveRadioPresetCache(count);
  if (!saved) reloadRadioPresetCache();
  printf("Radio preset save slot=%d count=%d ok=%d name=%s url=%s\n",
         slotIndex + 1, count, saved ? 1 : 0, name.c_str(), url.c_str());
  return saved;
}

static bool clearStationPreset(int slotIndex)
{
  if (slotIndex < 0 || slotIndex >= RADIO_PRESET_MAX) return false;
  if (!reloadRadioPresetCache() && radioPresetCache == nullptr) return false;
  int count = radioPresetCacheCount;
  if (slotIndex >= count) return false;
  memset(&radioPresetCache[slotIndex], 0, sizeof(radioPresetCache[slotIndex]));
  bool saved = saveRadioPresetCache(count);
  if (!saved) {
    if (!writeDefaultPresets()) {
      reloadRadioPresetCache();
      return false;
    }
    saved = reloadRadioPresetCache();
  }
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

static bool saveWifiCredential(const char *newSsid, const char *newPassword)
{
  if (newSsid == nullptr || newSsid[0] == 0 || strlen(newSsid) > 32) return false;
  if (newPassword == nullptr) newPassword = "";
  if (strlen(newPassword) > 64) return false;

  WifiCred list[WIFI_MAX_NETWORKS];
  int count = loadWifiList(list, WIFI_MAX_NETWORKS);
  const bool isNewNetwork = indexOfSsid(list, count, newSsid) < 0;
  const int previousCount = count;
  count = addOrUpdateWifi(list, count, newSsid, newPassword);
  if (isNewNetwork && count == previousCount) return false;
  return saveWifiList(list, count);
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
  reloadRadioPresetCache();
  String html;
  html.reserve(42000);
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

  int wifiCount = loadWifiList(captiveWifiList, WIFI_MAX_NETWORKS);
  html += F("<h2 style='margin-top:18px'>Saved networks</h2>");
  if (wifiCount == 0) {
    html += F("<p class='muted'>No saved network yet.</p>");
  } else {
    html += F("<table><tbody>");
    for (int i = 0; i < wifiCount; i++) {
      html += F("<tr><td>");
      html += htmlEscape(String(captiveWifiList[i].ssid));
      html += F("</td><td class='actions'><form method='POST' action='/wifi/forget'><input type='hidden' name='idx' value='");
      html += String(i);
      html += F("'><button class='small danger' type='submit'>Forget</button></form></td></tr>");
    }
    html += F("</tbody></table>");
  }
  html += F("</section>");

  int presetCount = radioPresetCacheCount;
  int nextSlot = 1;
  while (nextSlot <= RADIO_PRESET_MAX && radioPresetCache != nullptr &&
         radioPresetCache[nextSlot - 1].url[0] != '\0') nextSlot++;
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
    if (radioPresetCache[i].url[0] != '\0') {
      hasPreset = true;
      break;
    }
  }
  if (!hasPreset) {
    html += F("<p class='muted'>No preset yet.</p>");
  } else {
    html += F("<table><thead><tr><th class='slot'>#</th><th>Radio</th><th class='actions'></th></tr></thead><tbody>");
    for (int i = 0; i < presetCount; i++) {
      if (radioPresetCache[i].url[0] == '\0') continue;
      html += F("<tr><td class='slot'>");
      html += String(i + 1);
      html += F("</td><td><strong>");
      html += htmlEscape(String(radioPresetCache[i].name));
      html += F("</strong><div class='url'>");
      html += htmlEscape(String(radioPresetCache[i].url));
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

  if (!saveWifiCredential(finalSsid.c_str(), password.c_str())) {
    captiveServer.send(507, "text/html", F("<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body><h1>WiFi not saved</h1><p>Storage failed or the 10-network limit was reached. Forget one network and retry.</p><a href='/'>Back</a></body>"));
    return;
  }
  captiveServer.send(200, "text/html", F("<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body><h1>WiFi saved</h1><p>The radio will restart and connect automatically.</p></body>"));
  captiveRestartToRadio = false;
  captiveRestartPending = true;
  captiveRestartAtMs = millis() + 1500;
}

static void captiveHandleWifiForget()
{
  int idx = captiveServer.arg("idx").toInt();
  int count = loadWifiList(captiveWifiList, WIFI_MAX_NETWORKS);
  if (!removeWifiByIndex(captiveWifiList, count, idx) ||
      !saveWifiList(captiveWifiList, count)) {
    captiveServer.send(500, "text/html", F("<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body><h1>Network not forgotten</h1><a href='/'>Back</a></body>"));
    return;
  }
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
  if (!clearStationPreset(slot)) {
    captiveServer.send(400, "text/html", F("<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body><h1>Cannot clear preset</h1><a href='/'>Back</a></body>"));
    return;
  }
  portalRedirect("clear");
}

static void captiveHandleRadioDefaults()
{
  if (!writeDefaultPresets() || !reloadRadioPresetCache()) {
    captiveServer.send(500, "text/html", F("<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body><h1>Cannot restore presets</h1><a href='/'>Back</a></body>"));
    return;
  }
  portalRedirect("defaults");
}

static void captiveHandleRestart()
{
  captiveRestartToRadio = false;
  captiveRestartPending = true;
  captiveRestartAtMs = millis() + 250;
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
  if (captiveReturnToRadioEnabled) {
    tft.drawString("CHECK: back to radio", 160, 207, 2);
    tft.drawString("Knob: manual | Auto after 60s idle", 160, 226, 1);
  } else {
    tft.drawString("Press knob: manual setup", 160, 222, 2);
  }
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
  gpio_set_level(backLight, 1);
  forceTftPowerOn();
  tft.init();
  ensureLocalRadioData();
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  delay(200);

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  if (mac.length() < 4) mac = "0000";
  captiveApName = "MuseRadio-" + mac.substring(mac.length() - 4);
  captiveRestartPending = false;
  captiveRestartAtMs = 0;
  captiveRestartToRadio = false;
  captiveManualFallbackRequested = false;

  WiFi.softAPConfig(captivePortalIP, captivePortalIP, IPAddress(255, 255, 255, 0));
  bool apStarted = WiFi.softAP(captiveApName.c_str(), CAPTIVE_AP_PASSWORD);
  if (!apStarted) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_RED);
    tft.drawString("WiFi setup AP failed", 160, 105, 4);
    delay(2000);
    captiveManualFallbackRequested = true;
    return;
  }

  drawCaptivePortalScreen();
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

  uint32_t lastPortalClientMs = millis();
  uint32_t checkReleasedAtMs = 0;
  uint32_t checkPressedAtMs = 0;
  bool checkButtonArmed = false;
  while (true) {
    captiveDns.processNextRequest();
    captiveServer.handleClient();
    debugAdcButtonsTick();

    if (captiveReturnToRadioEnabled && !captiveRestartPending) {
      const uint32_t now = millis();
      if (WiFi.softAPgetStationNum() > 0) lastPortalClientMs = now;

      const int portalButton = readAdcButtonIndex(nullptr, nullptr);
      if (!checkButtonArmed) {
        if (portalButton != sw3) {
          if (checkReleasedAtMs == 0) checkReleasedAtMs = now;
          if ((now - checkReleasedAtMs) >= 250) checkButtonArmed = true;
        } else {
          checkReleasedAtMs = 0;
        }
      } else if (portalButton == sw3) {
        if (checkPressedAtMs == 0) checkPressedAtMs = now;
        if ((now - checkPressedAtMs) >= 50) {
          printf("Captive portal: CHECK pressed, returning to radio\n");
          captiveRestartToRadio = true;
          captiveRestartPending = true;
          captiveRestartAtMs = now + 250;
        }
      } else {
        checkPressedAtMs = 0;
      }

      if (!captiveRestartPending &&
          (now - lastPortalClientMs) >= CAPTIVE_IDLE_RETURN_MS) {
        printf("Captive portal: no AP client for %u ms, returning to radio\n",
               (unsigned)CAPTIVE_IDLE_RETURN_MS);
        captiveRestartToRadio = true;
        captiveRestartPending = true;
        captiveRestartAtMs = now + 250;
      }
    }

    if (captiveManualButtonPressed()) {
      waitCaptiveManualButtonReleased();
      captiveServer.stop();
      captiveDns.stop();
      WiFi.softAPdisconnect(true);
      captiveManualFallbackRequested = true;
      return;
    }
    if (captiveRestartPending &&
        (int32_t)(millis() - captiveRestartAtMs) >= 0) {
      tft.fillScreen(TFT_NAVY);
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(TFT_GREEN);
      tft.drawString(captiveRestartToRadio ? "Back to radio..." : "Restarting...",
                     160, 105, 4);
      delay(1000);
      esp_restart();
    }
    delay(10);
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

void drawImage(const char* f, int x, int y)
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

static int readAdcButtonIndex(int *rawOut, int *diffOut)
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
  uint32_t adcPressedAtMs = 0;
  bool adcLongPressSent = false;

  while (1)
  {
    int adcRaw = 0;
    int adcDiff = 0;
    int adcButton = readAdcButtonIndex(&adcRaw, &adcDiff);
    if (adcButton != adcHeldButton) {
      adcHeldButton = adcButton;
      adcPressedAtMs = millis();
      adcLongPressSent = false;
      if (adcButton >= sw0 && adcButton <= sw2) {
        pushAdcButtonEvent(adcButton);
        printf("ADC preset event button=%d raw=%d diff=%d\n",
               adcButton + 1, adcRaw, adcDiff);
      }
    } else if (adcButton == sw3 && !adcLongPressSent &&
               (millis() - adcPressedAtMs) >= ADC_SETTINGS_HOLD_MS) {
      adcLongPressSent = true;
      pushAdcButtonEvent(sw3);
      printf("ADC settings event: check button held %u ms\n",
             (unsigned)ADC_SETTINGS_HOLD_MS);
    }

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


static bool reloadRadioPresetCache()
{
  if (radioPresetCache == nullptr) {
    radioPresetCache = (CachedRadioPreset *)heap_caps_calloc(
      RADIO_PRESET_MAX, sizeof(CachedRadioPreset), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    radioPresetCacheInPsram = (radioPresetCache != nullptr);
    if (radioPresetCache == nullptr) {
      radioPresetCache = (CachedRadioPreset *)heap_caps_calloc(
        RADIO_PRESET_MAX, sizeof(CachedRadioPreset), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
  }
  if (radioPresetCache == nullptr) return false;
  memset(radioPresetCache, 0, RADIO_PRESET_MAX * sizeof(CachedRadioPreset));

  File nameFile = LittleFS.open(RADIO_NAME_PATH, FILE_READ);
  File linkFile = LittleFS.open(RADIO_LINK_PATH, FILE_READ);
  if (!nameFile || !linkFile) {
    if (nameFile) nameFile.close();
    if (linkFile) linkFile.close();
    radioPresetCacheCount = 0;
    return false;
  }

  int slot = 0;
  int highestUsedSlot = 0;
  while (slot < RADIO_PRESET_MAX && (nameFile.available() || linkFile.available())) {
    String name = nameFile.available() ? nameFile.readStringUntil('\n') : "";
    String url = linkFile.available() ? linkFile.readStringUntil('\n') : "";
    name.trim();
    url.trim();
    if (url.length() > 0 && isValidRadioUrl(url)) {
      if (!name.length()) name = "Preset " + String(slot + 1);
      name.toCharArray(radioPresetCache[slot].name, sizeof(radioPresetCache[slot].name));
      url.toCharArray(radioPresetCache[slot].url, sizeof(radioPresetCache[slot].url));
      highestUsedSlot = slot + 1;
    }
    slot++;
  }
  nameFile.close();
  linkFile.close();
  radioPresetCacheCount = highestUsedSlot;
  printf("Radio cache loaded: %d slots (%s)\n", radioPresetCacheCount,
         radioPresetCacheInPsram ? "PSRAM" : "internal RAM");
  return radioPresetCacheCount > 0;
}

char* Rlink(int st)
{
  static char empty[1] = {0};
  if (radioPresetCache == nullptr || st < 0 || st >= RADIO_PRESET_MAX) return empty;
  return radioPresetCache[st].url;
}

char* Rname(int st)
{
  static char fallback[RADIO_NAME_MAX];
  if (radioPresetCache != nullptr && st >= 0 && st < RADIO_PRESET_MAX && radioPresetCache[st].name[0]) {
    return radioPresetCache[st].name;
  }
  snprintf(fallback, sizeof(fallback), "Preset %d", st + 1);
  return fallback;
}

int maxStation(void)
{
  return radioPresetCacheCount;
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
  printf("ADC action check hold: open settings portal\n");
  captiveReturnToRadioEnabled = true;
  BSettings = true;
  toDisplay = 0;
  displayON();
  audio.stopSong();
  connected = false;
  stopRuntimeTasksForSettings();
  settings();
}

static void selectRadioPresetDirect(int presetIndex)
{
  if (presetIndex < 0 || presetIndex >= 3) return;
  displayON();
  if (!stationSlotHasUrl(presetIndex)) {
    wakeTftBacklight();
    tft.setRotation(1);
    tft.fillRect(80, 90, 240, 60, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString(("Preset " + String(presetIndex + 1) + " empty").c_str(), 180, 105, 4);
    printf("ADC preset %d ignored: empty\n", presetIndex + 1);
    return;
  }

  station = presetIndex;
  S = presetIndex * 2;
  VS = S;
  staEncoder.setCount(S);
  modSta = false;
  CLICK2B = false;
  stationDisplayPending = true;
  toDisplay = 2;
  if (!saveStoredInt("/station", station)) {
    printf("Failed to persist direct preset %d\n", presetIndex + 1);
  }
  printf("ADC direct preset=%d name=%s\n", presetIndex + 1, Rname(presetIndex));
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
  uint32_t nextConnectAttemptMs = 0;
  uint32_t retryDelayMs = 1000;
  while (1)
  {
    const int requestedStation = station;
    if (requestedStation != previousStation) {
      audio.stopSong();
      connected = false;
      previousStation = requestedStation;
      nextConnectAttemptMs = 0;
      retryDelayMs = 1000;
    }

    if (!connected && (int32_t)(millis() - nextConnectAttemptMs) >= 0) {
      if (WiFi.status() != WL_CONNECTED) {
        printf("Radio waiting for WiFi; retry in %u ms\n", retryDelayMs);
        wifiMulti.run(5000);
      }

      if (WiFi.status() == WL_CONNECTED) {
        const int activeStation = station;
        const char *url = Rlink(activeStation);
        if (!url || !url[0]) {
          station = normalizeStationSlot(activeStation, 1, maxStation());
          previousStation = -1;
        } else {
          printf("Radio connect preset=%d name=%s url=%s\n", activeStation + 1, Rname(activeStation), url);
          mes[0] = 0;
          audio.stopSong();
          connected = audio.connecttohost(url);
          if (connected) {
            retryDelayMs = 1000;
            stationDisplayPending = true;
            toDisplay = 2;
            refreshVolume();
          } else {
            printf("Radio connection rejected; retry in %u ms\n", retryDelayMs);
          }
        }
      }

      if (!connected) {
        nextConnectAttemptMs = millis() + retryDelayMs;
        retryDelayMs = retryDelayMs < 7500 ? retryDelayMs * 2 : 15000;
      }
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
        xSemaphoreTakeRecursive(codecMutex, portMAX_DELAY);
        gpio_set_level(PA, 0);          // amp off
        ES8388_Write_Reg(29, 0x00);     // stereo
        ES8388_Write_Reg(28, 0x04);     // no phase inversion + click free power up/down
        ES8388_Write_Reg(4, 0x0C);     // Rout2/Lout2
        jackON = true;
        refreshVolume();
        xSemaphoreGiveRecursive(codecMutex);
      }
    }
    else
    {
      if (jackON == true)
      {
        printf("Jack OFF\n");
        xSemaphoreTakeRecursive(codecMutex, portMAX_DELAY);
        gpio_set_level(PA, 1);          // amp on
        ES8388_Write_Reg(29, 0x20);     // mono (L+R)/2
        ES8388_Write_Reg(28, 0x14);     // Right DAC phase inversion + click free power up/down
        ES8388_Write_Reg(4, 0x30);      // Rout1/Lout1
        jackON = false;
        refreshVolume();
        xSemaphoreGiveRecursive(codecMutex);
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

    if (display == true)
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
  while (true)
  {
    displayT -= 100;
    if (displayT > 0) gpio_set_level(backLight, 1); else gpio_set_level(backLight, 0);
    delay(100);
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
  tft.drawString("WiFi", 50, 27, 2);


  tft.setTextColor(TFT_GREY, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);  // Set text datum to top-left
  tft.drawString("1/2/3: presets | Hold CHECK 5s: portal", 10, 220, 1);
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

  int j = 0;
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
      tft.drawString("No WiFi network found", 160, 110, 2);
      tft.drawString("Restarting portal...", 160, 145, 2);
      delay(2000);
      ESP.restart();
    }
    if (nSsid > 6) nSsid = 6;
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
        sprintf(s, "%d- %s", i + 1, WiFi.SSID(i).c_str());
        tft.drawString(s, 20, 80 + i * 25, GFXFF);
      }
      delay(100);
    }
    strcpy((char*)ssid, WiFi.SSID(j).c_str());
    printf("ssid = %s\n", ssid);
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
          strcat((char*)pwd, selectedChar);
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

    if (!saveWifiCredential((char*)ssid, (char*)pwd)) {
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(TFT_RED);
      tft.drawString("WiFi save failed", 160, 105, 4);
      delay(3000);
    }
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
void setup() {

  uint8_t res;
  Serial.begin(115200);
  const uint32_t serialWaitStartedAt = millis();
  while (!Serial && millis() - serialWaitStartedAt < 1000) delay(10);
  Serial.printf("\nMuse Radio %s booting\n", version);
  Serial.printf("PSRAM: %s, size=%u, free=%u\n",
         psramFound() ? "yes" : "no",
         (unsigned)ESP.getPsramSize(),
         (unsigned)ESP.getFreePsram());

  /////////////////////////////////////////////////////
  // Little FS init
  /////////////////////////////////////////////////////
  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS initialisation failed; preserving data and restarting in 5 seconds.");
    delay(5000);
    ESP.restart();
  }
  //    LittleFS.format();
  Serial.printf("LittleFS: used=%u total=%u bytes\n",
         (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  recoverStoredFiles();
  ensureLocalRadioData();
  if (!reloadRadioPresetCache()) {
    Serial.println("No valid local radio preset; restoring defaults.");
    if (!writeDefaultPresets() || !reloadRadioPresetCache()) {
      Serial.println("Radio preset storage failed; restarting in 5 seconds.");
      delay(5000);
      ESP.restart();
    }
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

  ///////////////////////////////////////////////////////
  //enable remote
  ///////////////////////////////////////////////////////
  irrecv.enableIRIn();
  forceTftPowerOn();
  tft.init();



  //////////////////////////////////////////////////////
  //Screen init
  //////////////////////////////////////////////////////
  Serial.printf("screen init...\n");
  //  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);
  drawImage("/Raspiaudio.png", 0, 0);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(version, 280, 220, 2);
  delay(2000);

  Serial.printf("WiFi\n");
  gpio_set_level(EN_4G, 0);
  ////////////////////////////////////////////////
  // WiFi init
  ////////////////////////////////////////////////
  started = false;
  WifiCred wifiList[WIFI_MAX_NETWORKS];
  int wifiCount = loadWifiList(wifiList, WIFI_MAX_NETWORKS);
  if (wifiCount == 0) {
    // No credentials yet, start the phone-friendly captive portal.
    captiveReturnToRadioEnabled = false;
    settings();
  }

  WiFi.useStaticBuffers(true);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  wifiMultiFromList(wifiMulti, wifiList, wifiCount);
  //   wifiMulti.run();
  const uint32_t connectTimeoutMs = 20000;
  if (wifiMulti.run(connectTimeoutMs) == WL_CONNECTED) {
    Serial.print("WiFi connected: ");
    Serial.print(WiFi.SSID());
    Serial.print(" ");
    Serial.println(WiFi.RSSI());
    started = true;

  }
  else {
    Serial.println("WiFi not connected!");
    tft.fillRect(0, 0, 320, 240, TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Connection failed...", 160, 75, 4);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("Starting WiFi setup...", 160, 125, 2);
    delay(1000);
    captiveReturnToRadioEnabled = false;
    settings();
  }


  //  delay(2000);
  ///////////////////////////////////////////////////////////////
  // Audio init
  //////////////////////////////////////////////////////////////
  //ES8388 codec init
  codecMutex = xSemaphoreCreateRecursiveMutex();
  if (codecMutex == nullptr) {
    Serial.println("Codec mutex allocation failed; restarting.");
    delay(2000);
    ESP.restart();
  }
  Wire.setPins(SDA, SCL);
  Wire.begin();
  res = 1;
  for (int attempt = 1; attempt <= 3 && res != 0; attempt++) {
    res = ES8388_Init();
    Serial.printf("Codec init attempt %d: %s (status=%u)\n", attempt, res == 0 ? "OK" : "failed", res);
    if (res != 0) delay(100);
  }
  if (res != 0) {
    gpio_set_level(PA, 0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_RED);
    tft.drawString("Audio codec error", 160, 95, 4);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Power cycle the radio", 160, 135, 2);
    while (true) delay(1000);
  }

  // audio lib init
  Audio::audio_info_callback = audioEvent;
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_MCLK);
  audio.setVolumeSteps(maxVol + 1);
  audio.setVolume(maxVol);

  // connections init
  if (gpio_get_level(JACK_DETECT) == 0)
  {
    Serial.printf("Jack ON\n");
    gpio_set_level(PA, 0);          // amp off
    ES8388_Write_Reg(29, 0x00);     // stereo
    ES8388_Write_Reg(28, 0x04);     // no phase inversion + click free power up/down
    ES8388_Write_Reg(4, 0x0C);     // Rout2/Lout2
    jackON = true;
    refreshVolume();
  }
  else
  {
    Serial.printf("Jack OFF\n");
    gpio_set_level(PA, 1);          // amp on
    ES8388_Write_Reg(29, 0x20);     // mono (L+R)/2
    ES8388_Write_Reg(28, 0x14);     // Right DAC phase inversion + click free power up/down
    ES8388_Write_Reg(4, 0x30);      // Rout1/Lout1
    jackON = false;
    refreshVolume();
  }

  ///////////////////////////////////////////////////////////////
  // recovering params (station & vol)
  ///////////////////////////////////////////////////////////////

  station = loadStoredInt("/station", 0, 0, RADIO_PRESET_MAX - 1);
  vol = loadStoredInt("/volume", 15, 0, maxVol);
  MS = maxStation() - 1;
  if (MS < 0) {
    writeDefaultPresets();
    MS = maxStation() - 1;
  }
  if (station < 0 || station > MS || !stationSlotHasUrl(station)) {
    station = normalizeStationSlot(0, 1, MS + 1);
    saveStoredInt("/station", station);
  }
  previousStation = -1;
  Serial.printf("station = %d    vol = %d\n", station, vol);
  // volume encoder init
  V = vol * pos360 / maxVol;
  volEncoder.setCount(V);
  PV = V;
  refreshVolume();
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
  tft.drawString("WiFi", 50, 27, 2);

  tft.setTextColor(TFT_GREY, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);  // Set text datum to top-left
  tft.drawString("1/2/3: presets | Hold CHECK 5s: portal", 10, 220, 1);
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
}


void loop() {

  if (stationDisplayPending) {
    stationDisplayPending = false;
    tft.setRotation(1);
    tft.fillRect(80, 90, 240, 60, TFT_BLACK);
    tft.setTextColor(TFT_STATION);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(Rname(station), 180, 105, 4);
  }
  for (int presetIndex = 0; presetIndex < 3; presetIndex++) {
    if (consumeAdcButtonEvent(presetIndex)) {
      selectRadioPresetDirect(presetIndex);
    }
  }

  if (consumeAdcButtonEvent(sw3))
  {
    if (BSettings == false) enterSettingsFromButton();
    else printf("ADC settings hold ignored: settings already active\n");
  }

  debugAdcButtonsTick();

  //////////////////////////////////////////////////////////////////////
  // Volume via encoder
  //////////////////////////////////////////////////////////////////////
  //  printf("------%x\n",REMOTE_KEY);
  V = volEncoder.getCount();
  if (V != PV)
  {
    PV = V;
    if (V < 0) V = 0;
    if (V > pos360) V = pos360;
    volEncoder.setCount(V);
    vol = V * maxVol / pos360;
    refreshVolume();
    printf("============> %d  %d\n", V, vol);
    volumeSavePending = true;
    volumeChangedAtMs = millis();
    toDisplay = 3;
  }
  //////////////////////////////////////////////////////////////////////
  // volume via remote keys
  //////////////////////////////////////////////////////////////////////
  if ((REMOTE_KEY == VOLP_rem) || (REMOTE_KEY == VOLM_rem))
  {
    if (muteON == true) {
      vol = Pvol;
      muteON = false;
    }
    if (REMOTE_KEY == VOLP_rem)vol++;
    if (REMOTE_KEY == VOLM_rem)vol--;
    REMOTE_KEY = 0;
    if (vol > maxVol) vol = maxVol;
    if (vol < 0) vol = 0;
    refreshVolume();
    volEncoder.setCount(vol * pos360 / maxVol);
    volumeSavePending = true;
    volumeChangedAtMs = millis();
    toDisplay = 3;
  }

  ///////////////////////////////////////////////////////////////////////
  // mute / unmute
  //////////////////////////////////////////////////////////////////////
  if ((muteB == true) && (muteON == false))
  {
    Pvol = vol;
    vol = 0;
    refreshVolume();
    muteB = false;
    muteON = true;
    toDisplay = 1;
  }
  if ((muteB == true) && (muteON == true))
  {
    vol = Pvol;
    refreshVolume();
    muteB = false;
    muteON = false;
    toDisplay = 2;
  }


  if (Bdonate == false)
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
        if (!saveStoredInt("/station", station)) {
          printf("Failed to persist station %d\n", station);
        }
        CLICK2B = false;
        if (muteON == true)
        {
          vol = Pvol;
          refreshVolume();
          muteB = false;
          muteON = false;
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

  /////////////////////////////////////////////////////////////////////////
  // Display refresh
  /////////////////////////////////////////////////////////////////////////
  if ((toDisplay != 0) && (Bdonate == false))
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
  if (volumeSavePending && (uint32_t)(millis() - volumeChangedAtMs) >= 750) {
    if (saveStoredInt("/volume", vol)) {
      volumeSavePending = false;
    } else {
      printf("Failed to persist volume %d; retrying\n", vol);
      volumeChangedAtMs = millis();
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
  if (strstr(info, "SampleRate=") != nullptr)
  {
    sscanf(info, "SampleRate=%d", &sampleRate);
    printf("Audio sample rate: %u Hz\n", sampleRate);
    connected = true;
  }
  if (strstr(info, "failed") || strstr(info, "Failed") || strstr(info, "ERROR") ||
      strstr(info, "error") || strstr(info, "disconnected") || strstr(info, "connection lost")) {
    connected = false;
    printf("Audio stream failure: %s (RSSI=%d dBm)\n", info, WiFi.RSSI());
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
      connected = false;
      break;
    case Audio::evt_name:
      audio_showstation(info);
      connected = true;
      break;
    case Audio::evt_icydescription:
      audio_showstreaminfo(info);
      connected = true;
      break;
    case Audio::evt_streamtitle:
      audio_showstreamtitle(info);
      connected = true;
      break;
    case Audio::evt_bitrate:
      audio_bitrate(info);
      connected = true;
      break;
    case Audio::evt_icyurl:
      audio_icyurl(info);
      break;
    case Audio::evt_lasthost:
      audio_lasthost(info);
      connected = true;
      break;
    default:
      break;
  }
}
