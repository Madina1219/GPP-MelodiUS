#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <driver/i2s_std.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <FastLED.h>
#include <math.h>
#include "mqtt_secrets.h"

// ================================================================
//  GLOBAL TYPES / STRUCTS
// ================================================================
typedef struct {
  char cmd;      // 'D' or 'U'
  uint8_t idx;   // 0..7
} MqttEvent;

// ================================================================
//  GLOBAL OBJECTS
// ================================================================
WiFiClient espClient;
PubSubClient mqtt(espClient);
WebServer server(80);
Preferences prefs;
i2s_chan_handle_t tx_handle = NULL;

QueueHandle_t mqttEventQueue = NULL;

// ================================================================
//  HARDWARE CONFIG
// ================================================================
#define USE_WS2812 1

// WS2812
#if USE_WS2812
  #define FASTLED_RMT_MAX_CHANNELS 1
  #define LED_DATA_PIN    16
  #define NUM_LEDS        23
  #define LED_BRIGHTNESS  20
  CRGB leds[NUM_LEDS];

  // 8 keys, each key uses 2 LEDs
  const int KEY_LED_A[8] = {0, 3, 6, 9, 12, 15, 18, 21};
  const int KEY_LED_B[8] = {1, 4, 7, 10, 13, 16, 19, 22};
  const int GAP_LEDS[7]  = {2, 5, 8, 11, 14, 17, 20};
#endif

// MQTT
const char* MQTT_HOST = "mqtt.cetools.org";
const int   MQTT_PORT = 1884;
const char* MQTT_USER = MQTT_USERNAME;
const char* MQTT_PASS = MQTT_PASSWORD;
const char* DEVICE_ID = "B";   // change to "A" on the other board
const char* BASE      = "student/ucfnlwa/0021";

// AP config portal
const char* AP_SSID     = "ESP32-Piano";
const char* AP_PASSWORD = "12345678";

// Button / power
#define POWER_PIN      4
#define LONG_PRESS_MS  3000

// I2S audio
#define I2S_BCLK       18
#define I2S_LRC        19
#define I2S_DOUT       23
#define SAMPLE_RATE    44100
#define BUF_SAMPLES    256

// Keys
const int keyPins[8] = {17, 14, 22, 21, 13, 27, 26, 25};
const unsigned long DEBOUNCE_MS = 20;

// Musical notes C4 -> C5
const float freqs[8] = {262.0, 294.0, 330.0, 349.0, 392.0, 440.0, 494.0, 523.0};

// ================================================================
//  SYSTEM STATE
// ================================================================
String myTopic;
String otherTopic;

volatile bool localOn[8]  = {false};
volatile bool remoteOn[8] = {false};

bool rawLast[8] = {false};
bool stable[8] = {false};
unsigned long lastChangeMs[8] = {0};
float phase[8] = {0};

// Wi-Fi state
String savedSSID = "";
String savedPWD  = "";
bool   hasSavedWiFi = false;
bool   wifiConnecting = false;
unsigned long wifiConnectStart = 0;
unsigned long lastWiFiRetry = 0;

const unsigned long WIFI_CONNECT_TIMEOUT_MS = 12000;
const unsigned long WIFI_RETRY_MS = 5000;

// MQTT state
unsigned long lastMqttRetry = 0;
const unsigned long MQTT_RETRY_MS = 3000;

// Modes
bool configMode = false;

// ================================================================
//  LED ANIMATION STATE
// ================================================================
typedef enum {
  ANIM_NONE,
  ANIM_WIFI_CONNECTING,
  ANIM_CONFIG_MODE,
  ANIM_LONG_PRESS,
  ANIM_RESET_FLASH,
  ANIM_WIFI_SUCCESS,
  ANIM_WIFI_FAIL
} AnimMode;

AnimMode currentAnim = ANIM_NONE;
unsigned long animStart = 0;

// ================================================================
//  FORWARD DECLARATIONS
// ================================================================
void beginWiFiConnect();
bool loadSavedWiFi();
void connectMQTTOnce();
void startConfigMode();
void stopConfigMode();
void enterDeepSleep();
void enqueueMqttEvent(char cmd, int idx);
void clearAllNotes(bool sendRelease = true);

