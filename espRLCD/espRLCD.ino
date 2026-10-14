#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "display_bsp.h"   // Waveshare driver (DisplayPort)
#include <Wire.h>
#include "esp_sleep.h"
#include "font.h"
#include "orbfont.h"
#include "secfont.h"
#include "PCF85063A-SOLDERED.h"
#include <WiFi.h>
#include "time.h"

const char* ssid       = "your_ssid"; // Wifi 4
const char* password   = "your_password";

const char* ntpServer  = "pool.ntp.org";
const long  gmtOffset_sec = -18000; // Offset in seconds (e.g., -5 hrs for EST = -5 * 3600)
const int   daylightOffset_sec = 0; // 3600 if Daylight Saving Time is active, else 0

const int BAT_ADC_PIN = 4; // GPIO4

static const uint8_t SHTC3_ADDR = 0x70;
PCF85063A rtc; 
static const int W = 400;
static const int H = 300;

DisplayPort RlcdPort(12, 11, 5, 40, 41, W, H);
// 1-bit "sprite/canvas"
GFXcanvas1 canvas(W, H);
float t, h;  // temperature and humidity

int sy=130;
int n=0;
 float v_bat=0;

  int minn=0;
  int hr=0;
  int sec=0;

uint8_t crc8(const uint8_t *data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

bool shtc3_cmd(uint16_t cmd) {
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

bool shtc3_read(float &tempC, float &rh) {
  // Wakeup
  if (!shtc3_cmd(0x3517)) return false;
  delay(1);

  // Measure (normal power, clock stretching disabled) – 0x7866 je česta komanda
  if (!shtc3_cmd(0x7866)) return false;
  delay(20);


  Wire.requestFrom((int)SHTC3_ADDR, 6);
  if (Wire.available() != 6) return false;

  uint8_t d[6];
  for (int i = 0; i < 6; i++) d[i] = Wire.read();

  if (crc8(&d[0], 2) != d[2]) return false;
  if (crc8(&d[3], 2) != d[5]) return false;

  uint16_t tRaw  = (uint16_t)d[0] << 8 | d[1];
  uint16_t rhRaw = (uint16_t)d[3] << 8 | d[4];


  tempC = -45.0f + 175.0f * (float)tRaw / 65535.0f;
  rh    = 100.0f * (float)rhRaw / 65535.0f;

  // Sleep
  shtc3_cmd(0xB098);
  return true;
}



// set canvas to display
void pushCanvasToRLCD(bool invert = false) {
  uint8_t *buf = canvas.getBuffer();        // 1bpp, packed
  const int bytesPerRow = (W + 7) / 8;      // za 400px = 50

  // Očisti RLCD buffer na bijelo (u tvom driveru postoji)
  RlcdPort.RLCD_ColorClear(ColorWhite);

  for (int y = 0; y < H; y++) {
    uint8_t *row = buf + y * bytesPerRow;

    for (int bx = 0; bx < bytesPerRow; bx++) {
      uint8_t v = row[bx];
      if (invert) v ^= 0xFF;

     
      int x0 = bx * 8;
      for (int bit = 0; bit < 8; bit++) {
        int x = x0 + bit;
        if (x >= W) break;

        bool on = (v & (0x80 >> bit)) != 0;
        if (on) {
          RlcdPort.RLCD_SetPixel((uint16_t)x, (uint16_t)y, ColorBlack);
        }
      }
    }
  }

  // Refresh 
  RlcdPort.RLCD_Display();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  analogReadResolution(12); // 0..4095
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);

  Wire.begin(13, 14);
  Wire.setTimeOut(50); // Set 50ms timeout so I2C reads never lock up the CPU
  RlcdPort.RLCD_Init();
 
  esp_sleep_enable_timer_wakeup(10ULL * 6000000ULL);


    rtc.begin();
    // setTime(hour, minute, sec);
    // rtc.setTime(18, 35, 00); // 24-hour mode, e.g., 11:53:00
    // setDate(weekday, day, month, yr);
   // rtc.setDate(1, 31, 3, 2025); // 0 for Sunday, e.g., Monday, 31.3.2025.
   setupTime();
    
}

void setupTime() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected! Syncing NTP time...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      // Sync internal system time to hardware PCF85063 RTC
      rtc.setTime(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      rtc.setDate(timeinfo.tm_wday, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      Serial.println("RTC updated from NTP successfully!");
    } else {
      Serial.println("Failed to obtain time from NTP.");
    }

    // Disconnect WiFi to save power
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  } else {
    Serial.println("\nWiFi connection failed. Using existing RTC time.");
  }
}

