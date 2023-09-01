
#include <SPI.h>
#include <LoRa.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "MQ135.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include "time.h"

// GPIO where the DS18B20 is connected to
const int oneWireBus = 4;

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);
// Pass our oneWire reference to Dallas Temperature sensor
DallasTemperature tempsensor(&oneWire);

const char* ssid = "Team . NET";
const char* password = "Nepo913913";
WebServer server(80);

//define the pins used by the transceiver module
#define ss 5
#define rst 14
#define dio0 2

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 32  // OLED display height, in pixels
// Declaration for SSD1306 display connected using I2C
#define OLED_RESET -1  // Reset pin
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// HC-SR04 Module Pin Connection with ESP32
const int trigPin = 33;
const int echoPin = 32;
const int gasPin = 35;

struct tm timeinfo;

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

// Instanciating gas sensor module
MQ135 gasSensor = MQ135(gasPin);

//define sound speed in cm/uS
#define SOUND_SPEED 0.034
#define CM_TO_INCH 0.393701

float ppm;
long duration;
float distanceCm;
float gasValue;

void setTimezone(String timezone) {
  Serial.printf("  Setting Timezone to %s\n", timezone.c_str());
  setenv("TZ", timezone.c_str(), 1);  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();
}

void initTime(String timezone) {

  Serial.println("Setting up time");
  configTime(0, 0, ntpServer);  // First connect to NTP server, with 0 TZ offset
  if (!getLocalTime(&timeinfo)) {
    Serial.println("  Failed to obtain time in init function.");
    return;
  }
  Serial.println("  Got the time from NTP");
  // Now we can set the real timezone
  setTimezone(timezone);
}

void setTime(int yr, int month, int mday, int hr, int minute, int sec, int isDst) {
  struct tm tm;

  tm.tm_year = yr - 1900;  // Set date
  tm.tm_mon = month - 1;
  tm.tm_mday = mday;
  tm.tm_hour = hr;  // Set time
  tm.tm_min = minute;
  tm.tm_sec = sec;
  tm.tm_isdst = isDst;  // 1 or 0
  time_t t = mktime(&tm);
  Serial.printf("Setting time: %s", asctime(&tm));
  struct timeval now = { .tv_sec = t };
  settimeofday(&now, NULL);
}

void setup() {
  //initialize Serial Monitor
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);  // Sets the trigPin as an Output
  pinMode(echoPin, INPUT);   // Sets the echoPin as an Input

  // Start the DS18B20 sensor
  tempsensor.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  initTime("WET0WEST,M3.5.0/1,M10.5.0");

  //init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  String formattedTime = getFormattedTime();
  Serial.println("Formatted Time: " + formattedTime);

  delay(100);
  server.on("/", handle_OnConnect);
  server.on("/test", handle_OnTest);
  server.onNotFound(handle_NotFound);
  server.begin();
  Serial.println("HTTP server started");

  while (!Serial)
    ;
  Serial.println("LoRa Transmitter Module");

  //setup LoRa transceiver module
  LoRa.setPins(ss, rst, dio0);

  //Location's frequency
  //433E6 for Asia
  //866E6 for Europe
  //915E6 for North America
  while (!LoRa.begin(433E6)) {
    Serial.println(".");
    delay(500);
  }
  // Change sync word (0xF3) to match the receiver
  // The sync word assures you don't get LoRa messages from other LoRa transceivers
  // ranges from 0-0xFF
  LoRa.setSyncWord(0xF3);
  Serial.println("LoRa Initializing OK!");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else  // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  Serial.println("Ready.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();

  setTimezone("<+0545>-5:45");

  tempsensor.requestTemperatures();
  float temperatureC = tempsensor.getTempCByIndex(0);
  // float temperatureF = tempsensor.getTempFByIndex(0);

  float rzero = gasSensor.getRZero();
  float ppm = gasSensor.getPPM();

  // Clears the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(echoPin, HIGH);

  // Calculate the distance
  distanceCm = duration * SOUND_SPEED / 2;
  gasValue = ppm;
  // initialize the OLED object
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }

  String formattedTime = getFormattedTime();
  // Display Text
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("HC-SR04");
  display.print("Val: ");
  display.println(distanceCm);
  display.display();
  Serial.print("Sending Data: NODE1|HC-SR04|");
  Serial.print(distanceCm);
  Serial.print("|");
  Serial.println(formattedTime);
  LoRa.beginPacket();
  LoRa.print("NODE1|HC-SR04|");
  LoRa.print(distanceCm);
  LoRa.print("|");
  LoRa.print(formattedTime);
  LoRa.endPacket();
  delay(1000);

  // Display Text
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("DS18B20");
  display.print("Val: ");
  display.println(temperatureC);
  display.display();

  Serial.print("Sending Data: NODE2|DS18B20|");
  Serial.print(temperatureC);
  Serial.print("|");
  Serial.println(formattedTime);
  LoRa.beginPacket();
  LoRa.print("NODE2|DS18B20|");
  LoRa.print(temperatureC);
  LoRa.print("|");
  LoRa.print(formattedTime);
  LoRa.endPacket();
  delay(1000);

  // Display Text
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("MQ-135");
  display.print("Val: ");
  display.println(ppm);
  display.display();
  Serial.print("Sending Data: NODE3|MQ-135|");
  Serial.print(ppm);
  Serial.print("|");
  Serial.println(formattedTime);
  LoRa.beginPacket();
  LoRa.print("NODE3|MQ-135|");
  LoRa.print(ppm);
  LoRa.print("|");
  LoRa.print(formattedTime);
  LoRa.endPacket();
  delay(1000);

  server.handleClient();
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "Failed to obtain time";
  }

  char timeString[50];  // This buffer will hold the formatted time
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);

  return String(timeString);
}


void handle_OnConnect() {
  server.send(200, "text/html", SendHTML(distanceCm, gasValue));
}

void handle_OnTest() {
  server.send(200, "text/plain", "Hello from test page.");
}

void handle_NotFound() {
  server.send(404, "text/plain", "Page not found.");
}

String SendHTML(float distCm, float gasVal) {
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>Web Server - ESP32</title>\n";
  ptr += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr += ".button {display: block;width: 80px;background-color: #3498db;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr += ".button-on {background-color: #3498db;}\n";
  ptr += ".button-on:active {background-color: #2980b9;}\n";
  ptr += ".button-off {background-color: #34495e;}\n";
  ptr += ".button-off:active {background-color: #2c3e50;}\n";
  ptr += "p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "<h1>ESP32 Web Server</h1>\n";
  ptr += "<h3>Using Local Mode</h3>\n";


  ptr += "<h4>MQ-135: ";
  ptr += gasVal;
  ptr += "</h4>\n";
  ptr += "<h4>HC-SR04: ";
  ptr += distCm;
  ptr += "</h4>\n";


  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
}
