/*
  ==============================================================================
  Final-Year B.E. Electronics and Communication Engineering Project:
  "IoT and AI Based Smart Mushroom Cultivation System for Contamination Detection and Yield Prediction"
  
  Module 1: ESP32 IoT Hardware Telemetry & Automated Actuator Controller
  Includes: DHT11, MQ135, 2-Channel Relay, SSD1306 I2C OLED Display & Blynk IoT Cloud
  ==============================================================================

  Hardware Pin Mapping:
  - Microcontroller : ESP32 DevKit V1
  - DHT11 Sensor    : Data Pin  -> GPIO 5
  - MQ135 Sensor    : Analog AO -> GPIO 34 (ADC1_CH6)
  - 2-Channel Relay :
      - Relay 1 (Fan)        -> GPIO 26
      - Relay 2 (Humidifier) -> GPIO 27
  - SSD1306 I2C OLED (128x64):
      - SDA / SCA -> GPIO 21
      - SCL       -> GPIO 22
      - VCC       -> 3.3V
      - GND       -> GND

  Cloud & Edge Telemetry:
  1. Blynk IoT Cloud 2.0 (Virtual Pins V0 - V6)
  2. Flask REST Backend API (Local Edge Server JSON POST)
  3. SSD1306 OLED Real-Time Display with Environmental Threshold Conditions
  ==============================================================================
*/

// ==============================================================================
// 1. BLYNK IOT 2.0 CLOUD CONFIGURATION (Must be at the very top of file)
// ==============================================================================
#define BLYNK_TEMPLATE_ID   "TMPLxxxxxx"            // Replace with your Template ID from blynk.cloud
#define BLYNK_TEMPLATE_NAME "Mushroom Monitor"     // Replace with your Template Name
#define BLYNK_AUTH_TOKEN    "YOUR_BLYNK_AUTH_TOKEN" // Replace with your Device Auth Token
#define BLYNK_PRINT Serial

// Set to 1 to enable Blynk IoT Cloud; Set to 0 to run local-only without Blynk
#define ENABLE_BLYNK 1

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>

#if ENABLE_BLYNK
  #include <WiFiClient.h>
  #include <BlynkSimpleEsp32.h>
#endif

// ==============================================================================
// 2. OLED DISPLAY CONFIGURATION & I2C PIN DEFINITIONS
// ==============================================================================
#define ENABLE_OLED 1

// Explicit I2C OLED Pin Definitions (SDA / SCA and SCL)
#define OLED_SDA        21   // I2C Serial Data Pin (SDA / SCA) -> GPIO 21
#define OLED_SCL        22   // I2C Serial Clock Pin (SCL)     -> GPIO 22
#define OLED_RESET      -1   // Reset pin (-1 if sharing Arduino reset pin)
#define OLED_I2C_ADDR   0x3C // Standard 0.96" SSD1306 I2C Address (0x3C)
#define SCREEN_WIDTH    128  // OLED display width in pixels
#define SCREEN_HEIGHT   64   // OLED display height in pixels

#if ENABLE_OLED
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
  bool oledAvailable = false;
#endif

// ==============================================================================
// 3. WI-FI & FLASK BACKEND CONFIGURATION
// ==============================================================================
const char* WIFI_SSID     = "Galaxy A16 5G B9D4";
const char* WIFI_PASSWORD = "luna1234";

// Target Flask Backend Telemetry Endpoint (Edge Server on Laptop)
const char* BACKEND_URL   = "http://10.86.232.25:5001/api/sensor-data";

// Telemetry interval (send reading every ~3 seconds)
const unsigned long TELEMETRY_INTERVAL_MS = 3000;

// ==============================================================================
// 4. SENSOR & RELAY PIN DEFINITIONS & CONTROL THRESHOLDS
// ==============================================================================
#define DHTPIN 5
#define DHTTYPE DHT11

#define MQ135_PIN 34
#define RELAY_FAN 26
#define RELAY_HUMIDIFIER 27

