// ChainCounter.ino - Arduino sketch for ESP32 chain meter
#include <Arduino.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include <WebSerial.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <stdarg.h>
#include <AsyncMqttClient.h>
#include "Assets.h"

#define APP_VERSION "1.0.0"

#define LED_PIN 8    // GPIO8
#define NUMPIXELS 1  // Only one LED on the board

Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_RGB + NEO_KHZ800);
AsyncMqttClient mqtt;

// controls whether debug messages should also be sent to client
bool debugToClient    = true;
bool debugToWebSerial = false;

// Max size needs to be adjusted based on how much data you send
static StaticJsonDocument<400> jsonDoc;
// Reusable output buffer
static char jsonBuf[400];

static StaticJsonDocument<400> jsonDocIn;


//=== PINS & CONSTANTS ===
const int PULSE_PIN  = 15;    // magnetic pulse (internal pull-up)
const int DIR_PIN    = 4;     // direction
const char hostname[] = "chaincounter";
const float DEFAULT_LINKS_PER_PULSE = 10.0;
const float DEFAULT_LINKS_PER_METER = 40.0;
constexpr unsigned long SAVE_INTERVAL_MS = 30000;
constexpr unsigned long INACTIVITY_MS = 2000;
constexpr uint32_t MQTT_INITIAL_BACKOFF_MS = 5000;
constexpr uint32_t MQTT_MAX_BACKOFF_MS     = 60000;

//=== GLOBAL VARIABLES ===
volatile unsigned long lastPulseMicros  = 0;
unsigned long lastValidPulseMicros      = 0;
volatile bool pulseFlag                 = false;

unsigned long lastBlinkMs = 0;
bool isBlinking = false;
bool moving = false;
const int blinkLength = 200;
const int blinkInterval = 4000;

float linksPerPulse, linksPerMeter, distancePerPulse;
float chainLength = 0.0;
float velocity    = 0.0;
bool  directionForward;
bool savedFlag;

bool   mqttEnable = false;
String mqttHost;
int    mqttPort   = 1883;
String mqttTopicRodeLength;
String mqttTopicRodeVelocity;

uint32_t mqttBackoffMs      = MQTT_INITIAL_BACKOFF_MS;
uint32_t nextMqttAttemptMs  = 0;


// timestamp of last motion detection (pulse or dir-edge)
unsigned long lastMoveTimeMs = 0;

// hysteresis for direction switching on DIR_PIN
constexpr unsigned long DIR_HYST_MS       = 1800;  // ms
constexpr unsigned long DIR_DEBOUNCE_MS   = 50;   // ms
unsigned long dirChangeStartMs = 0;
unsigned long lastDirBounceMs  = 0;
bool         lastRawDir        = false; // keeps last “debounced” value

// === SELF-CALIBRATION ===
float currentTypicalSpeed = -1.0;  // initial value
constexpr int SPEED_BUFFER_SIZE = 20;
float speedBuffer[SPEED_BUFFER_SIZE];
int speedBufferIndex = 0;
bool speedBufferFilled = false;
float lastAnomalousSpeed = NAN;

constexpr float MAX_PERCENTILE_SPAN = 0.15;   // Max allowed deviation between P70-P30
constexpr float SPEED_SAFETY_MARGIN = 0.20;   // 20% margin upward
constexpr float UPDATE_THRESHOLD     = 0.25;   // Requires at least 25% change

unsigned long lastMqttPublishMs = 0;
constexpr unsigned long MQTT_IDLE_INTERVAL_MS = 60000; // 1 minute

Preferences prefs;
AsyncWebServer server(80);
AsyncWebSocket  ws("/ws");

void sendJson(const JsonDocument& doc) {
  char buf[400];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  ws.textAll(buf, n);
}

// internal function for sending to WebSocket clients
void sendDebugMessageToClients(const char *msg) {
  if (!debugToClient) return;
  jsonDoc.clear();
  jsonDoc["debug"] = msg;
  sendJson(jsonDoc);
}

// debug output with automatic newline
void debugLog(const String &msg) {
  Serial.println(msg);
  if (debugToClient)    sendDebugMessageToClients(msg.c_str());
  if (debugToWebSerial) WebSerial.println(msg);
}

// printf-like with automatic newline
void debugLogf(const char *format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Serial.println(buf);
    if (debugToClient)    sendDebugMessageToClients(buf);
    if (debugToWebSerial) WebSerial.println(buf);
}