#if USE_WS2812
void setupLEDs();
void updateKeyLEDs();
void updateGapLEDs(unsigned long now);
void showLEDs();
void setAllLEDs(CRGB color);
#endif

// ================================================================
//  LED HELPERS
// ================================================================
#if USE_WS2812

CRGB noteColor(int i) {
  switch (i) {
    case 0: return CRGB(255, 0, 0);
    case 1: return CRGB(255, 80, 0);
    case 2: return CRGB(255, 180, 0);
    case 3: return CRGB(0, 255, 0);
    case 4: return CRGB(0, 255, 180);
    case 5: return CRGB(0, 80, 255);
    case 6: return CRGB(180, 0, 255);
    case 7: return CRGB(255, 255, 255);
  }
  return CRGB::White;
}

void setAllLEDs(CRGB color) {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = color;
}

void showLEDs() {
  FastLED.show();
}

void setupLEDs() {
  FastLED.addLeds<WS2812, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  setAllLEDs(CRGB::Black);
  showLEDs();
}

void updateKeyLEDs() {
  for (int i = 0; i < 8; i++) {
    CRGB col = (localOn[i] || remoteOn[i]) ? noteColor(i) : CRGB::Black;
    leds[KEY_LED_A[i]] = col;
    leds[KEY_LED_B[i]] = col;
  }
}

void clearGapLEDs() {
  for (int i = 0; i < 7; i++) leds[GAP_LEDS[i]] = CRGB::Black;
}

void updateGapLEDs(unsigned long now) {
  switch (currentAnim) {

    case ANIM_WIFI_CONNECTING: {
      int pos = ((now - animStart) / 120) % 7;
      for (int i = 0; i < 7; i++) {
        leds[GAP_LEDS[i]] = (i == pos) ? CRGB(0, 60, 255) : CRGB::Black;
      }
      break;
    }

    case ANIM_CONFIG_MODE: {
      float t = (float)((now - animStart) % 2000) / 2000.0f;
      float brightness = (sinf(t * 2.0f * M_PI - M_PI / 2.0f) + 1.0f) / 2.0f;
      uint8_t b = (uint8_t)(brightness * 120);
      for (int i = 0; i < 7; i++) leds[GAP_LEDS[i]] = CRGB(0, 0, b);
      break;
    }

    case ANIM_LONG_PRESS: {
      unsigned long elapsed = now - animStart;
      int filled = (int)(elapsed * 7 / LONG_PRESS_MS);
      if (filled > 7) filled = 7;
      for (int i = 0; i < 7; i++) {
        leds[GAP_LEDS[i]] = (i < filled) ? CRGB(255, 0, 0) : CRGB::Black;
      }
      break;
    }

    case ANIM_RESET_FLASH: {
      unsigned long elapsed = now - animStart;
      if (elapsed < 250) {
        setAllLEDs(CRGB(180, 180, 180));
      } else {
        currentAnim = ANIM_CONFIG_MODE;
        animStart = now;
      }
      break;
    }

    case ANIM_WIFI_SUCCESS: {
      if (now - animStart < 350) {
        for (int i = 0; i < 7; i++) leds[GAP_LEDS[i]] = CRGB(0, 255, 80);
      } else {
        currentAnim = configMode ? ANIM_CONFIG_MODE : ANIM_NONE;
        clearGapLEDs();
      }
      break;
    }

    case ANIM_WIFI_FAIL: {
      if (now - animStart < 500) {
        for (int i = 0; i < 7; i++) leds[GAP_LEDS[i]] = CRGB(255, 0, 0);
      } else {
        currentAnim = configMode ? ANIM_CONFIG_MODE : ANIM_NONE;
        clearGapLEDs();
      }
      break;
    }

    case ANIM_NONE:
    default:
      clearGapLEDs();
      break;
  }
}

#endif

