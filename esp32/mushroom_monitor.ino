/*
  ==============================================================================
  Final-Year B.E. Electronics and Communication Engineering Project:
  "IoT and AI Based Smart Mushroom Cultivation System for Contamination Detection and Yield Prediction"
  
  Module 1: ESP32 IoT Hardware Telemetry & Automated Actuator Controller
  Includes: DHT11 Sensor, MQ135 Air Quality Sensor, 2-Channel Relay Module,
            SSD1306 I2C OLED Display, Flask REST API & Blynk IoT Cloud
  ==============================================================================

  Hardware Pin Mapping (ESP32 DevKit):
  - DHT11 Sensor    : Data Pin          -> GPIO 5
  - MQ135 Sensor    : Digital Output DO -> GPIO 34 (P34 Digital Air Quality)
  - Relay Channel 1 : Exhaust Fan       -> GPIO 26
  - Relay Channel 2 : Humidifier        -> GPIO 27
  - SSD1306 OLED    : I2C Data (SDA)    -> GPIO 21
  - SSD1306 OLED    : I2C Clock (SCL)   -> GPIO 22
  ==============================================================================
*/

// ==============================================================================
// 1. BLYNK IOT 2.0 CONFIGURATION (Must be at the very top of file)
// ==============================================================================
#define BLYNK_TEMPLATE_ID   "TMPLxxxxxx"            // Replace with your Template ID from blynk.cloud
#define BLYNK_TEMPLATE_NAME "Mushroom Monitor"     // Replace with your Template Name
#define BLYNK_AUTH_TOKEN    "YOUR_BLYNK_AUTH_TOKEN" // Replace with your Device Auth Token
#define BLYNK_PRINT Serial

// Set to 1 to enable Blynk IoT Cloud; Set to 0 to run purely with local backend
#define ENABLE_BLYNK 1

#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#if ENABLE_BLYNK
  #include <WiFiClient.h>
  #include <BlynkSimpleEsp32.h>
#endif

// ==============================================================================
// 2. HARDWARE PIN DEFINITIONS
// ==============================================================================
#define DHTPIN 5                 // P5 for data of temperature & humidity sensor
#define DHTTYPE DHT11

#define MQ135_PIN 34             // P34 for digital air quality pin
#define RELAY_FAN 26             // GPIO 26 for Exhaust Fan relay
#define RELAY_HUMIDIFIER 27      // GPIO 27 for Ultrasonic Humidifier relay

#define SDA_PIN 21               // GPIO 21 for OLED SDA / SCA
#define SCL_PIN 22               // GPIO 22 for OLED SCL

// Relay trigger logic:
// Most standard Arduino relay modules trigger on HIGH or LOW.
// Set RELAY_ON to HIGH or LOW based on your relay module:
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// Climate thresholds for Oyster / Button mushroom cultivation:
const float HUMIDITY_MIN_THRESHOLD = 75.0; // Humidifier ON if humidity < 75%

// ==============================================================================
// 3. WI-FI & FLASK BACKEND CONFIGURATION
// ==============================================================================
const char *ssid = "Galaxy A16 5G B9D4";
const char *password = "luna1234";

// Target Flask Backend Telemetry Endpoint (update to your computer's IP address)
static const char *SERVER_URL = "http://10.112.173.25:5001/api/sensor-data";

// Telemetry interval (send reading every ~3 seconds)
const unsigned long TELEMETRY_INTERVAL_MS = 3000;

// ==============================================================================
// 4. OLED DISPLAY DEFINITIONS
// ==============================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledOK = false;

DHT dht(DHTPIN, DHTTYPE);
unsigned long lastTelemetryTime = 0;

// Actuator and Remote Control states
bool manualOverride = false;        // 0 = Autonomous Thresholds, 1 = Manual Mode from Blynk
bool fanManualState = false;        // Commanded Fan state from Blynk App V3
bool humidifierManualState = false; // Commanded Humidifier state from Blynk App V4
bool fanActive = false;             // Real-world Fan relay state
bool humidifierActive = false;      // Real-world Humidifier relay state