//=== INTERRUPT ROUTINE ===
void IRAM_ATTR pulseInterruptHandler() {
  lastPulseMicros = micros();
  pulseFlag       = true;
}

// Send JSON via WebSocket to all clients
void broadcastStatusToClients() {
  jsonDoc.clear();
  jsonDoc["version"]       = APP_VERSION;
  jsonDoc["length"]        = chainLength;
  jsonDoc["moving"]        = moving           ? 1 : 0;
  jsonDoc["velocity"]      = velocity;
  jsonDoc["direction"]     = directionForward ? 1 : 0;
  jsonDoc["linksPerRev"]   = linksPerPulse;
  jsonDoc["linksPerMeter"] = linksPerMeter;
  jsonDoc["mqttEnable"]    = mqttEnable ? 1 : 0;
  jsonDoc["mqttHost"]      = mqttHost;
  jsonDoc["mqttPort"]      = mqttPort;
  jsonDoc["mqttTopicRodeLength"]   = mqttTopicRodeLength;
  jsonDoc["mqttTopicRodeVelocity"] = mqttTopicRodeVelocity;
  sendJson(jsonDoc);
}

// Handle incoming messages from client
void processWebSocketCommand(void* arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->opcode != WS_TEXT) return;
  jsonDocIn.clear();
  auto err = deserializeJson(jsonDocIn, data, len);
  if (err) {
    debugLogf("⚠️ Json parse error: %s", err.c_str());
    return;
  }
  if (jsonDocIn.containsKey("linksPerRev")) {
    linksPerPulse = jsonDocIn["linksPerRev"].as<float>();
    prefs.putFloat("linksPerRev", linksPerPulse);
  }
  if (jsonDocIn.containsKey("linksPerMeter")) {
    linksPerMeter = jsonDocIn["linksPerMeter"].as<float>();
    prefs.putFloat("linksPerMeter", linksPerMeter);
  }
  if (jsonDocIn.containsKey("reset") && jsonDocIn["reset"].as<bool>()) {
    chainLength = 0;
    prefs.putFloat("chainLength", chainLength);
    savedFlag = true;
    debugLog("💾 Saved chainLength to flash");
  }
  if (jsonDocIn.containsKey("mqttEnable")) {
      mqttEnable = jsonDocIn["mqttEnable"].as<bool>();
      prefs.putBool("mqttEnable", mqttEnable);
  }
  if (jsonDocIn.containsKey("mqttHost")) {
      mqttHost = jsonDocIn["mqttHost"].as<const char*>();
      prefs.putString("mqttHost", mqttHost);
  }
  if (jsonDocIn.containsKey("mqttPort")) {
      mqttPort = jsonDocIn["mqttPort"].as<int>();
      prefs.putInt("mqttPort", mqttPort);
  }
  if (jsonDocIn.containsKey("mqttTopicRodeLength")) {
      mqttTopicRodeLength = jsonDocIn["mqttTopicRodeLength"].as<const char*>();
      prefs.putString("mqttTopicRodeLength", mqttTopicRodeLength);
  }
  if (jsonDocIn.containsKey("mqttTopicRodeVelocity")) {
      mqttTopicRodeVelocity = jsonDocIn["mqttTopicRodeVelocity"].as<const char*>();
      prefs.putString("mqttTopicRodeVelocity", mqttTopicRodeVelocity);
  }
  broadcastStatusToClients();
}

// WebSocket event handler
void onWebSocketEvent(AsyncWebSocket *server,
               AsyncWebSocketClient *client,
               AwsEventType type,
               void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    jsonDoc.clear();
    jsonDoc["version"]       = APP_VERSION;
    jsonDoc["linksPerRev"]   = linksPerPulse;
    jsonDoc["linksPerMeter"] = linksPerMeter;
    jsonDoc["length"]        = chainLength;
    jsonDoc["velocity"]      = velocity;
    jsonDoc["moving"]        = moving           ? 1 : 0;
    jsonDoc["direction"]     = directionForward ? 1 : 0;
    jsonDoc["mqttEnable"]           = mqttEnable ? 1 : 0;
    jsonDoc["mqttHost"]             = mqttHost;
    jsonDoc["mqttPort"]             = mqttPort;
    jsonDoc["mqttTopicRodeLength"]  = mqttTopicRodeLength;
    jsonDoc["mqttTopicRodeVelocity"]= mqttTopicRodeVelocity;
    size_t len2 = serializeJson(jsonDoc, jsonBuf, sizeof(jsonBuf));
    client->text(jsonBuf, len2);
    sendDebugMessageToClients("🌐 Client connected, debug starts here");
  }
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_TEXT) {
      // parse incoming JSON
      jsonDocIn.clear();
      auto err = deserializeJson(jsonDocIn, data, len);
      if (!err && jsonDocIn.containsKey("ping")) {
        // echo pong back to just that client
        client->text("{\"pong\":true}");
        return; // skip other data handling
      }
    }
    // if it wasn't ping, run ordinary message logic
    processWebSocketCommand(arg, data, len);
  }
}