// ================================================================
//  AUDIO TASK
// ================================================================
void audioTask(void* param) {
  esp_task_wdt_add(NULL);

  int16_t* buf = (int16_t*)malloc(BUF_SAMPLES * sizeof(int16_t));
  if (!buf) {
    Serial.println("[ERROR] audioTask malloc failed");
    vTaskDelete(NULL);
    return;
  }

  while (true) {
    esp_task_wdt_reset();

    for (int s = 0; s < BUF_SAMPLES; s++) {
      float mix = 0.0f;
      int cnt = 0;

      for (int i = 0; i < 8; i++) {
        if (localOn[i] || remoteOn[i]) {
          mix += sinf(phase[i]);
          phase[i] += 2.0f * (float)M_PI * freqs[i] / (float)SAMPLE_RATE;
          if (phase[i] > 2.0f * (float)M_PI) phase[i] -= 2.0f * (float)M_PI;
          cnt++;
        }
      }

      if (cnt > 0) mix /= (float)cnt;

      // Reduced amplitude for safer power draw
      buf[s] = (int16_t)(mix * 2000);
    }

    size_t written = 0;
    i2s_channel_write(tx_handle, buf, BUF_SAMPLES * sizeof(int16_t), &written, pdMS_TO_TICKS(200));
  }
}

// ================================================================
//  MQTT CALLBACK / TASK
// ================================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != otherTopic || length < 2) return;

  char cmd = (char)payload[0];
  int idx = ((char)payload[1]) - '0';

  if (idx < 0 || idx > 7) return;

  Serial.printf("[MQTT] recv %c%d\n", cmd, idx);

  if (cmd == 'D') remoteOn[idx] = true;
  else if (cmd == 'U') remoteOn[idx] = false;
}

void connectMQTTOnce() {
  if (mqtt.connected()) return;

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  Serial.print("[MQTT] Connecting...");
  String cid = String("ESP32-") + DEVICE_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  bool ok = (strlen(MQTT_USER) > 0)
              ? mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS)
              : mqtt.connect(cid.c_str());

  if (ok) {
    Serial.println(" connected!");
    bool subok = mqtt.subscribe(otherTopic.c_str());
    Serial.printf("[MQTT] subscribe %s : %s\n", otherTopic.c_str(), subok ? "OK" : "FAIL");
    for (int i = 0; i < 8; i++) remoteOn[i] = false;
  } else {
    Serial.printf(" failed (rc=%d)\n", mqtt.state());
  }
}

void enqueueMqttEvent(char cmd, int idx) {
  if (mqttEventQueue == NULL) return;

  MqttEvent evt;
  evt.cmd = cmd;
  evt.idx = idx;

  // no blocking in main loop
  xQueueSend(mqttEventQueue, &evt, 0);
}