// ==============================================================================
// 5. BLYNK VIRTUAL PIN HANDLERS (Remote App Control)
// ==============================================================================
#if ENABLE_BLYNK
// V5: Mode Selection (0 = Auto Mode, 1 = Manual Mode)
BLYNK_WRITE(V5) {
  manualOverride = (param.asInt() == 1);
  Serial.printf("[Blynk] Mode: %s\n", manualOverride ? "MANUAL OVERRIDE" : "AUTONOMOUS CLOSED-LOOP");
}

// V3: Fan Relay Switch Widget
BLYNK_WRITE(V3) {
  int val = param.asInt();
  if (manualOverride) {
    fanManualState = (val == 1);
    digitalWrite(RELAY_FAN, fanManualState ? RELAY_ON : RELAY_OFF);
    fanActive = fanManualState;
    Serial.printf("[Blynk] Manual Fan -> %s\n", fanManualState ? "ON" : "OFF");
  } else {
    // Reflect back current automated status
    Blynk.virtualWrite(V3, fanActive ? 1 : 0);
  }
}

// V4: Humidifier Relay Switch Widget
BLYNK_WRITE(V4) {
  int val = param.asInt();
  if (manualOverride) {
    humidifierManualState = (val == 1);
    digitalWrite(RELAY_HUMIDIFIER, humidifierManualState ? RELAY_ON : RELAY_OFF);
    humidifierActive = humidifierManualState;
    Serial.printf("[Blynk] Manual Humidifier -> %s\n", humidifierManualState ? "ON" : "OFF");
  } else {
    Blynk.virtualWrite(V4, humidifierActive ? 1 : 0);
  }
}

BLYNK_CONNECTED() {
  Serial.println("[Blynk] Connected to Blynk IoT Cloud!");
  Blynk.syncVirtual(V5);
}
#endif

// ==============================================================================
// 6. HELPER: TRANSMIT TELEMETRY TO FLASK BACKEND
// ==============================================================================
void sendSensorData(float temp, float hum, int airQuality, bool fanState, bool humState)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[HTTP] WiFi not connected - skipping HTTP send");
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(2500);

  float safeTemp = isnan(temp) ? 0.0 : temp;
  float safeHum = isnan(hum) ? 0.0 : hum;
  String airStr = (airQuality == HIGH) ? "POOR" : "OK";

  // Build JSON payload compatible with both legacy and new backend schemas
  String payload = "{";
  payload += "\"temperature\":" + String(safeTemp, 1) + ",";
  payload += "\"humidity\":" + String(safeHum, 1) + ",";
  payload += "\"air\":" + String(airQuality) + ",";
  payload += "\"air_quality\":\"" + (airQuality == HIGH ? String("POOR") : String("GOOD")) + "\",";
  payload += "\"fan_status\":" + String(fanState ? "true" : "false") + ",";
  payload += "\"humidifier_status\":" + String(humState ? "true" : "false") + ",";
  payload += "\"control_mode\":\"" + String(manualOverride ? "MANUAL" : "AUTO") + "\"";
  payload += "}";

  int httpResponseCode = http.POST(payload);
  Serial.print("[HTTP POST] Response: ");
  Serial.println(httpResponseCode);
  http.end();
}

