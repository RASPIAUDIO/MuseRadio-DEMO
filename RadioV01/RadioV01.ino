#include "Arduino.h"
#include <PubSubClient.h>
#include <Audio.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>
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
#include <driver/i2s.h>
#include "lwip/apps/sntp.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

PNG png;
#define MAX_IMAGE_WDITH 320 // Adjust for your images




#define version "V1.1b"

#define I2S_DOUT        17
#define I2S_BCLK        5
#define I2S_LRC         16
#define I2S_DIN         4
#define I2SN (i2s_port_t)0
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

static char const OTA_FILE_LOCATION[] = "https://raw.githubusercontent.com/RASPIAUDIO/ota/main/RadioLast.ota";

xTaskHandle radioH, keybH, batteryH, jackH, remoteH, displayONOFFH, improvWiFiInitH;


Arduino_ESP32_OTA ota;
Arduino_ESP32_OTA::Error ota_err = Arduino_ESP32_OTA::Error::None;




WiFiClient espClient;
PubSubClient client(espClient);

time_t now;
struct tm timeinfo;
char timeStr[60];
Audio audio;
WiFiMulti wifiMulti;              //////////////////////////////////////
#define maxVol 31
#define pos360 31
int vol = maxVol;
int Pvol;
uint8_t mode;
uint8_t ssid[80];
uint8_t pwd[80];
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
  return st;

}


int delay1 = 10;
int delay2 = 500;
i2s_pin_config_t pin_configR =
{
  .bck_io_num   =   I2S_BCLK,
  .ws_io_num    =   I2S_LRC ,
  .data_out_num =   I2S_DOUT,
  .data_in_num  =   I2S_DIN
};

int station = 0;
int previousStation;
int MS;
bool connected = true;
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

ESP32Encoder volEncoder;
ESP32Encoder staEncoder;




////////////////////////////////////////////////////////////////////////
//
// manages volume (via vol xOUT1, vol DAC, and vol xIN2)
//
////////////////////////////////////////////////////////////////////////

void ES8388vol_Set(uint8_t volx)
{

  // mute
  ES8388_Write_Reg(25, 0x04);
#define lowVol   16
  if (volx > maxVol) volx = maxVol;
  if (volx == 0)ES8388_Write_Reg(25, 0x04); else ES8388_Write_Reg(25, 0x00);

  //    if (volx > lowVol)audio.setVolume(volx); else audio.setVolume(lowVol);
  //    printf("VOLX = %d  %d\n",volx, jackON);
  if (jackON == true)
  {
    // LOUT2/ROUT2
    audio.setVolume(maxVol);
    ES8388_Write_Reg(46, 0);
    ES8388_Write_Reg(47, 0);
    ES8388_Write_Reg(48, volx + 2);
    ES8388_Write_Reg(49, volx + 2);
  }
  else
  {
    // ROUT1/LOUT1
    ES8388_Write_Reg(48, 0);
    ES8388_Write_Reg(49, 0);
    if (volx > lowVol)
    {
      audio.setVolume(maxVol);
      ES8388_Write_Reg(46, volx + 2);
      ES8388_Write_Reg(47, volx + 2);
    }
    else
    {
      audio.setVolume(maxVol * volx / lowVol);
      ES8388_Write_Reg(46, lowVol);
      ES8388_Write_Reg(47, lowVol);
    }
  }
  // RDAC/LDAC (digital)
  ES8388_Write_Reg(26, 0x00);
  ES8388_Write_Reg(27, 0x00);
  // unmute
  ES8388_Write_Reg(25, 0x00);

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
  audio.setVolume(maxVol / 2, 0);
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
#define THRESHOLD 200
#define maxB 3
  static int adcB[] = {1850, 2350, 450, 930};
  int adcValue, V;
  if ((nb > maxB) || (nb < 0))return -1;
  adcValue = analogRead(KEYs_ADC);
  V = adcB[nb];
  if (abs(V - adcValue) < THRESHOLD ) return 0; else return 1;
}