void scheduleNextMqttAttempt(uint32_t now) {
  // Double backoff until max level
  mqttBackoffMs = min(mqttBackoffMs * 2, MQTT_MAX_BACKOFF_MS);
  nextMqttAttemptMs = now + mqttBackoffMs;
  debugLogf("⏱️ Scheduling next MQTT attempt in %lus", mqttBackoffMs / 1000);
}

void configureMqttClient() {
    mqtt.setClientId("chaincounter");

    mqtt.onConnect([](bool session){
        debugLog("✅ MQTT connected");
        mqttBackoffMs = MQTT_INITIAL_BACKOFF_MS;
    });

    mqtt.onDisconnect([](AsyncMqttClientDisconnectReason reason){
        if (reason == AsyncMqttClientDisconnectReason::TCP_DISCONNECTED) {
            debugLogf("❌ No MQTT broker at %s:%d — will try again later", mqttHost.c_str(), mqttPort);
        }
        else {
            debugLogf("❌️ MQTT disconnected (reason=%d) — giving up", int(reason));
        }
        scheduleNextMqttAttempt(millis());
    });

    mqtt.onPublish([](uint16_t packetId) {
        debugLogf("📤 MQTT publish confirmed (packetId=%d)", packetId);
    });
}

void ensureMqttConnection() {
  uint32_t now = millis();

  // 1) If MQTT is already on or connected → reset backoff and return
  if (!mqttEnable || mqtt.connected()) {
    mqttBackoffMs     = MQTT_INITIAL_BACKOFF_MS;
    nextMqttAttemptMs = 0;
    return;
  }

  // 2) If it's not time for next attempt → skip
  if (now < nextMqttAttemptMs) {
    return;
  }

  // 3) IP or DNS resolution
  IPAddress mqttIP;
  if (!mqttIP.fromString(mqttHost)) {
    if (!WiFi.hostByName(mqttHost.c_str(), mqttIP)) {
      debugLogf("❌ Cannot resolve MQTT host: %s", mqttHost.c_str());
      scheduleNextMqttAttempt(now);
      return;
    }
  }

  // 4) Synchronous TCP-port-test
  {
    WiFiClient testClient;
    if (!testClient.connect(mqttIP, mqttPort)) {
      debugLogf("❌ No MQTT broker at %s:%d", mqttHost.c_str(), mqttPort);
      scheduleNextMqttAttempt(now);
      return;
    }
    testClient.stop();
  }

  // 5) Run asynchronous connection
  mqtt.setServer(mqttIP, mqttPort);
  debugLogf("🔌 Trying to connect to MQTT %s (%s:%d)…",
            mqttHost.c_str(),
            mqttIP.toString().c_str(),
            mqttPort);
  mqtt.connect();

  // 6) Successfully initiated connection attempt → reset backoff
  mqttBackoffMs     = MQTT_INITIAL_BACKOFF_MS;
  nextMqttAttemptMs = 0;
}

void publishChainMetrics() {
    if (!mqttEnable) {
        debugLog("⚠️ MQTT not enabled");
        return;
    }
    if (!mqtt.connected()) {
        debugLog("⚠️ mqtt not connected, skips publish");
        return;
    }
    if (mqttTopicRodeLength.isEmpty() || mqttTopicRodeVelocity.isEmpty()) {
        debugLog("⚠️ MQTT topic missing, aborting publish");
        return;
    }

    char buf1[16], buf2[16];
    snprintf(buf1, sizeof(buf1), "%.2f", chainLength);
    snprintf(buf2, sizeof(buf2), "%.2f", velocity);

    bool ok1 = mqtt.publish(mqttTopicRodeLength.c_str(), 0, false, buf1);
    bool ok2 = mqtt.publish(mqttTopicRodeVelocity.c_str(), 0, false, buf2);

    if(!ok1) debugLogf("❌ MQTT publish: %s = %s (%s)", mqttTopicRodeLength.c_str(), buf1, ok1 ? "OK" : "FAILED");
    if(!ok2) debugLogf("❌ MQTT publish: %s = %s (%s)", mqttTopicRodeVelocity.c_str(), buf2, ok2 ? "OK" : "FAILED");
}

