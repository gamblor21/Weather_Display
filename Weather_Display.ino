/******
The MIT License (MIT)

Copyright (c) 2020 Mark Komus

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
******/

#include <RTCZero.h>
#include <SPI.h>
#include <WiFi101.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include "Adafruit_EPD.h"

#include <ArduinoJson.h>
#include "arduino_secrets.h" 

#define EPD_CS     13
#define EPD_DC      12
#define SRAM_CS     11
#define EPD_RESET   10 // can set to -1 and share with microcontroller Reset!
#define EPD_BUSY    -1 // can set to -1 to not use a pin (will wait a fixed delay)
#define DONE_PIN  5
#define VBAT_PIN A7

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;            // your network key Index number (needed only for WEP)

int status = WL_IDLE_STATUS;

char server[] = "io.adafruit.com";    // name address for Google (using DNS)

// Initialize the Ethernet client library
// with the IP address and port of the server
// that you want to connect to (port 80 is default for HTTP):
WiFiClient client;

Adafruit_IL0373 display(212, 104, EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY);

/* Create an rtc object */
RTCZero rtc;

int nextAlarmMinute = 0;
volatile bool alarmWent = false;

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(115200);
  //while (!Serial) { ; }// wait for serial port to connect. Needed for native USB port only

  WiFi.setPins(8,7,4,2);

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue:
    while (true);
  }

  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);
  }
  Serial.println("Connected to wifi");
  printWiFiStatus();

  // set the done pin for the TPL5111 to output
  pinMode(DONE_PIN, OUTPUT);

  rtc.begin(); // initialize RTC 24H format
  rtc.setTime(0, 0, 0);
  rtc.setDate(0, 0, 0);
  
  rtc.attachInterrupt(timerAlarm);

  rtc.setAlarmTime(0, 0, 1);
  rtc.enableAlarm(rtc.MATCH_SS);
}