int batteryToSegments(float vbat) {
  if (vbat >= 4.0) return 5;
  if (vbat >= 3.90) return 4;
  if (vbat >= 3.80) return 3;
  if (vbat >= 3.65) return 2;
  if (vbat >= 3.50) return 1;
  return 0;
}

void draw()
{
  canvas.fillScreen(0);
  
  //frame
  canvas.fillRect(10, 10, 5, 280, 1);
  canvas.fillRect(385, 10, 5, 280, 1);
  canvas.fillRect(10, 10, 380, 5, 1);
  canvas.fillRect(10, 285, 380, 5, 1);

  //bat frame
  canvas.fillRect(308, 24, 60, 28, 1);
  canvas.fillRect(312, 28, 52, 20, 0);
  canvas.fillRect(368, 32, 5, 12, 1);

  //bet segments
  for(int i=0;i<batteryToSegments(v_bat);i++)
  canvas.fillRect(314+(i*10), 30, 7, 16, 1);


  //graph
  for(int j=0;j<60;j++){
    for(int i=0;i<10;i++)
    {
    if(j!=12 && j!=24 && j!=36 && j!=48)
    if(j<20)
    { 
    if(j<=sec)
    canvas.fillRect(20+(j*6),sy-(j*3)+(i*5),4,4,1);
    else
    canvas.drawRect(20+(j*6),sy-(j*3)+(i*5),4,4,1);
    }
    else
    {
      if(j<=sec)
      canvas.fillRect(20+(j*6),sy-(20*3)+(i*5),4,4,1);
      else
      canvas.drawRect(20+(j*6),sy-(20*3)+(i*5),4,4,1);
    }
    }}

 
  // line under graph 
    canvas.fillRect(90, 165, 290, 4, 1);

    canvas.fillRoundRect(162, 131, 70, 28, 6, 1);
    canvas.setTextColor(0);
    canvas.setFont(&Orbitron_Medium_22);
    canvas.setCursor(170, 153);
    canvas.print("TMP");

    canvas.setTextColor(1);
    canvas.setFont(&Orbitron_Medium_38);
    canvas.setCursor(240, 158);
    canvas.print(t);
 
  char timeStr[6];   // HH:MM\0
  sprintf(timeStr, "%02d:%02d", hr, minn);

  char secStr[4];   // HH:MM\0
  sprintf(secStr, "%02d",sec);

  canvas.setFont(&DSEG7_Classic_Bold_84);
  canvas.setCursor(20, 270);
  canvas.print(timeStr);

  canvas.setFont(&DSEG7_Classic_Bold_36);
  canvas.setCursor(320, 224);
  canvas.print(secStr);

    canvas.fillRoundRect(25, 25, 140, 30, 4, 1);
    canvas.setTextColor(0);
    canvas.setFont(&Orbitron_Medium_22);
    canvas.setCursor(35, 48);
    canvas.print("RLCD");

     canvas.fillRect(170, 25, 80, 3, 1);
     canvas.setTextColor(1);
    canvas.setFont(&Orbitron_Medium_19);
    canvas.setCursor(170, 48);
    canvas.print("ESP32 S3");

       canvas.setFont(&Orbitron_Medium_15);
    canvas.setCursor(25, 72);
    canvas.print("VOLOS");
    

  pushCanvasToRLCD(false);
}

void loop() {
  // Read time from hardware RTC
  minn = rtc.getMinute();
  hr   = rtc.getHour();
  sec  = rtc.getSecond();
 
  // Update battery and temperature sensors every 200 cycles (~10 seconds)
  if (n == 0) {
    int raw = analogRead(BAT_ADC_PIN);
    float v_adc = (raw / 4095.0f) * 3.3f;  
    v_bat = v_adc * 3.0f * 1.079f; 

    Serial.printf("raw=%d  Vadc=%.3f V  Vbat=%.3f V\n", raw, v_adc, v_bat);
    
    if (shtc3_read(t, h)) {
      Serial.print("T = "); Serial.print(t, 2); Serial.print(" C,  ");
      Serial.print("RH = "); Serial.print(h, 2); Serial.println(" %");
    } else {
      Serial.println("SHTC3 read failed");
    }
  }

  n++;
  if (n >= 200) n = 0;

  // Render to display
  draw();

  // Throttle loop speed to ~20 FPS (50ms delay) to prevent CPU starvation
  delay(50); 
}