void addSpeedToStatsBuffer(float v) {
  speedBuffer[speedBufferIndex] = v;
  speedBufferIndex = (speedBufferIndex + 1) % SPEED_BUFFER_SIZE;
  if (speedBufferIndex == 0) speedBufferFilled = true;
}

float getPercentile(float p) {
  int count = speedBufferFilled ? SPEED_BUFFER_SIZE : speedBufferIndex;
  if (count == 0) return NAN;

  float sorted[count];
  memcpy(sorted, speedBuffer, count * sizeof(float));
  std::sort(sorted, sorted + count);

  int idx = round(p * count);
  return sorted[min(idx, count - 1)];
}

void debounceAndUpdateDirection() {
  unsigned long nowMs = millis();
  // 1) Debounce
  bool rawSample = digitalRead(DIR_PIN);
  if (rawSample != lastRawDir) {
    if (nowMs - lastDirBounceMs < DIR_DEBOUNCE_MS) {
      // bounce: keep old value
      rawSample = lastRawDir;
    } else {
      // stable
      lastRawDir      = rawSample;
      lastDirBounceMs = nowMs;
    }
  }
  bool rawDir = rawSample;

  // 2) Hysteresis
  if (rawDir != directionForward) {
    // FALLING EDGE: HIGH→LOW → delay
    if (!rawDir && directionForward) {
      if (dirChangeStartMs == 0) {
        dirChangeStartMs = nowMs;  // start timer
      }
      else if (nowMs - dirChangeStartMs >= DIR_HYST_MS) {
        directionForward = rawDir;
        dirChangeStartMs = 0;
      }
    }
    // RISING EDGE: LOW→HIGH → immediate
    else if (rawDir && !directionForward) {
      directionForward = rawDir;
      dirChangeStartMs = 0;
    }
  }
  else {
    // back before hysteresis → reset timer
    dirChangeStartMs = 0;
  }
}

void handlePulse() {
  noInterrupts();
    unsigned long nowMicros = lastPulseMicros;
    pulseFlag = false;
  interrupts();

  distancePerPulse = (linksPerMeter > 0) ? linksPerPulse / linksPerMeter : DEFAULT_LINKS_PER_PULSE / DEFAULT_LINKS_PER_METER;

  float minValidPulseMs = (currentTypicalSpeed > 0) ? (distancePerPulse / currentTypicalSpeed) * 1000.0 : -1.0;

  unsigned long dtUs = nowMicros - lastValidPulseMicros;
  float dtMs = dtUs / 1000.0;

  if (dtMs < 50) {
    debugLogf("❌ Filtering out noise/bounce %.2f ms", dtMs);
    return;
  }

  // Calculate velocity
  float dtSec = dtMs / 1000.0;
  velocity = (dtSec > 0) ? distancePerPulse / dtSec : 0.0;

  // Statistics update – even if pulse might be ignored
  addSpeedToStatsBuffer(velocity);

  // Filter out too short/long pulse intervals
  if (dtMs < minValidPulseMs) {
    debugLogf("⚠️ Ignoring pulse interval (%.2f ms < %.2f ms, corresponds to >%.2f m/s)",
            dtMs, minValidPulseMs, velocity);
    return;
  }

  // Accepted pulse → advance "last accepted"
  lastValidPulseMicros = nowMicros;

  // Update length
  chainLength += (directionForward ? distancePerPulse : -distancePerPulse);

  // Mark movement and save
  moving = true;
  lastMoveTimeMs = millis();
  savedFlag = false;

  if(directionForward) {
    debugLogf("Direction 🟢 down %.2f m/s length %.2f m, pulse interval %.2f ms", velocity, chainLength, dtMs);
  }
  else {
    debugLogf("Direction 🔴 up %.2f m/s length %.2f m, pulse interval %.2f ms", velocity, chainLength, dtMs);
  }

  // Send status to WebSocket
  jsonDoc.clear();
  jsonDoc["length"]    = chainLength;
  jsonDoc["velocity"]  = velocity;
  jsonDoc["moving"]    = 1;
  jsonDoc["direction"] = directionForward ? 1 : 0;
  sendJson(jsonDoc);

  // Send to MQTT
  publishChainMetrics();
}

