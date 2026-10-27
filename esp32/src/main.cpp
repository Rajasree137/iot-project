/*
  ==============================================================================
  Final-Year B.E. Electronics and Communication Engineering Project:
  "IoT and AI Based Smart Mushroom Cultivation System for Contamination Detection and Yield Prediction"
  
  Module 1: ESP32 IoT Hardware Telemetry & Automated Actuator Controller
  ==============================================================================

  Hardware Setup:
  - Microcontroller : ESP32 DevKit V1
  - DHT11 Sensor    : Data Pin -> GPIO 5
  - MQ135 Sensor    : Analog Output (AO) -> GPIO 34 (ADC1_CH6)
  - 2-Channel Relay :
      - Relay 1 (Fan)        -> GPIO 26
      - Relay 2 (Humidifier) -> GPIO 27
  - (Optional) I2C OLED (SSD1306 128x64) :
      - SDA -> GPIO 21
      - SCL -> GPIO 22

  Data Flow:
  ESP32 Sensors -> Autonomous Relay Control -> Wi-Fi JSON POST -> Flask Backend (:5001/api/sensor-data)
  ==============================================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>

// Set to 1 if you have an I2C SSD1306 OLED connected (SDA=21, SCL=22)
// Set to 0 if you are not using an OLED screen (compiles with zero display dependencies)
#define ENABLE_OLED 1

#if ENABLE_OLED
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  #define SCREEN_WIDTH 128
  #define SCREEN_HEIGHT 64
  #define OLED_RESET -1
  #define OLED_I2C_ADDR 0x3C
  Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
  bool oledAvailable = false;
#endif

// ==============================================================================
// 1. WI-FI & BACKEND NETWORK CONFIGURATION
// ==============================================================================
const char* WIFI_SSID     = "Galaxy A16 5G B9D4";
const char* WIFI_PASSWORD = "luna1234";

// Target Flask Backend Telemetry Endpoint (Edge Server on Laptop)
const char* BACKEND_URL   = "http://10.86.232.25:5001/api/sensor-data";

// Telemetry interval (send reading every ~3 seconds)
const unsigned long TELEMETRY_INTERVAL_MS = 3000;

// ==============================================================================
// 2. PIN DEFINITIONS & THRESHOLDS
// ==============================================================================
#define DHTPIN 5
#define DHTTYPE DHT11

#define MQ135_PIN 34
#define RELAY_FAN 26
#define RELAY_HUMIDIFIER 27

// Actuator control thresholds for Oyster / Button mushroom cultivation
const float HUMIDITY_MIN_THRESHOLD      = 75.0; // Humidifier ON if humidity < 75%
const int   MQ135_POOR_ANALOG_THRESHOLD = 1500; // POOR air if analog ADC > 1500

// Relay trigger logic (Most Arduino 5V relay modules are Active LOW: LOW=ON, HIGH=OFF)
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// ==============================================================================
// 3. GLOBAL STATE & FORWARD DECLARATIONS
// ==============================================================================
DHT dht(DHTPIN, DHTTYPE);
unsigned long lastTelemetryTime = 0;

void connectWiFi();
void updateOLED(float temp, float hum, const String& airQuality, bool fanState, bool humState);
bool sendTelemetry(float temp, float hum, const String& airQuality, bool fanState, bool humState);

// ==============================================================================
// 4. HELPER FUNCTIONS
// ==============================================================================

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println();
  Serial.print("[Wi-Fi] Connecting to SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[Wi-Fi] Connected successfully!");
    Serial.print("[Wi-Fi] ESP32 Assigned IP : ");
    Serial.println(WiFi.localIP());
    Serial.print("[Wi-Fi] Target Backend URL: ");
    Serial.println(BACKEND_URL);
  } else {
    Serial.println("\n[Wi-Fi] Connection failed! Will retry automatically...");
  }
}

void updateOLED(float temp, float hum, const String& airQuality, bool fanState, bool humState) {
#if ENABLE_OLED
  if (!oledAvailable) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("MUSHROOM IOT MONITOR");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  display.setCursor(0, 15);
  display.printf("Temp: %.1f C\n", temp);
  display.printf("Hum : %.1f %%\n", hum);
  display.printf("Air : %s\n", airQuality.c_str());
  display.printf("Fan : %s | Hum: %s\n", fanState ? "ON" : "OFF", humState ? "ON" : "OFF");

  display.setCursor(0, 54);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: OK -> :5001");
  } else {
    display.print("WiFi: Disconnected");
  }

  display.display();
#endif
}

bool sendTelemetry(float temp, float hum, const String& airQuality, bool fanState, bool humState) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Wi-Fi disconnected. Reconnecting...");
    connectWiFi();
    return false;
  }

  HTTPClient http;
  http.begin(BACKEND_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(2500); // 2.5 second timeout

  // Build validated JSON payload strictly matching Flask /api/sensor-data contract
  String payload = "{";
  payload += "\"temperature\":" + String(temp, 1) + ",";
  payload += "\"humidity\":" + String(hum, 1) + ",";
  payload += "\"air_quality\":\"" + airQuality + "\",";
  payload += "\"fan_status\":" + String(fanState ? "true" : "false") + ",";
  payload += "\"humidifier_status\":" + String(humState ? "true" : "false");
  payload += "}";

  Serial.print("[HTTP POST] Sending payload: ");
  Serial.println(payload);

  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    Serial.printf("[HTTP POST] Success! Code: %d\n", httpCode);
    String response = http.getString();
    Serial.printf("[HTTP POST] Server response: %s\n", response.c_str());
    http.end();
    return (httpCode == 200 || httpCode == 201);
  } else {
    Serial.printf("[HTTP POST] Transmission failed: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    return false;
  }
}

// ==============================================================================
// 5. SETUP & MAIN LOOP
// ==============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==================================================");
  Serial.println("Smart Mushroom Cultivation Telemetry & Actuator Hub");
  Serial.println("==================================================");

  // Initialize Relays (Ensure default state is OFF)
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(RELAY_HUMIDIFIER, OUTPUT);
  digitalWrite(RELAY_FAN, RELAY_OFF);
  digitalWrite(RELAY_HUMIDIFIER, RELAY_OFF);

  // Initialize MQ135 Analog ADC Pin
  pinMode(MQ135_PIN, INPUT);
  analogReadResolution(12); // Standard 12-bit ADC (0 - 4095)

  // Initialize DHT11 Sensor
  dht.begin();
  Serial.println("[Sensors] DHT11 initialized on GPIO 5");

#if ENABLE_OLED
  // Initialize OLED (I2C: SDA=GPIO 21, SCL=GPIO 22)
  Wire.begin(21, 22);
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    oledAvailable = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.println("Mushroom Monitor");
    display.setCursor(10, 35);
    display.println("Initializing...");
    display.display();
    Serial.println("[Display] SSD1306 OLED initialized at 0x3C");
  } else {
    Serial.println("[Display] OLED not detected (Optional). Continuing...");
  }
#endif

  // Initial Wi-Fi connection
  connectWiFi();
}

void loop() {
  // Ensure Wi-Fi stays connected
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  unsigned long currentMillis = millis();

  // Run telemetry and closed-loop control every 3 seconds
  if (currentMillis - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryTime = currentMillis;

    // 1. Read DHT11 Sensor
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    // 2. Validate sensor readings (Rejects NaN / Infinity)
    if (isnan(temperature) || isnan(humidity) || isinf(temperature) || isinf(humidity)) {
      Serial.println("[Sensors WARN] DHT11 reading not ready. Will retry next cycle.");
      return;
    }

    // 3. Read MQ135 Air Quality Sensor
    int rawAir = analogRead(MQ135_PIN);
    String airQuality = (rawAir > MQ135_POOR_ANALOG_THRESHOLD) ? "POOR" : "GOOD";

    // 4. Closed-Loop Autonomous Actuator Control Logic
    // Condition A: If air quality is POOR -> Turn Fan ON
    bool fanActive = false;
    if (airQuality == "POOR") {
      digitalWrite(RELAY_FAN, RELAY_ON);
      fanActive = true;
    } else {
      digitalWrite(RELAY_FAN, RELAY_OFF);
      fanActive = false;
    }

    // Condition B: If humidity is below 75% -> Turn Humidifier ON
    bool humidifierActive = false;
    if (humidity < HUMIDITY_MIN_THRESHOLD) {
      digitalWrite(RELAY_HUMIDIFIER, RELAY_ON);
      humidifierActive = true;
    } else {
      digitalWrite(RELAY_HUMIDIFIER, RELAY_OFF);
      humidifierActive = false;
    }

    Serial.printf("[Tele] Temp: %.1f C | Hum: %.1f %% | Air: %s (%d) | Fan: %s | Hum: %s\n",
                  temperature, humidity, airQuality.c_str(), rawAir,
                  fanActive ? "ON" : "OFF", humidifierActive ? "ON" : "OFF");

    // 5. Update Local OLED Display
    updateOLED(temperature, humidity, airQuality, fanActive, humidifierActive);

    // 6. Transmit Telemetry JSON to Flask Backend
    sendTelemetry(temperature, humidity, airQuality, fanActive, humidifierActive);
  }
}
