#include "Arduino.h"
#include <Audio.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>    //////////////////////////////////
#include <Wire.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include "Free_Fonts.h"
#include <ESP32Encoder.h>
#include <PNGdec.h>
#include <ImprovWiFiLibrary.h>
#include "esp_wifi.h"

PNG png;
#define MAX_IMAGE_WDITH 240 // Adjust for your images

#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

#define version "V0.4"

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
#define ENC_A1          3
#define ENC_B1          42
//Digital buttons
#define CLICK2      GPIO_NUM_35
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



xTaskHandle radioH, keybH, batteryH, jackH, remoteH, displayONOFFH, improvWiFiInitH;


Audio audio;
WiFiMulti wifiMulti;              //////////////////////////////////////
#define maxVol 31
#define pos360 31
int vol = maxVol;
int Pvol;
uint8_t mode;
uint8_t ssid[80];
uint8_t pwd[80];
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
  st += ES8388_Write_Reg(29, 0x00);

  // DAC power-up LOUT1/ROUT1 ET 2 enabled
  st += ES8388_Write_Reg(4, 0x39);

  // DAC R phase inversion
  st += ES8388_Write_Reg(28, 0x14);


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
i2s_pin_config_t pin_configR=
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


#define MAX_IMAGE_WDITH 240
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
    if(jackON == true)
    {
// LOUT2/ROUT2    
    audio.setVolume(maxVol);
    ES8388_Write_Reg(46, 0);    
    ES8388_Write_Reg(47, 0);
    ES8388_Write_Reg(48, volx+2);    
    ES8388_Write_Reg(49, volx+2);    
    }
    else
    {
// ROUT1/LOUT1   
    ES8388_Write_Reg(48, 0);    
    ES8388_Write_Reg(49, 0);
    if(volx > lowVol)
    {
      audio.setVolume(maxVol);
      ES8388_Write_Reg(46, volx+2);    
      ES8388_Write_Reg(47, volx+2);    
    }
    else
    {
      audio.setVolume(maxVol*volx/lowVol);
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

////////////////////////////////////////////////////////////////////////////////////////
// to change the volume
////////////////////////////////////////////////////////////////////////////////////////
void refreshVolume(void)
{
// audio.setVolume(vol, 0);  
  if(gpio_get_level(JACK_DETECT) == 1)
  {
    if(vol == 0) gpio_set_level(PA, 0);
    else gpio_set_level(PA, 1);
  }
  audio.setVolume(maxVol/2, 0);
  ES8388vol_Set(vol);
}

int16_t xpos = 0;
int16_t ypos = 0;
int toDisplay = 0;
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
  printf("%s\n",f);
//  printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());   
  if (rc == PNG_SUCCESS) {
    tft.startWrite();
//    printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());    
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
  if((nb > maxB) || (nb < 0))return -1;
  adcValue = analogRead(KEYs_ADC);  
  V = adcB[nb];
  if(abs(V - adcValue) < THRESHOLD ) return 0; else return 1; 
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


    if(REMOTE_KEY == MUTE_rem){muteB = true; REMOTE_KEY = 0;}
    if(REMOTE_KEY == OK_rem){CLICK2B = true; REMOTE_KEY = 0;}
 
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
  if(b[i - 2] == 0x0d) b[i - 2] = 0;    
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
  if(b[i - 2] == 0x0d) b[i - 2] = 0;      
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
    delay(1);
  }
}
///////////////////////////////////////////////////////////////////
// Task managing the audio jack
//////////////////////////////////////////////////////////////////
static void jack(void* data)
{ 
  while(true)
  {
    //printf("----------------- %d\n",gpio_get_level(JACK_DETECT));   

  if(gpio_get_level(JACK_DETECT) == 0) 
  {
   
    if(jackON == false)
    {
    printf("Jack ON\n");
    gpio_set_level(PA, 0);          // amp off   
    ES8388_Write_Reg(29, 0x00);     // stereo
    ES8388_Write_Reg(28, 0x04);     // no phase inversion + click free power up/down
    ES8388_Write_Reg(4, 0x0C);     // Rout2/Lout2     
    jackON = true;   
    refreshVolume();
    }
  }
  else
  {
    if(jackON == true)
    {
    printf("Jack OFF\n");
    gpio_set_level(PA, 1);          // amp on
    ES8388_Write_Reg(29, 0x20);     // mono (L+R)/2
    ES8388_Write_Reg(28, 0x14);     // Right DAC phase inversion + click free power up/down    
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
 
  while(true)
  {
    display = false;
    adcValue = analogRead(BAT_GAUGE_PIN)- zeroVal;   
 /*   
    printf("adcValue = %d\n", adcValue);
    tft.fillRect(140, 50, 30, 20, TFT_BLACK);
    tft.setCursor(140, 50);
    tft.println(adcValue);
 */   
    if((gpio_get_level(USB_DETECT) == 1) && (PadcValue != 1))
    {
      display = true;   
      PadcValue = 1;  
    }

    if(gpio_get_level(USB_DETECT) == 0)
    {
      if((PadcValue > (adcValue * 110 /100)) || (PadcValue < (adcValue * 90 / 100)))
      {
        display = true;
        PadcValue = adcValue;
      }
    }
    
  if(display == true)
 {     
    tft.setRotation(1); 
    tft.fillCircle(280, 35, 40, TFT_BLACK);
    tft.fillRect(260, 25, 32, 18,TFT_WHITE);
    tft.fillRect(292, 30, 4, 8,TFT_WHITE);
    if(gpio_get_level(USB_DETECT) == 0)
      if(adcValue > alertVal)
        tft.fillRect(263, 28, 26*adcValue/(nominalVal - zeroVal), 12, TFT_BLUE);
      else
        tft.fillRect(263, 28, 26*adcValue/(nominalVal - zeroVal), 12, TFT_RED);      
    else
    {
      tft.fillTriangle(264,34,280,34,280,40, TFT_GREY);
      tft.fillTriangle(272,34,272,28,288,34, TFT_GREY);
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
  while(true)
  {
    if (irrecv.decode(&results)) {
    if(results.decode_type == NEC)
    {
      uint32_t v = results.value;
      if(v != 0xffffffff)REMOTE_KEY = v & 0xFFFF;
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
  while(true)
  {
    displayT -= 100;
    if(displayT > 0) gpio_set_level(backLight, 1); else gpio_set_level(backLight, 0);
    delay(100); 
  }
}

///////////////////////////////////////////////////////////////////////
// settings init
///////////////////////////////////////////////////////////////////////
void settings(void)
{
  int j;
  char charSet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+=%*&-_(){}[]@,;:?./";
  gpio_set_level(backLight, 1);  
  printf("SETTINGS!!!\n");
  tft.setRotation(1);
  headerL("SETTINGS", "1- Select your internet access", TFT_NAVY);
  tft.setTextDatum(TC_DATUM); 
  tft.setTextColor(TFT_GREEN);   
  tft.drawString("4G", 150, 105, 4);
  tft.setTextColor(TFT_WHITE);  
  tft.drawString("WiFi", 150, 160, 4);
  delay(2000);
  mode = 0;
  staEncoder.setCount(0);
  while(gpio_get_level(CLICK2) == 1)
  {
    int V;
    V = staEncoder.getCount();
    if((V > 0) && (mode == 0))
    {
      mode = 1;
      tft.setTextColor(TFT_WHITE);   
      tft.drawString("4G", 150, 105, 4);
      tft.setTextColor(TFT_GREEN);  
      tft.drawString("WiFi", 150, 160, 4);    
      staEncoder.setCount(0);  
    }

    if((V < 0) && (mode == 1))
    {
      mode = 0;
      tft.setTextColor(TFT_GREEN);   
      tft.drawString("4G", 150, 105, 4);
      tft.setTextColor(TFT_WHITE);  
      tft.drawString("WiFi", 150, 160, 4);    
      staEncoder.setCount(0);  
    }  
    delay(100);  
  }
  uint8_t bm = mode + 0x30;
  File ln = LittleFS.open("/mode", FILE_WRITE);
  ln.write(&bm, 1);
  ln.close();  

  if(mode == 0)
  {
    tft.fillScreen(TFT_NAVY);
    tft.setTextDatum(TC_DATUM); 
    tft.setTextColor(TFT_GREEN);   
    tft.drawString("4G selected", 150, 105, 4);      
    delay(2000);
  }
  else
  {
  tft.fillScreen(TFT_NAVY);
  tft.setTextDatum(TC_DATUM); 
  tft.setTextColor(TFT_GREEN);   
  tft.drawString("WiFi selected", 150, 105, 4);   
  delay(1000);
  headerL("SETTINGS", "2- Select your Wifi credentials", TFT_NAVY);    
  tft.setTextColor(TFT_RED);
  tft.setTextDatum(TC_DATUM);
  tft.setFreeFont(FSB9);
  tft.drawString("First, select your SSID", 160, 60, GFXFF);
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
      j = staEncoder.getCount()/2;
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
  tft.drawString("Then, Enter your password", 160, 60, GFXFF);
  c[1] = 0;
  pwd[0] = 0;
  staEncoder.setCount(0);
  while (button_get_level(sw3) == 1)
  {
    tft.setTextColor(TFT_YELLOW);
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(FSB12);
    tft.drawString("ssid:", 20, 80, GFXFF);
    tft.setTextColor(TFT_GREEN);
    tft.drawString((char*)ssid, 80, 80, GFXFF);
    tft.setTextColor(TFT_YELLOW);
    tft.fillRect(20, 120, 300, 30, TFT_NAVY);
    tft.drawString("pwd:", 20, 120, GFXFF);
    tft.setTextColor(TFT_GREEN);
    tft.drawString((char*)pwd, 80, 120, GFXFF);


    tft.setTextColor(TFT_YELLOW);
    tft.setTextDatum(TL_DATUM);

    while ((gpio_get_level(CLICK2) == 1) && (button_get_level(sw1) == 1) && (button_get_level(sw3) == 1))
    {
      PL = staEncoder.getCount()/2;
      if (PL < 0) {
        PL = 0;
        staEncoder.setCount(PL);
      }
      if (PL > (strlen(charSet) - 1)) {
        PL = strlen(charSet) - 1;
        staEncoder.setCount(PL);
      }
      if (PL != PPL)
      {
        PPL = PL;
        c[0] = charSet[PL];
        tft.fillRect(150, 170, 40, 40, TFT_BLACK);
        tft.drawString(c, 160, 180, 4);
      }
      delay(100);
    }
    if (gpio_get_level(CLICK2) == 0)
    {
      strcat((char*)pwd, c);
      printf("pwd = %s\n", pwd);
      while(gpio_get_level(CLICK2) == 0) delay(10);
    }
    if (button_get_level(sw1) == 0)
    {
      if (strlen((char*)pwd) > 0) pwd[strlen((char*)pwd) - 1] = 0;
      while(button_get_level(sw1) == 0) delay(10);
    }
  }
  printf("ssid: %s   pwd: %s\n", ssid, pwd);

  ln = LittleFS.open("/pwd", "w");
  ln.write(pwd, strlen((char*)pwd) + 1);
  ln.close();           
  }
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
  improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32_S3, "MuseRadio", "1.0", "Raspiaudio Radio");  
  improvSerial.onImprovConnected(WiFiConnected);
  delay(500);

  while(true)
  {
    improvSerial.handleSerial();   
    delay(10);
  }
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup() {

  uint8_t res;
  USBSerial.begin(115200);

/////////////////////////////////////////////////////  
// Little FS init
/////////////////////////////////////////////////////
  if (!LittleFS.begin()) {
    Serial.println("LittleFS initialisation failed!");
    while (1) for (;;);
  }

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
  ESP32Encoder::useInternalWeakPullResistors = UP;
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

///////////////////////////////////////////////////////
//enable remote
///////////////////////////////////////////////////////
 irrecv.enableIRIn(); 
  tft.init();

// Task to initialize WiFi credentials (Improv Serial)  
  xTaskCreatePinnedToCore(improvWiFiInit, "improvWiFiInit", 5000, NULL, 5, &improvWiFiInitH, 1); 

  File ln = LittleFS.open("/mode", FILE_READ);
// if mode not defined ==> settings
  if(!ln) settings();
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
  drawImage("/raspiaudio.png", 0, 0);
  tft.setTextColor(TFT_BLUE, TFT_WHITE);
  tft.drawString(version, 280, 220, 2);
  delay(1000);

if (mode == '0') {
  // 4G activation  
  printf("4G\n");
  gpio_set_level(EN_4G, 1);
  strcpy((char*)ssid, "xhkap");
  strcpy((char*)pwd, "12345678");

  // Attendre que le SSID soit disponible ou que le délai d'attente soit dépassé
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < MAX_WAIT_TIME) {
    delay(1000);
    printf("Waiting for SSID...\n");
  }

  if (WiFi.status() != WL_CONNECTED) {
    printf("SSID not found, resetting WiFi...\n");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    delay(1000);
    WiFi.mode(WIFI_STA);
    WiFi.begin((char*)ssid, (char*)pwd);

    // Attendre à nouveau que le SSID soit disponible ou que le délai d'attente soit dépassé
    startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < MAX_WAIT_TIME) {
      delay(1000);
      printf("Waiting for SSID after reset...\n");
    }

    if (WiFi.status() != WL_CONNECTED) {
      printf("SSID not found after reset, going to settings\n");
      settings();
    } else {
      printf("Connected to %s after reset\n", ssid);
      started = true;
    }
  } else {
    printf("Connected to %s\n", ssid);
    started = true;
  }
}
else
{
  printf("WiFi\n");
  gpio_set_level(EN_4G, 0);   
  ln = LittleFS.open("/ssid", FILE_READ);
  if(!ln) settings();
  ln.read(ssid, 80);
  ssid[ln.size() - 1] = 0;
  ln.close();
  ln = LittleFS.open("/pwd", FILE_READ);
  if(!ln) settings();
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
    tft.drawString("Connection failed...", 180, 105, 4);     
    settings();                                           //////////////////////////////
    delay(2000);   
  }   
 /*   
#define TO 40      
    ////////////////////////////////////////////
    int n = 0;
    while ((WiFi.status() != WL_CONNECTED) && (n < TO))
    {
      n++;
      delay(1000);
    }
    printf("%d    %d\n", WiFi.isConnected(), n);
    if (WiFi.status() == WL_CONNECTED) 
    {
    started = true;
    printf("Connected to %s\n", ssid);
    }

    else
    {
      tft.fillRect(0, 0, 320, 240, TFT_BLACK); 
      tft.setTextColor(TFT_RED);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("Connection failed...", 180, 105, 4);     
      settings();                                           //////////////////////////////
      delay(2000);
    }
*/
  
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
  audio.setVolumeSteps(maxVol+1);
  audio.setVolume(maxVol);

// connections init
if(gpio_get_level(JACK_DETECT) == 0) 
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
  V = vol * pos360 /maxVol;
  volEncoder.setCount(V);
  PV = V;
  refreshVolume();
// station encoder init
  staEncoder.setCount(station*2);
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
  if(mode == '0')  tft.drawString("4G", 50, 27, 2);   
  else tft.drawString("WiFi", 50, 27, 2);   
  toDisplay = 0;
  displayON();
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

 //   printf("RSSI = %d dB\n", WiFi.RSSI());   
  
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
  if((REMOTE_KEY == VOLP_rem) || (REMOTE_KEY == VOLM_rem))
  {
    if(muteON == true) {vol = Pvol; muteON = false;}
    if(REMOTE_KEY == VOLP_rem)vol++;
    if(REMOTE_KEY == VOLM_rem)vol--; 
    REMOTE_KEY = 0;   
    if(vol > maxVol) vol = maxVol;
    if(vol < 0) vol = 0;
    refreshVolume();
    volEncoder.setCount(vol*pos360/maxVol);
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
////////////////////////////////////////////////////////////////////////
//  station search via encoder
////////////////////////////////////////////////////////////////////////
  S = staEncoder.getCount();
  delay(50);
  if((S != VS) && (S == staEncoder.getCount()))
  {
    printf(">>>> %d\n", S);
    displayON();
    modSta = true;
    CLICK2B = false;
    lastModTime = millis();
    if(S > MS*2) S = 0;
    if(S < 0) S = MS*2;
    VS = S;
    staEncoder.setCount(S);
    tft.setRotation(1);  
    tft.fillRect(80, 90, 240, 60, TFT_BLACK); 
    tft.setTextColor(TFT_SILVER);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(Rname(S/2), 180, 105, 4); 
  }

  
////////////////////////////////////////////////////////////////////////////
// station search via remote keys
////////////////////////////////////////////////////////////////////////////
  if((REMOTE_KEY == LEFT_rem) || (REMOTE_KEY == RIGHT_rem))
  {
    displayON();
    if(modSta == false) S = station*2;
    if(REMOTE_KEY == LEFT_rem) S -= 2;
    if(REMOTE_KEY == RIGHT_rem) S += 2;
    REMOTE_KEY = 0;
    modSta = true;
    CLICK2B = false;
    lastModTime = millis();
    if(S > MS*2) S = 0;
    if(S < 0) S = MS*2;
    staEncoder.setCount(S);
    VS = S;
    tft.setRotation(1);  
    tft.fillRect(80, 90, 240, 60, TFT_BLACK); 
    tft.setTextColor(TFT_SILVER);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(Rname(S/2), 180, 105, 4); 
  }
  
  
/////////////////////////////////////////////////////////////////////////
// new station validation
/////////////////////////////////////////////////////////////////////////    
  if(CLICK2B == true)
  {
    displayON();
    if(modSta == true)
    {
    modSta = false;
    station = S/2;
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
  if(modSta == true)
  {
    if(millis() > (lastModTime + 4000))
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
  
   if (button_get_level(sw0) == 0)
    {
      while(button_get_level(sw0) == 0) delay(10);
      vTaskDelete(radioH);
      vTaskDelete(keybH);
      vTaskDelete(batteryH);
      vTaskDelete(jackH);
      vTaskDelete(remoteH);
      vTaskDelete(displayONOFFH);
      vTaskDelete(improvWiFiInitH);
      
      settings();
    }

/////////////////////////////////////////////////////////////////////////
// Display refresh
/////////////////////////////////////////////////////////////////////////
  if(toDisplay != 0)
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
        if(jaugeB == false)
        {
          tft.setRotation(1);
          tft.fillCircle(160, 190, 25, TFT_BLACK);  
          tft.fillRoundRect(80, 180, 160, 20, 10, TFT_WHITE);         
          jaugeB = true;
        }   
        int V2;
 //       V2 = V + V;
        V2 = V;
        if(V2 > pos360) V2 = pos360;
           
        tft.fillRect(90+140*V2/pos360, 182, 140-140*V2/pos360, 16, TFT_WHITE);

        tft.fillRect(90, 182, 140*V2/pos360, 16, TFT_GREY);
        N = 0;
        toDisplay = 4;
        break;   
      case 4 :
//      
        N++;
        if(N > 20)toDisplay = 2;
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
  

    if(WiFi.status() != WL_CONNECTED )
    {
    WiFi.disconnect(true);
    wifiMulti.run();
     delay(1500);  
    }   
 
  }
}
void audio_id3data(const char *info) { //id3 metadata
  //Serial.print("id3data     ");Serial.println(info);
}
void audio_eof_mp3(const char *info) { //end of file
  //Serial.print("eof_mp3     ");Serial.println(info);
}
void audio_showstation(const char *info) {
  //Serial.print("station     ");Serial.println(info);
}
void audio_showstreaminfo(const char *info) {
  //  Serial.print("streaminfo  ");Serial.println(info);
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
  // Serial.print("bitrate     ");Serial.println(info);
}
void audio_commercial(const char *info) { //duration in sec
  // Serial.print("commercial  ");Serial.println(info);
}
void audio_icyurl(const char *info) { //homepage
  // Serial.print("icyurl      ");Serial.println(info);
}
void audio_lasthost(const char *info) { //stream URL played
  //Serial.print("lasthost    ");Serial.println(info);
}
void audio_eof_speech(const char *info) {
  //Serial.print("eof_speech  ");Serial.println(info);
}
