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

// #define setCompileTime 1
#define DRD_TIMEOUT 10
#define DRD_ADDRESS 0

// Instantiate objects
tmElements_t tm;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP); //Client uses default pool.ntp.org
U8G2_SH1106_128X64_NONAME_F_HW_I2C OLED_1(U8G2_R0, U8X8_PIN_NONE);
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

// Globals
unsigned long previousMillis = 0;
unsigned long previousNTPMillis = 0;
const long interval = 5 * 1000;
const long ntpInterval = 60 * 1000; // interval for NTP checks
const char *monthName[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
// Not needed because simply listing ip
// const char *ntpServerName = "10.10.10.102";

void drawOLED_1(void)
{
  char hourBuffer[10];
  // sprintf (hourBuffer, "Time: %02u:%02u:%02u\n", hour, min, sec); //original
    sprintf (hourBuffer, "%02u",tm.Hour);
  // itoa(tm.Hour), hourBuffer, 10);

  char minuteBuffer[10];
  sprintf (minuteBuffer, "%02u",tm.Minute);
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

  if (drd.detectDoubleReset())
  {
    Serial.println("Double Reset Detected");
    // ResetSettings to determine if it will work wihthout wifi
    wifiManager.resetSettings();
  }
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
  Serial.println("DS1307RTC Read Test");
  Serial.println("-------------------");

  Serial.println("Starting UDP");
  timeClient.begin();
  Serial.print("Status");
  Serial.println(WiFi.status());
}

void loop()
{

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
      RTC.set(timeClient.getEpochTime() - 4 * 3600);
    }
    else
    {
      Serial.println("No NTP - I'm not connected");
    }
  }
  if (currentMillis - previousMillis >= interval)
  {

    previousMillis = currentMillis;

    if (RTC.read(tm))
    {

      Serial.print("RTC Time = ");
      print2digits(tm.Hour);
      Serial.write(':');
      print2digits(tm.Minute);
      Serial.write(':');
      print2digits(tm.Second);
      // Serial.print(", Date (D/M/Y) = ");  //original
      Serial.print(", Date (Y_M_D) = ");
      Serial.print(tmYearToCalendar(tm.Year));
      Serial.write('_');
      Serial.print(tm.Month);
      Serial.write('_');
      Serial.print(tm.Day);
      Serial.println();
      drawOLED_1();
    }
    else
    {
      if (RTC.chipPresent())
      {
        Serial.println("The DS1307 is stopped.  Please run the SetTime");
        Serial.println("example to initialize the time and begin running.");
        Serial.println();
      }
      else
      {
        Serial.println("DS1307 read error!  Please check the circuitry.");
        Serial.println();
      }
      // delay(1000);
    }
  }
}