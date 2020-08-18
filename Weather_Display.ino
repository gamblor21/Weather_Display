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

//#include <RTCZero.h>
// Note on RTC

#include <SPI.h>
#include <WiFi101.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include "Adafruit_EPD.h"

#include <Fonts/FreeSans7pt7b.h>
#include <Fonts/FreeSans8pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include <ArduinoJson.h>
#include "secrets.h" 

#define EPD_CS     13
#define EPD_DC      12
#define SRAM_CS     11
#define EPD_RESET   10 // can set to -1 and share with microcontroller Reset!
#define EPD_BUSY    -1 // can set to -1 to not use a pin (will wait a fixed delay)
#define DONE_PIN  5 // For the TPL5111
#define VBAT_PIN A7

char ssid[] = SECRET_SSID;     // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)

int status = WL_IDLE_STATUS;

char server[] = "io.adafruit.com";    // name address for Google (using DNS)
char adafruit_user[] = "Gamblor21"; // user on Adafruit IO we are pulling requests from
char timezone[] = "America/Winnipeg"; // timezone to request from worldtimeapi.org

WiFiClient client;

Adafruit_IL0373 display(212, 104, EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY);

int nextAlarmMinute = 0;
volatile bool alarmWent = false;

void setup() {
  Serial.begin(115200);
  //while (!Serial) { ; }

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
    status = WiFi.begin(ssid, pass);
  }
  Serial.println("Connected to wifi");
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");

  // set the done pin for the TPL5111 to output
  pinMode(DONE_PIN, OUTPUT);
}

// Variables we will display
float avgtemp = 0.0;
float avgPressure = 0.0;
char p6Trend[20] = "\0";
float avgHumidity = 0.0;
float avgWindSpeed = 0.0;
int windDirection = 0.0;
char windDirectionString[5] = "\0";
float windGust = 0.0;
float totalRain = 0.0;  
float batVoltage = 0.0;
float measuredvbat = 0.0;
char currentTime[255] = "\0";

void loop() {
  // Read temperature
  sendHTTPRequest("temperature", 5, false, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 5000;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      client.stop();
    }
    else {
      readTrailer();
    
      for (int i = 0; i < 5; i++) {
        avgtemp += doc[i]["value"].as<float>();
      }
      
      avgtemp /= 5.0;
    }
  }

  // Read Pressure
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
      client.stop();
    }
    else {
      readTrailer();
    
      for (int i = 0; i < 5; i++) {
        avgPressure += doc[i]["value"].as<float>();
      }
      avgPressure /= 5.0;
      currentPressure = avgPressure;
    }
  }


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
      client.stop();
    }
    else {
      readTrailer();
      
      JsonObject parameters = doc["parameters"];
      JsonArray data = doc["data"];

      // Look at several tends but right now only display the 6 hour trend
      float p48 = data[0][1].as<float>();
      float p24 = data[24][1].as<float>();
      float p12 = data[36][1].as<float>();
      float p6 = data[42][1].as<float>();
  
      float p48diff = currentPressure - p48;
      float p24diff = currentPressure - p24;
      float p12diff = currentPressure - p12;
      float p6diff = currentPressure - p6;
  
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
    }
  }

  // Read Humidty
  sendHTTPRequest("humidity", 5, false, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 1350;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      client.stop();
    }
    else {
      readTrailer();
    
      for (int i = 0; i < 5; i++) {
        avgHumidity += doc[i]["value"].as<float>();
      }
      avgHumidity /= 5.0;
    }
  }

  // Reading wind speed
  sendHTTPRequest("wind-speed", 2, false, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 1350;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      client.stop();
    }
    else {
      readTrailer();
    
      for (int i = 0; i < 2; i++) {
        avgWindSpeed += doc[i]["value"].as<float>();
      }
      avgWindSpeed /= 2.0;
    }
  }

  // Read wind gust
  sendHTTPRequest("wind-gust", 1, false, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 2000;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      client.stop();
    }
    else {
      readTrailer();
  
      windGust = doc[0]["value"].as<float>();
    }
  }

  // Read wind direction
  sendHTTPRequest("wind-direction", 1, false, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 1350;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      client.stop();
    }
    else {
      readTrailer();
  
      windDirection = doc[0]["value"].as<int>();
      windDirectionToString(windDirection, windDirectionString);
    }
  }
  
  // Reading rain in the last 60 minutes
  sendHTTPRequest("rain", 60, false, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 1350;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      client.stop();
    }
    else {
      readTrailer();
  
      for (int i = 0; i < 60; i++) {
        totalRain += doc[i]["value"].as<float>();
      }
    }
  }
  
  // Reading remote battery level
  sendHTTPRequest("battery-voltage", 1, true, false);

  if (checkHTTPStatus()) {
    const size_t capacity = 5*JSON_ARRAY_SIZE(3) + JSON_ARRAY_SIZE(5) + 10*JSON_OBJECT_SIZE(2) + 5*JSON_OBJECT_SIZE(11) + 1350;
    DynamicJsonDocument doc(capacity);
  
    // Parse JSON object
    DeserializationError error = deserializeJson(doc, client);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      client.stop();
    }
    else {
      batVoltage = doc[0]["value"].as<float>();
    }
  }

  client.stop();

  // Measure the local battery voltage
  measuredvbat = analogRead(VBAT_PIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage

  // Read the time from worldtimeapi.org
  getCurrentTimeFromWeb(currentTime);

  // Display all the values
  displayValues();

  // Signal the TPS5111 we are done and to power everything down
  digitalWrite(DONE_PIN, HIGH);
  delay(10);
  digitalWrite(DONE_PIN, LOW);
  delay(1000);

  Serial.println("We should never get here");
  while (true) { delay(1000); }
}