void setup() {
  Serial.begin(115200);
  delay(500);  // Give time for Serial to start
  debugLogf("ChainCounter version: %s", APP_VERSION);

  pinMode(DIR_PIN, INPUT);
  pinMode(PULSE_PIN, INPUT_PULLUP);
  // magnetic sensor pulls PULSE_PIN low when magnet arrives, trigger on FALLING
  attachInterrupt(digitalPinToInterrupt(PULSE_PIN),
                  pulseInterruptHandler,
                  FALLING);
  lastRawDir     = digitalRead(DIR_PIN);
  directionForward = lastRawDir;
  lastMoveTimeMs = millis();
  lastPulseMicros      = micros();
  lastValidPulseMicros = lastPulseMicros;

  // Read saved settings
  prefs.begin("chain", false);
  linksPerPulse = prefs.getFloat("linksPerRev",  DEFAULT_LINKS_PER_PULSE);
  linksPerMeter = prefs.getFloat("linksPerMeter",DEFAULT_LINKS_PER_METER);
  chainLength   = prefs.getFloat("chainLength",  0.0);
  if (!isfinite(chainLength)) {
    debugLog("⚠️ Invalid chainLength in prefs – resetting");
    chainLength = 0.0;
  }
  mqttEnable    = prefs.getBool  ("mqttEnable", false);
  mqttHost      = prefs.getString("mqttHost", "");
  mqttPort      = prefs.getInt   ("mqttPort", 1883);
  mqttTopicRodeLength    = prefs.getString("mqttTopicRodeLength",   "vessels/self/navigation/anchor/rodeLength");
  mqttTopicRodeVelocity  = prefs.getString("mqttTopicRodeVelocity", "vessels/self/navigation/anchor/rodeVelocity");

  float typicalSpeed = prefs.getFloat("typicalMaxSpeed", -1.0);
  if (typicalSpeed > 0) {
    debugLogf("📌 Saved normal max speed: %.2f m/s", typicalSpeed);
  } else {
    debugLog("📌 No saved max speed – system in learning mode");
  }

  savedFlag = true;

  // WiFiManager with captive portal
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("ChainCounterAP")) {
    debugLog("❌ WiFi connect failed, rebooting...");
    delay(3000);
    ESP.restart();
  }
  debugLogf("✅ Connected, IP: %s", WiFi.localIP().toString().c_str());

  ArduinoOTA.onStart([]() {
    debugLog("OTA: starting upload");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned int lastPercent = 0;                  // remembers last logged percent
    unsigned int percent = (progress * 100) / total;      // calculate current percent

    // only log when percent is a multiple of 5 _and_ differs from last logged
    if (percent % 5 == 0 && percent != lastPercent) {
      lastPercent = percent;
      debugLogf("OTA: %u%%", percent);
    }
  });
  ArduinoOTA.onEnd([]() {
    debugLog("OTA: done!");
    WebSerial.println("OTA: done!");
    WebSerial.loop();
    delay(500);
    ESP.restart();
  });
  ArduinoOTA.onError([](ota_error_t err) {
    debugLogf("OTA: ERROR[%u]", err);
  });
  ArduinoOTA.setPassword("ota");
  ArduinoOTA.begin();   // starts OTA service

  debugLogf("OTA ready on IP %s", WiFi.localIP().toString().c_str());

  if (!MDNS.begin(hostname)) {
    debugLog("❌ mDNS responder failed");
  } else {
    debugLogf("✅ mDNS responder started: %s.local", hostname);
    MDNS.addService("http", "tcp", 80);
  }

  // Webserver and WebSocket
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/css", style_css);
  });
  server.begin();
  // http://<IP>/webserial
  WebSerial.begin(&server);
  WebSerial.onMessage([](uint8_t *data, size_t len) {
    // Build up a String from incoming bytes
    String cmd;
    for (size_t i = 0; i < len; i++) {
      cmd += char(data[i]);
    }
    cmd.trim();  // remove \r\n at the end
    cmd.toLowerCase();

    if (cmd == "restart")  {
      WebSerial.println("Restart");
      WebSerial.loop();
      delay(500);
      ESP.restart();
    }
    else if (cmd == "ip")  {
      WebSerial.println(WiFi.localIP().toString().c_str());
    } else if (cmd == "uptime")  {
      unsigned long uptimeMillis = millis();
      unsigned long seconds = uptimeMillis / 1000;
      unsigned long minutes = seconds / 60;
      unsigned long hours = minutes / 60;
      char buf[64];
      snprintf(buf, sizeof(buf),
               "Uptime: %lu hours, %lu minutes, %lu seconds",
               hours, minutes % 60, seconds % 60);
      WebSerial.println(buf);
    }
    else if (cmd == "mq")  {
      if(!mqttEnable) {
        WebSerial.println("MQTT not enabled");
      } else {
        publishChainMetrics();
        WebSerial.println("Sent to MQTT");
      }
    }
    else {
      char buf[64];
      snprintf(buf, sizeof(buf), "Unknown command: %s", cmd.c_str());
      WebSerial.println(buf);
    }
  });
  debugLogf("WebSerial remote console http://%s.local/webserial", hostname);
  configureMqttClient();
}