void loop() {
  char buf[255];  
  //Serial.println("\nStarting connection to server...");

  while (!alarmWent) { delay(10); }
  alarmWent = false;

  sprintf(buf, "Time is %2.2d:%2.2d:%2.2d", rtc.getHours(), rtc.getMinutes(), rtc.getSeconds());
  Serial.println(buf);

  display.begin();
  display.clearBuffer();
  display.setTextWrap(true);
 
  sendHTTPRequest("temperature", 5, false, false);

  if (checkHTTPStatus()) {
    // Allocate the JSON document
    // Use arduinojson.org/v6/assistant to compute the capacity.
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 5000;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
  
    float avgtemp = 0.0;
    for (int i = 0; i < 5; i++) {
      avgtemp += doc[i]["value"].as<float>();
    }
    avgtemp /= 5.0;
    display.setCursor(10, 10);
    display.setTextSize(2);
    display.setTextColor(EPD_BLACK);
    sprintf(buf, "Temp: %2.1fC", avgtemp);
    Serial.println(buf);
    display.print(buf);
  }

  readTrailer();


  //Serial.println(F("Reading pressure..."));
  sendHTTPRequest("pressure", 5, false, false);

  float currentPressure = 0.0;
  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 1350;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
  
    float avgPressure = 0.0;
    for (int i = 0; i < 5; i++) {
      avgPressure += doc[i]["value"].as<float>();
    }
    avgPressure /= 5.0;
    currentPressure = avgPressure;
    display.setCursor(10, 35);
    display.setTextSize(1);
    display.setTextColor(EPD_BLACK);
    sprintf(buf, "%2.1f kPa", avgPressure);
    Serial.println(buf);
    display.print(buf);
  }

  readTrailer();

  // Read 48 hours of pressure to get trends
  sendHTTPRequest("pressure", 0, false, true);  
  if (checkHTTPStatus()) {
    Serial.println("Pressure check");
    const size_t capacity = 49*JSON_ARRAY_SIZE(2) + JSON_ARRAY_SIZE(48) + JSON_OBJECT_SIZE(3) + 2*JSON_OBJECT_SIZE(5) + 2320;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
    
    JsonObject parameters = doc["parameters"];
    JsonArray data = doc["data"];
    float p48 = data[0][1].as<float>();
    float p24 = data[24][1].as<float>();
    float p12 = data[36][1].as<float>();
    float p6 = data[42][1].as<float>();

    float p48diff = currentPressure - p48;
    float p24diff = currentPressure - p24;
    float p12diff = currentPressure - p12;
    float p6diff = currentPressure - p6;

    char p6Trend[20] = "\0";
    if (abs(p6diff) > 0.35)
      sprintf(p6Trend, "Rapidly ");

    if(abs(p6diff) < 0.15) {
      sprintf(p6Trend, "Steady");
    }
    else if (p6diff > 0.15) {
      sprintf(p6Trend, "%sRising", p6Trend);
    }
    else if (p6diff < 0.15) {
      sprintf(p6Trend, "%sFalling", p6Trend);
    }
    
    Serial.printf("48: %f : %f", p48, p48diff); Serial.println();
    Serial.printf("24: %f : %f", p24, p24diff); Serial.println();
    Serial.printf("12: %f : %f", p12, p12diff); Serial.println();
    Serial.printf(" 6: %f : %f", p6, p6diff); Serial.println();
    Serial.println(p6Trend);

    display.setCursor(70, 35);
    display.setTextSize(1);
    display.setTextColor(EPD_BLACK);
    sprintf(buf, "%s", p6Trend);
    display.print(buf);
  }

  readTrailer();

  //Serial.println(F("Reading humidity..."));
  sendHTTPRequest("humidity", 5, false, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 1350;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
  
    float avgHumidity = 0.0;
    for (int i = 0; i < 5; i++) {
      avgHumidity += doc[i]["value"].as<float>();
    }
    avgHumidity /= 5.0;
    display.setCursor(10, 47);
    display.setTextSize(1);
    display.setTextColor(EPD_BLACK);
    sprintf(buf, "Humidity: %2.1f %%RH", avgHumidity);
    Serial.println(buf);
    display.print(buf);
  }

  readTrailer();

  //Serial.println(F("Reading wind..."));
  float avgWindSpeed = 0.0;
  
  sendHTTPRequest("wind-speed", 2, false, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 1350;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
  
    for (int i = 0; i < 2; i++) {
      avgWindSpeed += doc[i]["value"].as<float>();
    }
    avgWindSpeed /= 2.0;
  }

  readTrailer();

  sendHTTPRequest("wind-gust", 1, false, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 2000;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }

    float windGust = doc[0]["value"].as<float>();
    display.setCursor(10, 59);
    display.setTextSize(1);
    display.setTextColor(EPD_BLACK);
    sprintf(buf, "Wind: %2.1f km/h Gust: %2.1f km/h", avgWindSpeed, windGust);
    Serial.println(buf);
    display.print(buf);

  }

  readTrailer();


  //Serial.println(F("Reading rain..."));
  sendHTTPRequest("rain", 60, false, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 1350;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
  
    float totalRain = 0.0;
    for (int i = 0; i < 60; i++) {
      totalRain += doc[i]["value"].as<float>();
    }
    display.setCursor(10, 71);
    display.setTextSize(1);
    display.setTextColor(EPD_BLACK);
    sprintf(buf, "Rain: %2.1fmm", totalRain);
    Serial.println(buf);
    display.print(buf);
  }

  readTrailer();

  //Serial.println(F("Reading battery..."));
  sendHTTPRequest("battery-voltage", 1, true, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 1350;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
  
    float batVoltage = doc[0]["value"].as<float>();

    display.setCursor(130, 92);
    display.setTextSize(1);
    display.setTextColor(EPD_RED);
    sprintf(buf, "Bat: %1.2f V", batVoltage);
    Serial.println(buf);
    display.print(buf);
  }

  //readTrailer();

  client.stop();

  // Measure the battery voltage
  float measuredvbat = analogRead(VBAT_PIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage

  display.setCursor(10, 92);
  display.setTextSize(1);
  display.setTextColor(EPD_RED);
  sprintf(buf, "LBat: %1.2f V", measuredvbat);
  Serial.println(buf);
  display.print(buf);

  if (getCurrentTimeFromWeb(buf) == true) {
    display.setCursor(10, 82);
    display.setTextSize(1);
    display.setTextColor(EPD_BLACK);
    Serial.println(buf);
    display.print(buf);
  }

 
  display.display();
  Serial.println("Display done");
  //delay(300000);

  digitalWrite(DONE_PIN, HIGH);
  delay(10);
  digitalWrite(DONE_PIN, LOW);
  delay(1000);

  Serial.println("We should never get here");
}


void printWiFiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

bool readTrailer() {
  char trailer[32] = {0};
  client.readBytesUntil('\r', trailer, sizeof(trailer));
  client.readBytesUntil('\r', trailer, sizeof(trailer));
  client.readBytesUntil('\r', trailer, sizeof(trailer));
  client.readBytesUntil('\r', trailer, sizeof(trailer));

  if (client.available() != 0)
    return false;

  return true;
}

bool checkHTTPStatus() {
    // Check HTTP status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
      Serial.print(F("Unexpected response: "));
      Serial.println(status);
      client.readBytesUntil('\r', status, sizeof(status));
      Serial.println(status);
      client.readBytesUntil('\r', status, sizeof(status));
      Serial.println(status);
      client.readBytesUntil('\r', status, sizeof(status));
      Serial.println(status);
      client.readBytesUntil('\r', status, sizeof(status));
      Serial.println(status);
      return false;
    }

    // Skip HTTP headers
    char endOfHeaders[] = "\r\n\r\n";
    if (!client.find(endOfHeaders)) {
      Serial.println(F("Invalid response"));
      return false;
    }

    char endOfLength[] = "\r\n";
    if (!client.find(endOfLength)) {
      Serial.println(F("Invalid response"));
      return false;
    }
}

bool sendHTTPRequest(char* feed, byte limit, bool connClose, bool isChart) {
  // if not connected connect
  if (client.connected() == false) {
    if (client.connectSSL(server, 443)) {
      Serial.println(F("Client connected to Adafruit IO"));
    }
    else {
      Serial.println(F("Client failed to connect to Adafruit IO "));
      return false;
    }
  }
  
  char request[200];
  if (isChart == true) {
    // hard coded right now to 1 hour resolution for 48 hours or data
    sprintf(request, "GET /api/v2/Gamblor21/feeds/%s/data/chart?hours=48&resolution=60", feed);
    sprintf(request, "%s HTTP/1.1", request);
   }
  else {  
    sprintf(request, "GET /api/v2/Gamblor21/feeds/%s/data?include=value", feed);
    if (limit > 0)
      sprintf(request, "%s&limit=%d HTTP/1.1", request, limit);
    else
      sprintf(request, "%s HTTP/1.1", request);
  }
  
  client.println(request);
  client.println("Host: io.adafruit.com");
  
  if (connClose == true)
    client.println("Connection: close");
    
  client.println();

  return true;
}

//http://worldtimeapi.org/api/timezone/America/Winnipeg
bool getCurrentTimeFromWeb(char* timeString) {
  if (client.connect("worldtimeapi.org", 80)) {
    Serial.println(F("Client connected to world time api"));
  }
  else {
    Serial.println(F("Client failed to connect to world time api"));
    return false;
  }

  client.println("GET /api/timezone/America/Winnipeg HTTP/1.1");
  client.println("Host: worldtimeapi.org");
  client.println("Accept: */*");
  //client.println("Connection: close");
  client.println();

  char status[255] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    Serial.print(F("Unexpected response: not 200"));
    return false;
  }

  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    return false;
  }

  const size_t capacity = JSON_OBJECT_SIZE(15) + 360;
  DynamicJsonDocument doc(capacity);
  
  // Parse JSON object
  DeserializationError error = deserializeJson(doc, client);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }
  
  const char* datetime = doc["datetime"]; // "2020-07-27T14:39:06.660033-05:00"
  
  memset(timeString, 0, strlen(timeString));
  strncpy(timeString, datetime+11, 5); 

  return true;
}

void timerAlarm() {
  nextAlarmMinute = (nextAlarmMinute + 5) % 60;
  rtc.setAlarmTime(0, nextAlarmMinute, 0);
  rtc.enableAlarm(rtc.MATCH_MMSS);
  alarmWent = true;
}