///////////////////////////////////////////////////////////////////////
// task managing buttons
//
/////////://///////////////////////////////////////////////////////////
static void keyb(void* pdata)
{

  while (1)
  {

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
    delay(100);
  }
}


/////////////////////////////////////////////////////////////////////////////
// gets station link from LittleFS file "/linkS"
//
/////////////////////////////////////////////////////////////////////////////
char* Rlink(int st)
{
  int i;
  static char b[80];
  File ln = LittleFS.open("/linkS", FILE_READ);
  i = 0;
  uint8_t c;
  while (i != st)
  {
    while (c != 0x0a)ln.read(&c, 1);
    c = 0;
    i++;
  }
  i = 0;
  do
  {
    ln.read((uint8_t*)&b[i], 1);
    i++;
  } while (b[i - 1] != 0x0a);
  b[i - 1] = 0;
  //to suppress extra char 0x0d (rc)  (for Windows users)
  if (b[i - 2] == 0x0d) b[i - 2] = 0;
  ln.close();
  return b;
}
/////////////////////////////////////////////////////////////////////////////////
//  gets station name from LittleFS file "/namS"
//
/////////////////////////////////////////////////////////////////////////////////
char* Rname(int st)
{
  int i;
  static char b[20];
  File ln = LittleFS.open("/nameS", FILE_READ);
  i = 0;
  uint8_t c;
  while (i != st)
  {
    while (c != 0x0a)ln.read(&c, 1);
    c = 0;
    i++;
  }
  i = 0;
  do
  {
    ln.read((uint8_t*)&b[i], 1);
    i++;
  } while (b[i - 1] != 0x0a);
  b[i - 1] = 0;
  //to suppress extra char 0x0d (rc)  (for Windows users)
  if (b[i - 2] == 0x0d) b[i - 2] = 0;
  ln.close();
  return b;
}
/////////////////////////////////////////////////////////////////////////
//  defines how many stations in LittleFS file "/linkS"
//
////////////////////////////////////////////////////////////////////////
int maxStation(void)
{
  File ln = LittleFS.open("/linkS", FILE_READ);
  uint8_t c;
  int m = 0;
  int t;
  t = ln.size();
  int i = 0;
  do
  {
    while (c != 0x0a) {
      ln.read(&c, 1);
      i++;
    }
    c = 0;
    m++;
  } while (i < t);
  ln.close();
  printf("=========> %d \n", m);
  return m;
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
    // printf("st %d prev %d\n",station,previousStation);
    if ((station != previousStation) || (connected == false))
    {
      printf("station no %d %s\n", station, Rname(station));
      i2s_stop(I2SN);
      i2s_zero_dma_buffer(I2SN);
      delay(500);
      i2s_start(I2SN);
      audio.stopSong();
      connected = false;
      //delay(100);
      linkS = Rlink(station);
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
  if (mode == '0')  tft.drawString("4G", 50, 27, 2);
  else tft.drawString("WiFi", 50, 27, 2);


  tft.setTextColor(TFT_GREY, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);  // Set text datum to top-left
  tft.drawString("1-Settings  2-Radio select", 20, 220, 2);
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

    ln = LittleFS.open("/pwd", "w");
    ln.write(pwd, strlen((char*)pwd) + 1);
    ln.close();
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
  uint8_t bm = '1';
  File ln = LittleFS.open("/mode", FILE_WRITE);
  ln.write(&bm, 1);
  ln.close();
  ln = LittleFS.open("/pwd", "w");
  ln.write((uint8_t*)password, strlen(password) + 1);
  ln.close();
  ln = LittleFS.open("/ssid", FILE_WRITE);
  ln.write((uint8_t*)ssid, strlen(ssid) + 1);
  ln.close();
  vTaskDelete(radioH);
  vTaskDelete(keybH);
  vTaskDelete(batteryH);
  vTaskDelete(jackH);
  vTaskDelete(remoteH);
  vTaskDelete(displayONOFFH);


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
  improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32_S3, "Radio", "1.0", "Raspiaudio Radio");
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

  uint8_t res;
  USBSerial.begin(115200);

  // Start the Improv Wi-Fi over Serial task immediately
  xTaskCreatePinnedToCore(improvWiFiInit, "improvWiFiInit", 5000, NULL, 5, &improvWiFiInitH, 1);

  // Small delay to ensure Improv Serial is ready
  vTaskDelay(100 / portTICK_PERIOD_MS);

  /////////////////////////////////////////////////////
  // Little FS init
  /////////////////////////////////////////////////////
  if (!LittleFS.begin()) {
    Serial.println("LittleFS initialisation failed!");
    while (1) for (;;);
  }
  //    LittleFS.format();
  File root = LittleFS.open("/", "r");
  File file = root.openNextFile();
  while (file) {
    printf("FILE: /%s\n", file.name());
    file = root.openNextFile();
    delay(100);
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

  //gpio_set_pull_mode
  gpio_set_pull_mode(CLICK1, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(CLICK2, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(JACK_DETECT, GPIO_PULLUP_ONLY);

  gpio_set_pull_mode(EN_4G, GPIO_PULLUP_ONLY);

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
  tft.init();



  File ln = LittleFS.open("/mode", FILE_READ);
  // if mode not defined ==> settings
  if (!ln) settings();
  ln.read(&mode, 1);
  ln.close();
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

  {
    printf("WiFi\n");
    gpio_set_level(EN_4G, 0);
    ln = LittleFS.open("/ssid", FILE_READ);
    if (!ln) settings();
    ln.read(ssid, 80);
    ssid[ln.size() - 1] = 0;
    ln.close();
    ln = LittleFS.open("/pwd", FILE_READ);
    if (!ln) settings();
    ln.read(pwd, 80);
    pwd[ln.size() - 1] = 0;
    ln.close();
  }
  ////////////////////////////////////////////////
  // WiFi init
  ////////////////////////////////////////////////
  started = false;
  printf("%s    %s\n", ssid, pwd);

  WiFi.useStaticBuffers(true);
  WiFi.mode(WIFI_STA);
  //   WiFi.begin((char*)ssid, (char*)pwd);
  wifiMulti.addAP((char*)ssid, (char*)pwd);    /////////////////////////////////////:
  //   wifiMulti.run();
  const uint32_t connectTimeoutMs = 20000;
  if (wifiMulti.run(connectTimeoutMs) == WL_CONNECTED) {
    USBSerial.print("WiFi connected: ");
    USBSerial.print(WiFi.SSID());
    USBSerial.print(" ");
    USBSerial.println(WiFi.RSSI());
    started = true;

    ////////////////////////////////////////////////////////////////
    //last version .ota loading
    ////////////////////////////////////////////////////////////////
    if (BOTA == true)loadLastOTA();

    fetchStationList();


  }
  else {
    Serial.println("WiFi not connected!");
    tft.fillRect(0, 0, 320, 240, TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Connection failed...", 180, 105, 4);
    settings();                                           //////////////////////////////
    delay(2000);
  }


  //  delay(2000);
  ///////////////////////////////////////////////////////////////
  // Audio init
  //////////////////////////////////////////////////////////////
  //ES8388 codec init
  Wire.setPins(SDA, SCL);
  Wire.begin();
  res = ES8388_Init();
  if (res == 0)printf("Codec init OK\n"); else printf("Codec init failed\n");

  // audio lib init
  i2s_set_pin((i2s_port_t)0, &pin_configR);
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolumeSteps(maxVol + 1);
  audio.setVolume(maxVol);

  // connections init
  if (gpio_get_level(JACK_DETECT) == 0)
  {
    printf("Jack ON\n");
    gpio_set_level(PA, 0);          // amp off
    ES8388_Write_Reg(29, 0x00);     // stereo
    ES8388_Write_Reg(28, 0x04);     // no phase inversion + click free power up/down
    ES8388_Write_Reg(4, 0x0C);     // Rout2/Lout2
    jackON = true;
    refreshVolume();
  }
  else
  {
    printf("Jack OFF\n");
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

  // previous station
  ln = LittleFS.open("/station", "r");
  ln.read((uint8_t*)b, 2);
  b[2] = 0;
  station = atoi(b);
  ln.close();
  // previous volume
  ln = LittleFS.open("/volume", "r");
  ln.read((uint8_t*)b, 2);
  b[2] = 0;
  vol = atoi(b);

  ln.close();
  MS = maxStation() - 1;
  previousStation = -1;
  printf("station = %d    vol = %d\n", station, vol);
  // volume encoder init
  V = vol * pos360 / maxVol;
  volEncoder.setCount(V);
  PV = V;
  refreshVolume();
  // station encoder init
  staEncoder.setCount(station * 2);
  S = VS = 0;

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
  tft.drawString("1-Settings  2-Radio select", 20, 220, 2);
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
    sprintf(b, "%02d", vol);
    File ln = LittleFS.open("/volume", "w");
    ln.write((uint8_t*)b, 2);
    ln.close();
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
    sprintf(b, "%02d", vol);
    File ln = LittleFS.open("/volume", "w");
    ln.write((uint8_t*)b, 2);
    ln.close();
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
      if (S > MS * 2) S = 0;
      if (S < 0) S = MS * 2;
      VS = S;
      staEncoder.setCount(S);
      tft.setRotation(1);
      tft.fillRect(80, 90, 240, 60, TFT_BLACK);
      tft.setTextColor(TFT_SILVER);
      tft.setTextDatum(TC_DATUM);
      tft.drawString(Rname(S / 2), 180, 105, 4);
    }


    ////////////////////////////////////////////////////////////////////////////
    // station search via remote keys
    ////////////////////////////////////////////////////////////////////////////
    if ((REMOTE_KEY == LEFT_rem) || (REMOTE_KEY == RIGHT_rem))
    {
      displayON();
      if (modSta == false) S = station * 2;
      if (REMOTE_KEY == LEFT_rem) S -= 2;
      if (REMOTE_KEY == RIGHT_rem) S += 2;
      REMOTE_KEY = 0;
      modSta = true;
      CLICK2B = false;
      lastModTime = millis();
      if (S > MS * 2) S = 0;
      if (S < 0) S = MS * 2;
      staEncoder.setCount(S);
      VS = S;
      tft.setRotation(1);
      tft.fillRect(80, 90, 240, 60, TFT_BLACK);
      tft.setTextColor(TFT_SILVER);
      tft.setTextDatum(TC_DATUM);
      tft.drawString(Rname(S / 2), 180, 105, 4);
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
        station = S / 2;
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

    ////////////////////////////////////////////////////////////////////////
    // explicit call of Settings (sw0)
    ////////////////////////////////////////////////////////////////////////
    if ((button_get_level(sw0) == 0) && (BSettings == false))
    {
      while (button_get_level(sw0) == 0) delay(50);
      BSettings = true;
      displayON();
      vTaskDelete(radioH);
      vTaskDelete(keybH);
      vTaskDelete(batteryH);
      vTaskDelete(jackH);
      vTaskDelete(remoteH);
      vTaskDelete(displayONOFFH);
      settings();
    }
  }

  if (button_get_level(sw1) == 0)
  {
    while (button_get_level(sw1) == 0) delay(10);
    displayON();
    Bdonate = !Bdonate;
    delay(100);
    if (Bdonate == true)
    {

      // stop battery display and display ON/OFF
      vTaskDelete(batteryH);
      vTaskDelete(displayONOFFH);
      QRcreate();
    }
    else
    {
      retrieveDisplay();
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
  if (strstr(info, "SampleRate=") > 0)
  {
    sscanf(info, "SampleRate=%d", &sampleRate);
    printf("==================>>>>>>>>>>%d\n", sampleRate);
  }
  connected = true;
  if (strstr(info, "failed") > 0) {
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

void fetchStationList() {
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
#define I2SW (i2s_port_t)0
#define I2SR (i2s_port_t)1

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