void mqttTask(void* param) {
  MqttEvent evt;

  for (;;) {
    if (!configMode && WiFi.status() == WL_CONNECTED) {
      if (!mqtt.connected() && millis() - lastMqttRetry > MQTT_RETRY_MS) {
        lastMqttRetry = millis();
        connectMQTTOnce();
      }

      if (mqtt.connected()) {
        mqtt.loop();

        while (xQueueReceive(mqttEventQueue, &evt, 0) == pdTRUE) {
          char msg[3] = {evt.cmd, (char)('0' + evt.idx), '\0'};
          bool ok = mqtt.publish(myTopic.c_str(), msg, false);
          Serial.printf("[MQTT] %s -> %s : %s\n", myTopic.c_str(), msg, ok ? "OK" : "FAIL");
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ================================================================
//  POWER / SLEEP
// ================================================================
void clearAllNotes(bool sendRelease) {
  for (int i = 0; i < 8; i++) {
    bool wasLocal = localOn[i];
    localOn[i] = false;
    remoteOn[i] = false;
    if (sendRelease && wasLocal) {
      enqueueMqttEvent('U', i);
    }
  }
}

void enterDeepSleep() {
  Serial.println("[PWR] Entering deep sleep — press button to wake");

  clearAllNotes(true);
  delay(50);

  if (mqtt.connected()) mqtt.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

#if USE_WS2812
  setAllLEDs(CRGB::Black);
  showLEDs();
  delay(20);
#endif

  // GPIO4 is RTC-capable on ESP32 classic
  esp_sleep_enable_ext0_wakeup((gpio_num_t)POWER_PIN, 0);
  delay(100);
  esp_deep_sleep_start();
}

void checkPowerButton() {
  static unsigned long pressStart = 0;
  static bool pressing = false;
  static bool longFired = false;

  bool cur = (digitalRead(POWER_PIN) == LOW);
  unsigned long now = millis();

  if (cur && !pressing) {
    pressing = true;
    longFired = false;
    pressStart = now;
    currentAnim = ANIM_LONG_PRESS;
    animStart = now;
    return;
  }

  if (cur && pressing) {
    if (!longFired && (now - pressStart >= LONG_PRESS_MS)) {
      longFired = true;
      Serial.println("[PWR] Long press -> Config mode");
      startConfigMode();
    }
    return;
  }

  if (!cur && pressing) {
    pressing = false;
    unsigned long held = now - pressStart;

    if (held < 30) {
      currentAnim = configMode ? ANIM_CONFIG_MODE : ANIM_NONE;
      return;
    }

    if (!longFired) {
      Serial.println("[PWR] Short press -> Deep sleep");
      enterDeepSleep();
    }
  }
}

// ================================================================
//  WIFI
// ================================================================
bool loadSavedWiFi() {
  prefs.begin("cfg", true);
  savedSSID = prefs.getString("ssid", "");
  savedPWD  = prefs.getString("pwd", "");
  prefs.end();

  hasSavedWiFi = !savedSSID.isEmpty();

  if (hasSavedWiFi) {
    Serial.printf("[WiFi] Saved SSID found: %s\n", savedSSID.c_str());
  } else {
    Serial.println("[WiFi] No saved credentials");
  }

  return hasSavedWiFi;
}

void beginWiFiConnect() {
  if (!hasSavedWiFi || configMode) return;
  if (WiFi.status() == WL_CONNECTED) return;
  if (wifiConnecting) return;

  Serial.printf("[WiFi] Connecting to: %s\n", savedSSID.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPWD.c_str());

  wifiConnecting = true;
  wifiConnectStart = millis();
  lastWiFiRetry = millis();

  currentAnim = ANIM_WIFI_CONNECTING;
  animStart = millis();
}

void updateWiFiState() {
  if (configMode) return;

  if (!hasSavedWiFi) {
    startConfigMode();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (wifiConnecting) {
      wifiConnecting = false;
      esp_wifi_set_ps(WIFI_PS_NONE);  // reduce latency
      Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
      currentAnim = ANIM_WIFI_SUCCESS;
      animStart = millis();
    }
    return;
  }

  if (!wifiConnecting) {
    if (millis() - lastWiFiRetry > WIFI_RETRY_MS) {
      beginWiFiConnect();
    }
    return;
  }

  if (millis() - wifiConnectStart > WIFI_CONNECT_TIMEOUT_MS) {
    Serial.println("[WiFi] Connection timeout");
    WiFi.disconnect(true);
    wifiConnecting = false;
    currentAnim = ANIM_WIFI_FAIL;
    animStart = millis();
  }
}

// ================================================================
//  CONFIG PORTAL
// ================================================================
String buildPage(String msg = "") {
  int n = WiFi.scanNetworks();
  String options = "";

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    String lock = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "" : "&#128274; ";
    int rssi = WiFi.RSSI(i);
    String bars = rssi > -55 ? "▂▄▆█" : rssi > -70 ? "▂▄▆_" : rssi > -80 ? "▂▄__" : "▂___";
    options += "<option value='" + ssid + "'>" + lock + ssid + "  " + bars + "</option>";
  }
  WiFi.scanDelete();

  String alertHtml = "";
  if (!msg.isEmpty()) {
    bool ok = msg.startsWith("OK") || msg.startsWith("Connected");
    alertHtml = "<div class='alert " + String(ok ? "alert-ok" : "alert-err") + "'>" + msg + "</div>";
  }

  return R"(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Piano Setup</title>
<style>
  :root{--bg:#0a0a0f;--surface:#111118;--border:#222230;--accent:#7fff6e;--accent2:#6e8fff;--text:#e8e8f0;--muted:#55556a}
  *{font-family:'Courier New',Courier,monospace}
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:var(--text);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px 16px}
  .wrap{width:100%;max-width:420px}
  .header{margin-bottom:28px}
  .header .label{font-size:10px;letter-spacing:4px;text-transform:uppercase;color:var(--accent);margin-bottom:6px}
  .header h1{font-size:32px;font-weight:800;line-height:1}
  .header p{margin-top:8px;font-size:12px;color:var(--muted);line-height:1.6}
  .card{background:var(--surface);border:1px solid var(--border);border-radius:16px;padding:28px;position:relative}
  .field{margin-bottom:20px}
  .field label{display:block;font-size:10px;letter-spacing:2px;text-transform:uppercase;color:var(--muted);margin-bottom:8px}
  select,input[type=text],input[type=password]{width:100%;background:var(--bg);border:1px solid var(--border);border-radius:8px;padding:12px 14px;font-size:13px;color:var(--text)}
  button{width:100%;padding:14px;background:var(--accent);color:#0a0a0f;border:none;border-radius:8px;font-size:14px;font-weight:700;cursor:pointer;margin-top:8px}
  .alert{border-radius:8px;padding:12px 14px;font-size:12px;margin-bottom:20px;border:1px solid}
  .alert-ok{background:rgba(127,255,110,.08);border-color:rgba(127,255,110,.3);color:var(--accent)}
  .alert-err{background:rgba(255,80,80,.08);border-color:rgba(255,80,80,.3);color:#ff6060}
  .divider{display:flex;align-items:center;gap:12px;margin:20px 0;font-size:10px;letter-spacing:2px;color:var(--muted);text-transform:uppercase}
  .divider::before,.divider::after{content:'';flex:1;height:1px;background:var(--border)}
  .legend{margin-top:20px;padding-top:16px;border-top:1px solid var(--border);font-size:11px;color:var(--muted);line-height:2}
  .legend b{color:var(--text)}
</style></head>
<body><div class="wrap">
  <div class="header">
    <div class="label">ESP32 · Device )" + String(DEVICE_ID) + R"(</div>
    <h1>Piano Setup</h1>
    <p>Connect to a network to enable MQTT sync between the two devices.</p>
  </div>
  <div class="card">
    )" + alertHtml + R"(
    <form method="POST" action="/connect">
      <div class="field">
        <label>Select Network</label>
        <select name="ssid">)" + options + R"(</select>
      </div>
      <div class="divider">or enter manually</div>
      <div class="field">
        <label>Network Name (SSID)</label>
        <input type="text" name="manual_ssid" placeholder="Leave blank to use selection above">
      </div>
      <div class="field">
        <label>Password</label>
        <input type="password" name="password" placeholder="Enter WiFi password">
      </div>
      <button type="submit">Connect &amp; Save</button>
    </form>
    <div class="legend">
      <b>Short press</b> — Sleep / Wake<br>
      <b>Hold 3s</b> — Reset WiFi
    </div>
  </div>
</div></body></html>)";
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "text/html; charset=UTF-8", buildPage());
}

void handleConnect() {
  String ssid = server.arg("manual_ssid");
  if (ssid.isEmpty()) ssid = server.arg("ssid");
  String pwd = server.arg("password");

  if (ssid.isEmpty()) {
    server.send(200, "text/html; charset=UTF-8", buildPage("ERROR: Network name cannot be empty."));
    return;
  }

  server.send(200, "text/html; charset=UTF-8",
    "<html><body style='font-family:Courier New;background:#0a0a0f;color:#e8e8f0;display:flex;align-items:center;justify-content:center;height:100vh;'>"
    "<div style='text-align:center'><h2>Connecting...</h2><p>Please wait up to 15 seconds.</p></div>"
    "</body></html>"
  );

  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pwd.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    esp_wifi_set_ps(WIFI_PS_NONE);

    prefs.begin("cfg", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pwd", pwd);
    prefs.end();

    savedSSID = ssid;
    savedPWD  = pwd;
    hasSavedWiFi = true;
    wifiConnecting = false;

    stopConfigMode();

#if USE_WS2812
    setAllLEDs(CRGB::Black);
    showLEDs();
#endif
  } else {
    Serial.println("[WiFi] Connection failed");
    WiFi.disconnect();
  }
}

void startConfigMode() {
  configMode = true;
  wifiConnecting = false;

  if (mqtt.connected()) {
    clearAllNotes(true);
    mqtt.disconnect();
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.println("[CONFIG] Hotspot started");
  Serial.printf("[CONFIG] SSID: %s  Pass: %s\n", AP_SSID, AP_PASSWORD);
  Serial.println("[CONFIG] Open: http://192.168.4.1");

  server.on("/", handleRoot);
  server.on("/connect", HTTP_POST, handleConnect);
  server.begin();

  currentAnim = ANIM_RESET_FLASH;
  animStart = millis();
}

void stopConfigMode() {
  server.stop();
  configMode = false;
  WiFi.softAPdisconnect(true);
  currentAnim = ANIM_NONE;
  lastWiFiRetry = 0;
}

// ================================================================
//  I2S SETUP
// ================================================================
void setupI2S() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
  if (err != ESP_OK) {
    Serial.printf("[I2S] new_channel failed: %d\n", err);
    while (true) delay(1000);
  }

  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK,
      .ws   = (gpio_num_t)I2S_LRC,
      .dout = (gpio_num_t)I2S_DOUT,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
    },
  };

  err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
  if (err != ESP_OK) {
    Serial.printf("[I2S] init_std failed: %d\n", err);
    while (true) delay(1000);
  }

  err = i2s_channel_enable(tx_handle);
  if (err != ESP_OK) {
    Serial.printf("[I2S] enable failed: %d\n", err);
    while (true) delay(1000);
  }

  Serial.println("[I2S] Ready");
}

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
  if (wakeup == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("=== Woke from deep sleep ===");
  } else {
    Serial.println("=== ESP32 Piano Boot ===");
  }

  pinMode(POWER_PIN, INPUT_PULLUP);
  for (int i = 0; i < 8; i++) {
    pinMode(keyPins[i], INPUT_PULLUP);
    rawLast[i] = false;
    stable[i] = false;
    localOn[i] = false;
    remoteOn[i] = false;
    lastChangeMs[i] = millis();
  }

  myTopic    = String(BASE) + "/" + String(DEVICE_ID) + "/evt";
  otherTopic = String(BASE) + "/" + (String(DEVICE_ID) == "A" ? "B" : "A") + "/evt";
  Serial.printf("[MQTT] myTopic:    %s\n", myTopic.c_str());
  Serial.printf("[MQTT] otherTopic: %s\n", otherTopic.c_str());

  Serial.println("[I2S] Initialising...");
  setupI2S();

#if USE_WS2812
  Serial.println("[LED] Initialising...");
  setupLEDs();
  Serial.println("[LED] Ready");
#endif

  mqttEventQueue = xQueueCreate(32, sizeof(MqttEvent));

  Serial.println("[AUDIO] Starting task on Core 0...");
  xTaskCreatePinnedToCore(audioTask, "audioTask", 8192, NULL, 2, NULL, 0);
  Serial.println("[AUDIO] Task started");

  Serial.println("[MQTT] Starting task on Core 1...");
  xTaskCreatePinnedToCore(mqttTask, "mqttTask", 4096, NULL, 3, NULL, 1);

  loadSavedWiFi();
  if (hasSavedWiFi) {
    beginWiFiConnect();
  } else {
    startConfigMode();
  }

  if (wakeup == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("[PWR] Waiting for button release...");
    while (digitalRead(POWER_PIN) == LOW) delay(10);
    Serial.println("[PWR] Ready");
  }
}

// ================================================================
//  LOOP
// ================================================================
void loop() {
  checkPowerButton();

  if (configMode) {
    server.handleClient();

  #if USE_WS2812
    static unsigned long lastLEDcfg = 0;
    unsigned long nowCfg = millis();
    if (nowCfg - lastLEDcfg > 30) {
      lastLEDcfg = nowCfg;
      updateKeyLEDs();
      updateGapLEDs(nowCfg);
      showLEDs();
    }
  #endif
    return;
  }

  updateWiFiState();

  // Key scan
  unsigned long now = millis();
  for (int i = 0; i < 8; i++) {
    bool raw = (digitalRead(keyPins[i]) == LOW);

    if (raw != rawLast[i]) {
      rawLast[i] = raw;
      lastChangeMs[i] = now;
    }

    if ((now - lastChangeMs[i]) > DEBOUNCE_MS && stable[i] != raw) {
      stable[i] = raw;
      localOn[i] = raw;
      enqueueMqttEvent(raw ? 'D' : 'U', i);
      Serial.printf("[KEY] key%d %s (GPIO%d)\n", i + 1, raw ? "DOWN" : "UP", keyPins[i]);
    }
  }

#if USE_WS2812
  static unsigned long lastLED = 0;
  if (now - lastLED > 30) {
    lastLED = now;
    updateKeyLEDs();
    updateGapLEDs(now);
    showLEDs();
  }
#endif
}