void loop() {
  // ➕ Update direction first
  debounceAndUpdateDirection();

  ArduinoOTA.handle();
  WebSerial.loop();

  // 1) Handle pulse intervals
  if (pulseFlag) handlePulse();

  // 2) Inactivity handling
  if (moving && millis() - lastMoveTimeMs > INACTIVITY_MS) {
      debugLog("Motion 🟨 stopped");
      moving   = false;
      velocity = 0;
      if (chainLength < 0) {
        chainLength = 0;
      }

      // send status to clients
      jsonDoc.clear();
      jsonDoc["length"]   = chainLength;
      jsonDoc["velocity"] = 0;
      jsonDoc["moving"]   = 0;
      sendJson(jsonDoc);

      // reset timer so block only runs once per inactivity
      lastMoveTimeMs = millis();
  }

  // calculate max speed from latest statistics
  if (speedBufferFilled) {
    float median = getPercentile(0.5);
    float p30    = getPercentile(0.3);
    float p70    = getPercentile(0.7);
    float span   = p70 - p30;

    if (span <= MAX_PERCENTILE_SPAN) {
      float newTypicalSpeed = median * (1.0 + SPEED_SAFETY_MARGIN);
      if (currentTypicalSpeed < 0 || fabs(newTypicalSpeed - currentTypicalSpeed) / currentTypicalSpeed > UPDATE_THRESHOLD) {
        currentTypicalSpeed = newTypicalSpeed;
        debugLogf("📈 Updating internal typicalSpeed to %.2f m/s", currentTypicalSpeed);
      }
    }
  }

  // Periodic MQTT publish during idle
  if (!moving && mqttEnable && millis() - lastMqttPublishMs >= MQTT_IDLE_INTERVAL_MS) {
    debugLog("📤 Periodic MQTT update during idle");
    publishChainMetrics();
    lastMqttPublishMs = millis();
  }

  // Save chain length and typical speed periodically
  if (!savedFlag && millis() - lastMoveTimeMs >= SAVE_INTERVAL_MS) {
    prefs.putFloat("chainLength", chainLength);
    savedFlag = true;
    debugLog("💾 Saved chainLength to flash");

    if (currentTypicalSpeed > 0) {
      float savedSpeed = prefs.getFloat("typicalMaxSpeed", -1.0);
      if (savedSpeed < 0 || fabs(currentTypicalSpeed - savedSpeed) / savedSpeed > UPDATE_THRESHOLD) {
        prefs.putFloat("typicalMaxSpeed", currentTypicalSpeed);
        debugLogf("💾 Saving typicalSpeed %.2f m/s to flash", currentTypicalSpeed);
      }
    }
  }

  // 4) WiFi / MQTT keep-alive
  ensureMqttConnection();

  // 5) LED blink or solid color
  unsigned long now = millis();
  if (moving) {
    // Movement – solid color
    if (directionForward) {
      pixels.setPixelColor(0, pixels.Color(0, 255, 0));  // Green
    } else {
      pixels.setPixelColor(0, pixels.Color(255, 0, 0));  // Red
    }
    pixels.show();
  } else {
    // Stationary – blink occasionally
    if (!isBlinking && now - lastBlinkMs >= (blinkInterval - blinkLength)) {
      isBlinking = true;
      lastBlinkMs = now;

      if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        pixels.setPixelColor(0, pixels.Color(0, 255, 0));  // Green (AP mode)
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 255));  // Blue
      }

      pixels.show();
    }

    // Turn off LED 300 ms after blink start
    if (isBlinking && now - lastBlinkMs >= blinkLength) {
      pixels.clear();
      pixels.show();
      isBlinking = false;
    }
  }
}
