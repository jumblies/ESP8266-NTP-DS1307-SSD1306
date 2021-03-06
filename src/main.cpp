/*TODO

Add timezone 
Add resetSettings jumper code
Add display code


Pins HW I2C
D1 SCL
D2 SDA
*/

// Libraries
#include "Arduino.h"
#include <Wire.h>
#include <TimeLib.h>
#include <DS1307RTC.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <NTPClient.h>
//local helper functions
#include "timeDisplayHelpers.h"
#include <U8g2lib.h>
#include <DoubleResetDetector.h>
#include <Timezone.h>

// #define setCompileTime 1
#define DRD_TIMEOUT 5
#define DRD_ADDRESS 0

// Instantiate objects
tmElements_t tm;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP); //Client uses default pool.ntp.org
U8G2_SH1106_128X64_NONAME_F_HW_I2C OLED_1(U8G2_R0, U8X8_PIN_NONE);
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

// Timezone Objects

TimeChangeRule myDST = {"EDT", Second, Sun, Mar, 2, -240}; //UTC - 4 hours
TimeChangeRule mySTD = {"EST", First, Sun, Nov, 2, -300};  //UTC - 5 hours
Timezone myTZ(myDST, mySTD);
TimeChangeRule *tcr;

// Globals
unsigned long previousMillis = 0;
unsigned long previousNTPMillis = 0;
const long interval = 1 * 1000;
const long ntpInterval = 5 * 1000; // interval for NTP checks
const char *monthName[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
// Not needed because simply listing ip
// const char *ntpServerName = "10.10.10.102";

void drawOLED_1(time_t locoMoco)
{
  char hourBuffer[10];
  sprintf(hourBuffer, "%02u", hour(locoMoco));
  // itoa(tm.Hour), hourBuffer, 10);

  char minuteBuffer[10];
  sprintf(minuteBuffer, "%02u", minute(locoMoco));
  // itoa(tm.Minute, minuteBuffer, 10);

  OLED_1.clearBuffer(); // clear the internal memory
  OLED_1.setFont(u8g2_font_logisoso42_tn);
  // char buffer[7];
  OLED_1.drawStr(0, 42, hourBuffer);

  OLED_1.drawStr(60, 62, minuteBuffer);
  OLED_1.sendBuffer(); // transfer internal memory to the display
}

bool getTime(const char *str)
{
  int Hour, Min, Sec;

  if (sscanf(str, "%d:%d:%d", &Hour, &Min, &Sec) != 3)
    return false;
  tm.Hour = Hour;
  tm.Minute = Min;
  tm.Second = Sec;
  return true;
}

bool getDate(const char *str)
{
  char Month[12];
  int Day, Year;
  uint8_t monthIndex;

  if (sscanf(str, "%s %d %d", Month, &Day, &Year) != 3)
    return false;
  for (monthIndex = 0; monthIndex < 12; monthIndex++)
  {
    if (strcmp(Month, monthName[monthIndex]) == 0)
      break;
  }
  if (monthIndex >= 12)
    return false;
  tm.Day = Day;
  tm.Month = monthIndex + 1;
  tm.Year = CalendarYrToTm(Year);
  return true;
}

void setup()
{
  OLED_1.begin();
  OLED_1.setI2CAddress(0x3C * 2);
  OLED_1.clearBuffer();
  OLED_1.setFont(u8g2_font_ncenB14_tr);
  OLED_1.drawStr(0, 20, "Booting!");
  OLED_1.sendBuffer();

  Serial.begin(115200);
  while (!Serial)
  {
    delay(200);
  }
  WiFiManager wifiManager;

  // if (drd.detectDoubleReset())
  // {
  //   Serial.println("Double Reset Detected");
  //   // ResetSettings to determine if it will work wihthout wifi
  //   wifiManager.resetSettings();
  // }
  wifiManager.setTimeout(15);
  // wifiManager.setConfigPortalBlocking(false);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("NTP_RTC"))
  {
    Serial.println("failed to connect and hit timeout");
  }

#ifdef setCompileTime
  bool parse = false;
  bool config = false;

  // get the date and time the compiler was run
  if (getDate(__DATE__) && getTime(__TIME__))
  {
    parse = true;
    // and configure the RTC with this info
    if (RTC.write(tm))
    {
      Serial.println("RTC Configured from compile time");
      config = true;
    }
  }
#endif

  // wifiManager.autoConnect("AutoConnectAP");
  Serial.println(WiFi.localIP());

  setSyncProvider(RTC.get); // the function to get the time from the RTC
  if (timeStatus() != timeSet)
    Serial.println("Unable to sync with the RTC");
  else
    Serial.println("RTC has set the system time");

  Serial.println("Starting UDP");
  timeClient.begin();

  // Serial.println(WiFi.status());
  // if (WiFi.status() == 3)
  // {
  //   Serial.printf("Wifi Connected; IP = %s", WiFi.localIP().toString().c_str());
  // }
  // else
  // {
  //   WiFi.mode(WIFI_OFF);
  //   Serial.println("Wifi Off");
  // }
}

void loop()
{
  /*Get 3 time objects: MCU time, Local time(mcu time converted to local), and RTC which should match MCU time unless drifted) */
  // MCU time in UTC
  time_t utc = now();
  // Conversion of MCU time to localtime via timezone
  time_t local = myTZ.toLocal(utc, &tcr);
  // Get the RTC time as an object
  time_t rtcTime = RTC.get();

  unsigned long currentMillis = millis();
  unsigned long currentNTPMillis = millis();

  // This compartmentalizes NTP and RTC update
  if (currentNTPMillis - previousNTPMillis >= ntpInterval)
  {
    previousNTPMillis = currentNTPMillis;
    if (WiFi.status() == 3)
    // Debug wifi not connected
    {
      timeClient.update();

      Serial.print("NTP Time (GMT)= ");
      Serial.println(timeClient.getFormattedTime());
      // unsigned long epoch = timeClient.getEpochTime() ; //-4 h for EST
      // RTC can be set in one fell swoop
      //  offset of hours done manually
      // RTC.set(timeClient.getEpochTime());

      // This is pretty confusing 
      // RTC.set(timeClient.getEpochTime() - 2208988800UL);  // this sets it to an invalid time
      RTC.set(timeClient.getEpochTime());
    }
    else
    {
      // WiFi.mode(WIFI_OFF);
      Serial.println("Wifi Off - No NTP");
    }
  }
  if (currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;
    {
      drawOLED_1(local);
      Serial.printf("UTC_MCU_Time = %.2d:%.2d:%.2d \t", hour(utc), minute(utc), second(utc));
      Serial.printf("UTC_RTC_time= %.2d:%.2d:%.2d\t", hour(rtcTime), minute(rtcTime), second(rtcTime));
      Serial.printf("Local_MCU_Time = %.2d:%.2d:%.2d \n", hour(local), minute(local), second(local));
      // The RTC is always in UTC time and local (above) is after TZ conversion.
    }
  }
}