// ==============================================================================
// 7. HELPER: OLED SCREEN UPDATE WITH LIVE FAN & HUMIDIFIER CONDITIONS
// ==============================================================================
void updateOLEDDisplay(float temp, float hum, int airQuality, bool fanState, bool humState)
{
  if (!oledOK) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // 1. Header with Mode indicator
  display.setCursor(0, 0);
  if (manualOverride) {
    display.println("Mushroom [MANUAL]");
  } else {
    display.println("Mushroom Monitor");
  }
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE); // Visible divider line

  // 2. Temperature Readout
  display.setCursor(0, 15);
  display.print("Temp: ");
  if (isnan(temp)) {
    display.println("--.- C");
  } else {
    display.print(temp, 1);
    display.println(" C");
  }

  // 3. Humidity Readout with [LOW] alert indicator
  display.setCursor(0, 26);
  display.print("Hum : ");
  if (isnan(hum)) {
    display.println("--.- %");
  } else {
    display.print(hum, 1);
    display.print(" %");
    if (hum < HUMIDITY_MIN_THRESHOLD) {
      display.println(" [LOW]");
    } else {
      display.println(" [OK]");
    }
  }

  // 4. Air Quality Condition (P34 DO Sensor)
  display.setCursor(0, 37);
  display.print("Air : ");
  display.println(airQuality == HIGH ? "POOR (High Gas)" : "OK (Clean)");

  // 5. Live Actuator States (Fan & Humidifier Outputs requested)
  display.setCursor(0, 48);
  display.printf("Fan : %s | Hum: %s\n", fanState ? "ON" : "OFF", humState ? "ON" : "OFF");

  // 6. Network & Cloud Status Footer
  display.setCursor(0, 57);
#if ENABLE_BLYNK
  if (Blynk.connected()) {
    display.print("WiFi: OK | Blynk: OK");
  } else if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: OK -> Backend");
  } else {
    display.print("WiFi: Disconnected");
  }
#else
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: OK -> Backend");
  } else {
    display.print("WiFi: Disconnected");
  }
#endif

  display.display();
}

// ==============================================================================
// 8. SETUP INITIALIZATION
// ==============================================================================
void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==================================================");
  Serial.println("Smart Mushroom Cultivation Telemetry & Actuator Hub");
  Serial.println("ESP32 DevKit • OLED • Blynk IoT • Flask Backend");
  Serial.println("==================================================");

  // 1. Initialize DHT11 Sensor (GPIO 5)
  dht.begin();
  delay(1000);
  Serial.println("[Sensors] DHT11 initialized on GPIO 5");

  // 2. Initialize MQ135 Air Quality Sensor (GPIO 34)
  pinMode(MQ135_PIN, INPUT);
  Serial.println("[Sensors] MQ135 Digital Input initialized on GPIO 34");

  // 3. Initialize Relays (Fan on GPIO 26, Humidifier on GPIO 27)
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(RELAY_HUMIDIFIER, OUTPUT);
  digitalWrite(RELAY_FAN, RELAY_OFF);
  digitalWrite(RELAY_HUMIDIFIER, RELAY_OFF);
  Serial.println("[Actuators] Relays initialized (Fan: GPIO 26, Humidifier: GPIO 27)");

  // 4. Initialize I2C and OLED Display (SDA=21, SCL=22)
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000); // 100 kHz I2C Clock

  oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledOK) {
    // Try alternate I2C address 0x3D
    oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  }

  if (!oledOK)
  {
    Serial.println("[OLED] Display not found on 0x3C or 0x3D. Continuing...");
  }
  else
  {
    Serial.println("[OLED] Display initialized successfully!");
    // Show the large "READY" banner for 2 seconds
    display.clearDisplay();
    display.setTextSize(3);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(18, 20);
    display.println("READY");
    display.display();
    delay(2000);
  }

  // 5. Connect to Wi-Fi Network
  WiFi.begin(ssid, password);
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n[WiFi] Connected successfully!");
    Serial.print("[WiFi] IP address: ");
    Serial.println(WiFi.localIP());