// Actuator control thresholds for Oyster / Button mushroom cultivation
const float HUMIDITY_MIN_THRESHOLD      = 75.0; // Humidifier ON if humidity < 75%
const int   MQ135_POOR_ANALOG_THRESHOLD = 1500; // POOR air if analog ADC > 1500

// Relay trigger logic (Standard 5V relay modules are Active LOW: LOW=ON, HIGH=OFF)
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// ==============================================================================
// 5. GLOBAL STATE VARIABLES & FORWARD DECLARATIONS
// ==============================================================================
DHT dht(DHTPIN, DHTTYPE);
unsigned long lastTelemetryTime = 0;

// Actuator & Remote Control states
bool manualOverride = false;          // 0 = Autonomous Mode, 1 = Manual Mode from Blynk App
bool fanManualState = false;          // Remote Fan state from Blynk App V3
bool humidifierManualState = false;   // Remote Humidifier state from Blynk App V4
bool fanActive = false;               // Current Fan hardware status
bool humidifierActive = false;        // Current Humidifier hardware status

void connectWiFi();
void updateOLED(float temp, float hum, const String& airQuality, bool fanState, bool humState, int rawAir, bool manualMode);
bool sendTelemetry(float temp, float hum, const String& airQuality, int rawAir, bool fanState, bool humState, bool manualMode);

// ==============================================================================
// 6. BLYNK VIRTUAL PIN HANDLERS (Remote App Control)
// ==============================================================================
#if ENABLE_BLYNK
// V5: Mode Selection (0 = Auto Mode, 1 = Manual Mode)
BLYNK_WRITE(V5) {
  manualOverride = (param.asInt() == 1);
  Serial.printf("[Blynk] Control Mode Changed: %s\n", manualOverride ? "MANUAL OVERRIDE" : "AUTONOMOUS CLOSED-LOOP");
}

// V3: Fan Relay Actuator (Switch widget in Blynk App)
BLYNK_WRITE(V3) {
  int val = param.asInt();
  if (manualOverride) {
    fanManualState = (val == 1);
    digitalWrite(RELAY_FAN, fanManualState ? RELAY_ON : RELAY_OFF);
    fanActive = fanManualState;
    Serial.printf("[Blynk] Manual Fan -> %s\n", fanManualState ? "ON" : "OFF");
  } else {
    Serial.println("[Blynk] Fan switch ignored: System is in AUTONOMOUS mode (toggle V5 to override)");
    Blynk.virtualWrite(V3, fanActive ? 1 : 0);
  }
}

// V4: Humidifier Relay Actuator (Switch widget in Blynk App)
BLYNK_WRITE(V4) {
  int val = param.asInt();
  if (manualOverride) {
    humidifierManualState = (val == 1);
    digitalWrite(RELAY_HUMIDIFIER, humidifierManualState ? RELAY_ON : RELAY_OFF);
    humidifierActive = humidifierManualState;
    Serial.printf("[Blynk] Manual Humidifier -> %s\n", humidifierManualState ? "ON" : "OFF");
  } else {
    Serial.println("[Blynk] Humidifier switch ignored: System is in AUTONOMOUS mode (toggle V5 to override)");
    Blynk.virtualWrite(V4, humidifierActive ? 1 : 0);
  }
}

// Sync virtual states upon connecting to Blynk Cloud
BLYNK_CONNECTED() {
  Serial.println("[Blynk] Connected to Blynk Cloud successfully!");
  Blynk.syncVirtual(V5); // Sync current auto/manual mode state
}
#endif

// ==============================================================================
// 7. HELPER FUNCTIONS: WI-FI & OLED DISPLAY
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

#if ENABLE_BLYNK
    if (String(BLYNK_AUTH_TOKEN) != "YOUR_BLYNK_AUTH_TOKEN" && String(BLYNK_AUTH_TOKEN).length() > 5) {
      if (!Blynk.connected()) {
        Serial.println("[Blynk] Configuring and connecting to Blynk.Cloud...");
        Blynk.config(BLYNK_AUTH_TOKEN);
        Blynk.connect(3000); // 3 second non-blocking connection attempt
        if (Blynk.connected()) {
          Serial.println("[Blynk] Connected to Blynk IoT Cloud!");
        } else {
          Serial.println("[Blynk] Initial connection timed out. Will retry non-blockingly.");
        }
      }
    } else {
      Serial.println("[Blynk] Notice: BLYNK_AUTH_TOKEN not set. Running in local mode.");
    }