// Display all the values read to the eInk display
void displayValues() {
  char buf[255];
  int16_t x1, y1;
  uint16_t w, h;
  
  display.begin();
  display.clearBuffer();
  display.setTextWrap(false);

  display.setTextColor(EPD_BLACK);
  
  display.setTextSize(1);
  display.setFont(&FreeSansBold24pt7b);
  sprintf(buf, "%2.1fC", avgtemp);
  Serial.println(buf);
  display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(106-(w/2), 37);
  display.print(buf);

  display.setFont(&FreeSans9pt7b);
  sprintf(buf, "%2.1fkPa", avgPressure);
  Serial.println(buf);
  display.setCursor(2, 55);
  display.print(buf);

  display.setFont(&FreeSans7pt7b);
  sprintf(buf, "%s", p6Trend);
  Serial.println(buf);
  display.setCursor(5, 67);
  display.print(buf);

  display.setFont(&FreeSans9pt7b);
  sprintf(buf, "%2.1f %%RH", avgHumidity);
  Serial.println(buf);
  display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(208-w, 55);
  display.print(buf);

  display.setFont(&FreeSans8pt7b);
  sprintf(buf, "%2.1f / %2.1f km/h %s %d", avgWindSpeed, windGust, windDirectionString, windDirection);
  Serial.println(buf);
  display.setCursor(2, 85);
  display.print(buf);

  display.setFont(&FreeSans9pt7b);
  sprintf(buf, "%2.1fmm", totalRain);
  Serial.println(buf);
  display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(208-w, 70);
  //display.setCursor(2, 90);
  display.print(buf);

  display.setFont();
  display.setTextColor(EPD_RED);
  sprintf(buf, "Bat: %1.2f V", batVoltage);
  Serial.println(buf);
  display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(208-w, 94);
  display.print(buf);

  display.setFont();
  display.setCursor(2, 94);
  display.setTextColor(EPD_RED);
  sprintf(buf, "LBat: %1.2f V", measuredvbat);
  Serial.println(buf);
  display.print(buf);

  display.setFont(&FreeSans7pt7b);
  display.setTextColor(EPD_BLACK);
  Serial.println(currentTime);
  display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(106-w, 94);
  display.setCursor(90, 100);
  display.print(currentTime);

  display.display();
}

// Read the data after the JSON request. We can then use keep-alives to reuse the connections
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

// For a new connection make sure we got a 200 response and read the headers we are ignoring
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

// Send an HTTP request to Adafruit IO takes the feed name, the limit of how many records to return, if the connection
// should be kept open and do we want the raw data or a summary of 48 hours at 60 minute resolution
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
    sprintf(request, "GET /api/v2/%s/feeds/%s/data/chart?hours=48&resolution=60", adafruit_user, feed);
    sprintf(request, "%s HTTP/1.1", request);
   }
  else {  
    sprintf(request, "GET /api/v2/%s/feeds/%s/data?include=value", adafruit_user, feed);
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

// Gets the current time from worldtimeapi.org and save the time string to the timeString buffer
bool getCurrentTimeFromWeb(char* timeString) {
  if (client.connect("worldtimeapi.org", 80)) {
    Serial.println(F("Client connected to world time api"));
  }
  else {
    Serial.println(F("Client failed to connect to world time api"));
    return false;
  }

  char buf[255];
  sprintf(buf, "GET /api/timezone/%s HTTP/1.1", timezone);
  client.println(buf);
  client.println("Host: worldtimeapi.org");
  client.println("Accept: */*");
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
    client.stop();
    return false;
  }
  
  const char* datetime = doc["datetime"]; // "2020-07-27T14:39:06.660033-05:00"
  
  memset(timeString, 0, strlen(timeString));
  strncpy(timeString, datetime+11, 5); 

  return true;
}

/* 
 *Translate the wind in degrees to a cardinal direction
 */
static char* directions[] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };

bool windDirectionToString(int degrees, char *dirString) {
  int index = ((float)degrees + 11.25) / (float)22.5;
  //Serial.println(index);
  strcpy(dirString, directions[index%16]);
  
  return true;
}