#if ENABLE_BLYNK
    if (String(BLYNK_AUTH_TOKEN) != "YOUR_BLYNK_AUTH_TOKEN" && String(BLYNK_AUTH_TOKEN).length() > 5) {
      Serial.println("[Blynk] Connecting to Blynk IoT Cloud...");
      Blynk.config(BLYNK_AUTH_TOKEN);
      Blynk.connect(3000); // 3 second connection attempt
      if (Blynk.connected()) {
        Serial.println("[Blynk] Connected to Blynk Cloud!");
      } else {
        Serial.println("[Blynk] Will retry connecting in background.");
      }
    } else {
      Serial.println("[Blynk] Notice: BLYNK_AUTH_TOKEN not set yet. Running in local mode.");
    }
#endif
  }
  else
  {
    Serial.println("\n[WiFi] Connection timed out - operating in standalone offline mode");
  }
}

// ==============================================================================
// 9. MAIN CONTROL LOOP
// ==============================================================================
void loop()
{
  // 1. Maintain background Blynk & Wi-Fi tasks
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

  // Run sensor acquisition and closed-loop control every 3 seconds
  if (currentMillis - lastTelemetryTime >= TELEMETRY_INTERVAL_MS)
  {
    lastTelemetryTime = currentMillis;

    // A. Read Sensors
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    int airQuality = digitalRead(MQ135_PIN); // HIGH = POOR air, LOW = OK

    Serial.print("[Tele] Temp: ");
    Serial.print(temp);
    Serial.print(" C, Hum: ");
    Serial.print(hum);
    Serial.print(" %, Air: ");
    Serial.print(airQuality == HIGH ? "POOR" : "OK");

    // B. Actuator Control Logic (Auto Mode vs Manual Mode)
    if (manualOverride) {
      // Manual Mode: Actuator relays are directly driven by Blynk switches
      fanActive = fanManualState;
      humidifierActive = humidifierManualState;
      digitalWrite(RELAY_FAN, fanActive ? RELAY_ON : RELAY_OFF);
      digitalWrite(RELAY_HUMIDIFIER, humidifierActive ? RELAY_ON : RELAY_OFF);
    } else {
      // Autonomous Condition 1: FAN driven via MQ135 (POOR -> Fan ON)
      if (airQuality == HIGH) {
        digitalWrite(RELAY_FAN, RELAY_ON);
        fanActive = true;
      } else {
        digitalWrite(RELAY_FAN, RELAY_OFF);
        fanActive = false;
      }

      // Autonomous Condition 2: HUMIDIFIER driven via DHT11 (Humidity < 75% -> Humidifier ON)
      if (!isnan(hum)) {
        if (hum < HUMIDITY_MIN_THRESHOLD) {
          digitalWrite(RELAY_HUMIDIFIER, RELAY_ON);
          humidifierActive = true;
        } else {
          digitalWrite(RELAY_HUMIDIFIER, RELAY_OFF);
          humidifierActive = false;
        }
      }
    }

    Serial.printf(" | Fan: %s | Hum: %s | Mode: %s\n",
                  fanActive ? "ON" : "OFF", humidifierActive ? "ON" : "OFF",
                  manualOverride ? "MANUAL" : "AUTO");

    // C. OLED Display: Redraw every loop with live conditions & fan/humidifier status
    updateOLEDDisplay(temp, hum, airQuality, fanActive, humidifierActive);

    // D. Transmit Telemetry JSON to Flask Backend
    sendSensorData(temp, hum, airQuality, fanActive, humidifierActive);

    // E. Sync Virtual Pins with Blynk IoT Cloud
#if ENABLE_BLYNK
    if (Blynk.connected()) {
      if (!isnan(temp)) Blynk.virtualWrite(V0, temp);
      if (!isnan(hum)) Blynk.virtualWrite(V1, hum);
      Blynk.virtualWrite(V2, airQuality == HIGH ? "POOR" : "OK");
      Blynk.virtualWrite(V3, fanActive ? 1 : 0);
      Blynk.virtualWrite(V4, humidifierActive ? 1 : 0);
      Blynk.virtualWrite(V5, manualOverride ? 1 : 0);
      Blynk.virtualWrite(V6, airQuality);
    }
#endif
  }
}