#endif
  } else {
    Serial.println("\n[Wi-Fi] Connection failed! Will retry automatically...");
  }
}

/*
  ==============================================================================
  OLED Screen Update: Displays Live Environmental Telemetry & Actuator Conditions
  ==============================================================================
  Conditions displayed:
  - Header: MUSHROOM IOT MONITOR (or [MANUAL] if remote override active)
  - Temperature (°C)
  - Humidity (%): Flags "[LOW]" if below 75% threshold
  - Air Quality: "GOOD" or "POOR" based on MQ135 threshold (1500)
  - Actuator States: Fan ON/OFF, Humidifier ON/OFF
  - Network & Cloud Status: WiFi and Blynk indicators
  ==============================================================================
*/
void updateOLED(float temp, float hum, const String& airQuality, bool fanState, bool humState, int rawAir, bool manualMode) {
#if ENABLE_OLED
  if (!oledAvailable) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // 1. Header with Mode Condition
  display.setCursor(0, 0);
  if (manualMode) {
    display.println("MUSHROOM IOT [MANUAL]");
  } else {
    display.println("MUSHROOM IOT MONITOR ");
  }
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // 2. Temperature Condition Row
  display.setCursor(0, 14);
  display.printf("Temp: %.1f C\n", temp);

  // 3. Humidity Condition Row (Flag condition if < 75%)
  display.setCursor(0, 24);
  if (hum < HUMIDITY_MIN_THRESHOLD) {
    display.printf("Hum : %.1f %% [LOW]\n", hum);
  } else {
    display.printf("Hum : %.1f %% [OK]\n", hum);
  }

  // 4. Air Quality Condition Row (Flag condition if POOR)
  display.setCursor(0, 34);
  display.printf("Air : %s (%d)\n", airQuality.c_str(), rawAir);

  // 5. Actuator States Row (Conditions: Fan & Humidifier Relay triggers)
  display.setCursor(0, 44);
  display.printf("Fan : %s | Hum: %s\n", fanState ? "ON" : "OFF", humState ? "ON" : "OFF");

  // 6. Network & Cloud Status Footer
  display.setCursor(0, 54);
#if ENABLE_BLYNK
  if (Blynk.connected()) {
    display.print("WiFi: OK | Blynk: OK");
  } else if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: OK -> :5001");
  } else {
    display.print("WiFi: Disconnected");
  }
#else
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: OK -> :5001");
  } else {
    display.print("WiFi: Disconnected");
  }
#endif

  display.display();
#endif
}

bool sendTelemetry(float temp, float hum, const String& airQuality, int rawAir, bool fanState, bool humState, bool manualMode) {
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
  payload += "\"raw_air\":" + String(rawAir) + ",";
  payload += "\"fan_status\":" + String(fanState ? "true" : "false") + ",";
  payload += "\"humidifier_status\":" + String(humState ? "true" : "false") + ",";
  payload += "\"control_mode\":\"" + String(manualMode ? "MANUAL" : "AUTO") + "\"";
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
// 8. SETUP INITIALIZATION
// ==============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==================================================");
  Serial.println("Smart Mushroom Cultivation Telemetry & Actuator Hub");
  Serial.println("With OLED Display, Blynk IoT Cloud & Local Backend");
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
  // Initialize I2C communication on explicitly defined SDA (21) & SCL (22) pins
  Wire.begin(OLED_SDA, OLED_SCL);
  Serial.printf("[Display] I2C Wire started on SDA=%d, SCL=%d\n", OLED_SDA, OLED_SCL);

  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    oledAvailable = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(10, 15);
    display.println("MUSHROOM MONITOR");
    display.drawLine(0, 26, 128, 26, SSD1306_WHITE);
    display.setCursor(10, 32);
    display.println("Blynk IoT Cloud");
    display.setCursor(10, 46);
    display.println("Initializing...");
    display.display();

    Serial.printf("[Display] SSD1306 OLED initialized at address 0x%02X\n", OLED_I2C_ADDR);
  } else {
    Serial.println("[Display] OLED not detected at 0x3C (Optional). Continuing...");
  }
