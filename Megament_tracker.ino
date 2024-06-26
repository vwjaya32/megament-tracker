#include <TinyGPS++.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// #include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <HTTPClient.h>

const char* ssid = "OPPO A92";
const char* password = "mcdjv1234";
const char* serverName = "http://34.101.96.10:8005/location";
// const char* serverName = "https://jsonplaceholder.typicode.com/posts";
const int trackerId = 1;
const char* accessToken = "bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOjEsImVtYWlsIjoidXNlcjFAZXhhbXBsZS5jb20iLCJ1c2VybmFtZSI6InVzZXIxIiwiaWF0IjoxNzE3MzkwNTM5fQ.Gs6LXDhnxlSiTapfbeVMl2w2DPNimslUTY-4Ky8-Ta8";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
TinyGPSPlus gps;
// WiFiClientSecure client;

typedef struct {
  double lat;
  double lon;
  unsigned long timestamp;
} CachedGPSData;

CachedGPSData cachedData[1000]; // Adjust the size as needed
int cacheIndex = 0;

TaskHandle_t Task1, Task2;

void coreTask1(void* param) {
  while (true) {
    updateGPSAndDisplay();
    delay(1000);
  }
}

void coreTask2(void* param) {
  while (true) {
    handleWiFiAndAPI();
    delay(5000);
  }
}

void setup() {
  Serial.begin(115200);
  // Serial2.begin(9600, SERIAL_8N1, 16, 17);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  // Check and setup OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
  }

  // Connect to SSID
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    displayConnecting();
  }

  delay(3000);

  display.clearDisplay();
  display.setCursor(35, 16);
  display.print("Connected");
  display.display();

  delay(3000);

  // client.setInsecure();

  xTaskCreatePinnedToCore(coreTask1, "Task1", 10000, NULL, 1, &Task1, 0);
  xTaskCreatePinnedToCore(coreTask2, "Task2", 10000, NULL, 1, &Task2, 1);
}

void loop() {}

void updateGPSAndDisplay() {
  while(Serial2.available()){
    gps.encode(Serial2.read());
  }

  if (millis() > 5000 && gps.charsProcessed() < 10) {
    Serial.println(F("No GPS detected: check wiring."));
  }

  displayGPSInfo();
}

void displayConnecting(){
  display.clearDisplay();
  display.setCursor(30, 16);
  display.print("Connecting");
  display.display();
  for (int i = 0; i < 3; i++) {
    display.print(".");
    display.display();
  }
}

void displayGPSInfo() {
  if (gps.location.isValid()) {
    display.clearDisplay();

    display.setCursor(0, 0);
    display.print("SSID: ");
    display.print(WiFi.SSID());

    display.setCursor(0, 16);
    display.print("Lat: ");
    display.print(gps.location.lat(), 6);
    display.setCursor(0, 32);
    display.print("Lon: ");
    display.print(gps.location.lng(), 6);
    display.setCursor(0, 48);
    display.print("Cached Index: ");
    display.print(cacheIndex);
    display.display();
  } else {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("No GPS data");
    display.setCursor(0, 16);
    display.print("Check Wiring and GPS Signal");
    display.display();
  }
}

String getTimestampString(unsigned long timestamp) {
  char buffer[21];
  int year = gps.date.year();
  int month = gps.date.month();
  int day = gps.date.day();
  int hour = gps.time.hour();
  int minute = gps.time.minute();
  int second = gps.time.second();
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
  return String(buffer);
}

void handleWiFiAndAPI() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    // http.begin(client, serverName);
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", accessToken);

    bool requestSuccessful = false;
    int httpResponseCode;

    // Send cached data first
    while (cacheIndex > 0) {
      Serial.println("Sending cached requests.. index:"+String(cacheIndex));
      String postData = "{\"trackerId\":" + String(trackerId) + ", \"latitude\":" + String(cachedData[cacheIndex - 1].lat, 6) + ", \"longitude\":" + String(cachedData[cacheIndex - 1].lon, 6) + ", \"timestamp\":\"" + getTimestampString(cachedData[cacheIndex - 1].timestamp) + "\"}";

      httpResponseCode = http.POST(postData);

      if (httpResponseCode == 200 || httpResponseCode == 201) {
        Serial.println("HTTP POST Successful");
        requestSuccessful = true;

        String response = http.getString(); // Get the response payload
        Serial.println("Response: " + response); // Print the response payload
        cacheIndex--;
      } else {
        requestSuccessful = false;
        Serial.println("Error in sending HTTP POST request: " + String(httpResponseCode));
        break;
      }
    }

    // Send current GPS data if available
    if (gps.location.isValid()) {
      String postData = "{\"trackerId\":" + String(trackerId) + ", \"latitude\":" + String(gps.location.lat(), 6) + ", \"longitude\":" + String(gps.location.lng(), 6) + ", \"timestamp\":\"" + getTimestampString(millis() / 1000) + "\"}";

      httpResponseCode = http.POST(postData);

      if (httpResponseCode == 200 || httpResponseCode == 201) {
        requestSuccessful = true;
        Serial.println("HTTP POST Successful");
      } else {
        Serial.println(String(httpResponseCode));
        requestSuccessful = false;
        Serial.println("Error in sending HTTP POST request: " + String(httpResponseCode));
        
        Serial.println("Caching request.. index: "+ String(cacheIndex));

        // Cache the GPS data
        if (cacheIndex < 1000) {
          cachedData[cacheIndex].lat = gps.location.lat();
          cachedData[cacheIndex].lon = gps.location.lng();
          cachedData[cacheIndex].timestamp = millis() / 1000;
          cacheIndex++;
        } else {
          Serial.println("Cache is full, unable to store more data.");
        }
      }
      String response = http.getString(); // Get the response payload
      Serial.println("Response: " + response); // Print the response payload
       
    } else {
      Serial.println("GPS not calibrated yet!");
    }

    http.end();
  } else {
    Serial.println("Wi-Fi is not connected");
  }
}