#endif

  // Initial Wi-Fi & Blynk connection
  connectWiFi();
}

// ==============================================================================
// 9. MAIN CONTROL LOOP
// ==============================================================================

void loop() {
  // Ensure Wi-Fi stays connected
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

#if ENABLE_BLYNK
  if (WiFi.status() == WL_CONNECTED) {
    if (String(BLYNK_AUTH_TOKEN) != "YOUR_BLYNK_AUTH_TOKEN" && String(BLYNK_AUTH_TOKEN).length() > 5) {
      if (Blynk.connected()) {
        Blynk.run();
      } else {
        static unsigned long lastBlynkRetry = 0;
        if (millis() - lastBlynkRetry >= 10000) {
          lastBlynkRetry = millis();
          Blynk.connect(1000);
        }
      }
    }
  }
#endif

  unsigned long currentMillis = millis();

  // Run telemetry, closed-loop actuator control, and cloud sync every 3 seconds
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

    // 4. Closed-Loop Actuator Control Logic (Auto Mode vs Manual Mode)
    if (manualOverride) {
      // Manual Mode: Actuators are commanded directly via Blynk Mobile App (V3 and V4)
      fanActive = fanManualState;
      humidifierActive = humidifierManualState;
      digitalWrite(RELAY_FAN, fanActive ? RELAY_ON : RELAY_OFF);
      digitalWrite(RELAY_HUMIDIFIER, humidifierActive ? RELAY_ON : RELAY_OFF);
    } else {
      // Autonomous Mode Condition A: If air quality is POOR (MQ135 > 1500) -> Turn Fan ON
      if (airQuality == "POOR") {
        digitalWrite(RELAY_FAN, RELAY_ON);
        fanActive = true;
      } else {
        digitalWrite(RELAY_FAN, RELAY_OFF);
        fanActive = false;
      }

      // Autonomous Mode Condition B: If humidity is below 75% -> Turn Humidifier ON
      if (humidity < HUMIDITY_MIN_THRESHOLD) {
        digitalWrite(RELAY_HUMIDIFIER, RELAY_ON);
        humidifierActive = true;
      } else {
        digitalWrite(RELAY_HUMIDIFIER, RELAY_OFF);
        humidifierActive = false;
      }
    }

    Serial.printf("[Tele] Temp: %.1f C | Hum: %.1f %% | Air: %s (%d) | Fan: %s | Hum: %s | Mode: %s\n",
                  temperature, humidity, airQuality.c_str(), rawAir,
                  fanActive ? "ON" : "OFF", humidifierActive ? "ON" : "OFF",
                  manualOverride ? "MANUAL" : "AUTO");

    // 5. Update Local OLED Display with Live Data & Conditions
    updateOLED(temperature, humidity, airQuality, fanActive, humidifierActive, rawAir, manualOverride);

    // 6. Transmit Telemetry JSON to Flask Backend
    sendTelemetry(temperature, humidity, airQuality, rawAir, fanActive, humidifierActive, manualOverride);

    // 7. Push Telemetry to Blynk IoT Cloud (Virtual Pins V0 - V6)
#if ENABLE_BLYNK
    if (Blynk.connected()) {
      Blynk.virtualWrite(V0, temperature);
      Blynk.virtualWrite(V1, humidity);
      Blynk.virtualWrite(V2, airQuality);
      Blynk.virtualWrite(V3, fanActive ? 1 : 0);
      Blynk.virtualWrite(V4, humidifierActive ? 1 : 0);
      Blynk.virtualWrite(V5, manualOverride ? 1 : 0);
      Blynk.virtualWrite(V6, rawAir);
      Serial.println("[Blynk] Telemetry synced to Cloud (V0:Temp, V1:Hum, V2:Air, V3:Fan, V4:Hum, V5:Mode, V6:ADC)");
    }
#endif
  }
}
