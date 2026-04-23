#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <driver/i2s_std.h>
#include <math.h>
#include "mqtt_secrets.h"
#include <FastLED.h>

i2s_chan_handle_t tx_handle = NULL;

// =================== WS2812 LEDs ===================
#define USE_WS2812 1
#if USE_WS2812
#define FASTLED_RMT_MAX_CHANNELS 1
#define LED_DATA_PIN 4
#define NUM_LEDS 23
#define LED_BRIGHTNESS 20
CRGB leds[NUM_LEDS];

// Layout: [k,k,gap, k,k,gap, ...] x8 keys
const int KEY_LED_A[8] = { 0, 3, 6, 9, 12, 15, 18, 21 };
const int KEY_LED_B[8] = { 1, 4, 7, 10, 13, 16, 19, 22 };
const int GAP_LEDS[7] = { 2, 5, 8, 11, 14, 17, 20 };
#endif

// =================== MQTT ===================
const char* MQTT_HOST = "mqtt.cetools.org";
const int MQTT_PORT = 1884;
const char* MQTT_USER = MQTT_USERNAME;
const char* MQTT_PASS = MQTT_PASSWORD;
const char* DEVICE_ID = "A";  // Change to "B" on the other board
const char* BASE = "student/ucfnlwa/0021";

// =================== AP Hotspot ===================
const char* AP_SSID = "ESP32-Piano";
const char* AP_PASSWORD = "12345678";

// =================== Power Button ===================
#define POWER_PIN 16
#define LONG_PRESS_MS 3000

// =================== I2S ===================
#define I2S_BCLK 18
#define I2S_LRC 19
#define I2S_DOUT 23
#define SAMPLE_RATE 44100
#define BUF_SAMPLES 256

const float freqs[8] = { 262.0, 294.0, 330.0, 349.0, 392.0, 440.0, 494.0, 523.0 };
float phase[8] = { 0 };

// =================== Keys ===================
const int keyPins[8] = { 17, 14, 22, 21, 13, 27, 26, 25 };
bool rawLast[8] = { false };
bool stable[8] = { false };
unsigned long lastChangeMs[8] = { 0 };
const unsigned long DEBOUNCE_MS = 20;

bool localOn[8] = { false };
bool remoteOn[8] = { false };

// =================== Global Objects ===================
WiFiClient espClient;
PubSubClient mqtt(espClient);
WebServer server(80);
Preferences prefs;

String myTopic;
String otherTopic;

bool deviceOn = true;
bool configMode = false;

// =================== LED Animation State ===================
typedef enum {
  ANIM_NONE,
  ANIM_WIFI_CONNECTING,  // blue chase on gap LEDs
  ANIM_CONFIG_MODE,      // blue breathing on gap LEDs
  ANIM_LONG_PRESS,       // red fill progress
  ANIM_RESET_FLASH,      // white flash then config
  ANIM_WIFI_SUCCESS,     // green flash
  ANIM_WIFI_FAIL         // red flash
} AnimMode;

AnimMode currentAnim = ANIM_NONE;
unsigned long animStart = 0;

// =================== Forward Declarations ===================
void connectMQTT();
void startConfigMode();
void clearAllNotes();
void publishEvt(char cmd, int idx);
void setAllLEDs(CRGB color);
void showLEDs();
void updateSystemLEDAnimation(unsigned long now);
void flashGapLEDs(CRGB color, unsigned long durationMs);

// =================================================================
//  LED Helpers
// =================================================================
#if USE_WS2812

CRGB noteColor(int i) {
  switch (i) {
    case 0: return CRGB(255, 0, 0);      // C4
    case 1: return CRGB(255, 80, 0);     // D4
    case 2: return CRGB(255, 180, 0);    // E4
    case 3: return CRGB(0, 255, 0);      // F4
    case 4: return CRGB(0, 255, 180);    // G4
    case 5: return CRGB(0, 80, 255);     // A4
    case 6: return CRGB(180, 0, 255);    // B4
    case 7: return CRGB(255, 255, 255);  // C5
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

void flashGapLEDs(CRGB color, unsigned long durationMs) {
  for (int i = 0; i < 7; i++) leds[GAP_LEDS[i]] = color;
  showLEDs();
  delay(durationMs);
  clearGapLEDs();
  showLEDs();
}

void updateGapLEDs(unsigned long now) {
  switch (currentAnim) {

    case ANIM_WIFI_CONNECTING:
      {
        int pos = ((now - animStart) / 100) % 7;
        for (int i = 0; i < 7; i++) {
          leds[GAP_LEDS[i]] = (i == pos) ? CRGB(0, 60, 255) : CRGB::Black;
        }
        break;
      }

    case ANIM_CONFIG_MODE:
      {
        float t = (float)((now - animStart) % 2000) / 2000.0f;
        float brightness = (sinf(t * 2.0f * M_PI - M_PI / 2.0f) + 1.0f) / 2.0f;
        uint8_t b = (uint8_t)(brightness * 120);
        for (int i = 0; i < 7; i++) leds[GAP_LEDS[i]] = CRGB(0, 0, b);
        break;
      }

    case ANIM_LONG_PRESS:
      {
        unsigned long elapsed = now - animStart;
        int filled = (int)(elapsed * 7 / LONG_PRESS_MS);
        if (filled > 7) filled = 7;
        for (int i = 0; i < 7; i++) {
          leds[GAP_LEDS[i]] = (i < filled) ? CRGB(255, 0, 0) : CRGB::Black;
        }
        break;
      }

    case ANIM_RESET_FLASH:
      {
        unsigned long elapsed = now - animStart;
        if (elapsed < 300) {
          setAllLEDs(CRGB(200, 200, 200));
        } else {
          currentAnim = ANIM_CONFIG_MODE;
          animStart = now;
        }
        break;
      }

    case ANIM_WIFI_SUCCESS:
      {
        unsigned long elapsed = now - animStart;
        if (elapsed < 400) {
          for (int i = 0; i < 7; i++) leds[GAP_LEDS[i]] = CRGB(0, 255, 80);
        } else {
          currentAnim = ANIM_NONE;
          clearGapLEDs();
        }
        break;
      }

    case ANIM_WIFI_FAIL:
      {
        unsigned long elapsed = now - animStart;
        if (elapsed < 500) {
          for (int i = 0; i < 7; i++) leds[GAP_LEDS[i]] = CRGB(255, 0, 0);
        } else {
          currentAnim = ANIM_NONE;
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

void updateSystemLEDAnimation(unsigned long now) {
  updateGapLEDs(now);
  showLEDs();
}

#endif

// =================================================================
//  Audio Task (Core 0)
// =================================================================
void audioTask(void* param) {
  int16_t* buf = (int16_t*)malloc(BUF_SAMPLES * sizeof(int16_t));
  if (!buf) {
    Serial.println("[ERROR] audioTask malloc failed");
    vTaskDelete(NULL);
    return;
  }

  while (true) {
    if (!deviceOn || configMode) {
      memset(buf, 0, BUF_SAMPLES * sizeof(int16_t));
      size_t w = 0;
      i2s_channel_write(tx_handle, buf, BUF_SAMPLES * sizeof(int16_t), &w, portMAX_DELAY);
      vTaskDelay(1);
      continue;
    }

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
      buf[s] = (int16_t)(mix * 5000);
    }

    size_t written = 0;
    i2s_channel_write(tx_handle, buf, BUF_SAMPLES * sizeof(int16_t), &written, portMAX_DELAY);
    vTaskDelay(1);
  }
}

// =================================================================
//  MQTT Publish
// =================================================================
void publishEvt(char cmd, int idx) {
  char msg[3] = { cmd, (char)('0' + idx), '\0' };
  bool ok = mqtt.publish(myTopic.c_str(), msg, false);
  Serial.printf("[MQTT] publish %s -> %s : %s\n", myTopic.c_str(), msg, ok ? "OK" : "FAIL");
}

void clearAllNotes() {
  for (int i = 0; i < 8; i++) {
    localOn[i] = false;
    remoteOn[i] = false;
    if (mqtt.connected()) publishEvt('U', i);
  }
}

// =================================================================
//  Config Portal HTML
// =================================================================
String buildPage(String msg = "") {
  int n = WiFi.scanNetworks();
  String options = "";
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    String lock = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "" : "&#128274; ";
    int rssi = WiFi.RSSI(i);
    String bars = rssi > -55 ? "▂▄▆█" : rssi > -70 ? "▂▄▆_"
                                      : rssi > -80 ? "▂▄__"
                                                   : "▂___";
    options += "<option value='" + ssid + "'>" + lock + ssid + "  " + bars + "</option>";
  }
  WiFi.scanDelete();

  String alertHtml = "";
  if (!msg.isEmpty()) {
    bool ok = msg.startsWith("OK") || msg.startsWith("Connected");
    alertHtml = "<div class='alert " + String(ok ? "alert-ok" : "alert-err") + "'>" + msg + "</div>";
  }

  return R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Piano Setup</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=DM+Mono:wght@400;500&family=Syne:wght@700;800&display=swap');
  :root {
    --bg: #0a0a0f;
    --surface: #111118;
    --border: #222230;
    --accent: #7fff6e;
    --accent2: #6e8fff;
    --text: #e8e8f0;
    --muted: #55556a;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: var(--bg);
    color: var(--text);
    font-family: 'DM Mono', monospace;
    min-height: 100vh;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 24px 16px;
  }
  body::before {
    content: '';
    position: fixed;
    inset: 0;
    background-image:
      linear-gradient(rgba(127,255,110,.03) 1px, transparent 1px),
      linear-gradient(90deg, rgba(127,255,110,.03) 1px, transparent 1px);
    background-size: 40px 40px;
    pointer-events: none;
    z-index: 0;
  }
  .wrap {
    position: relative;
    z-index: 1;
    width: 100%;
    max-width: 420px;
  }
  .header {
    margin-bottom: 28px;
  }
  .header .label {
    font-size: 10px;
    letter-spacing: 4px;
    text-transform: uppercase;
    color: var(--accent);
    margin-bottom: 6px;
  }
  .header h1 {
    font-family: 'Syne', sans-serif;
    font-size: 32px;
    font-weight: 800;
    line-height: 1;
    color: var(--text);
  }
  .header h1 span { color: var(--accent); }
  .header p {
    margin-top: 8px;
    font-size: 12px;
    color: var(--muted);
    line-height: 1.6;
  }
  .card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 16px;
    padding: 28px;
    position: relative;
    overflow: hidden;
  }
  .card::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 1px;
    background: linear-gradient(90deg, transparent, var(--accent), transparent);
    opacity: 0.5;
  }
  .field { margin-bottom: 20px; }
  .field label {
    display: block;
    font-size: 10px;
    letter-spacing: 2px;
    text-transform: uppercase;
    color: var(--muted);
    margin-bottom: 8px;
  }
  select, input[type=text], input[type=password] {
    width: 100%;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 12px 14px;
    font-family: 'DM Mono', monospace;
    font-size: 13px;
    color: var(--text);
    outline: none;
    transition: border-color 0.2s;
    appearance: none;
  }
  select:focus, input:focus {
    border-color: var(--accent);
    box-shadow: 0 0 0 3px rgba(127,255,110,0.08);
  }
  select option { background: #111118; }
  .divider {
    display: flex;
    align-items: center;
    gap: 12px;
    margin: 20px 0;
    font-size: 10px;
    letter-spacing: 2px;
    color: var(--muted);
    text-transform: uppercase;
  }
  .divider::before, .divider::after {
    content: '';
    flex: 1;
    height: 1px;
    background: var(--border);
  }
  button {
    width: 100%;
    padding: 14px;
    background: var(--accent);
    color: #0a0a0f;
    border: none;
    border-radius: 8px;
    font-family: 'Syne', sans-serif;
    font-size: 14px;
    font-weight: 700;
    letter-spacing: 1px;
    text-transform: uppercase;
    cursor: pointer;
    transition: opacity 0.2s, transform 0.1s;
    margin-top: 8px;
  }
  button:hover { opacity: 0.88; }
  button:active { transform: scale(0.98); }
  .alert {
    border-radius: 8px;
    padding: 12px 14px;
    font-size: 12px;
    margin-bottom: 20px;
    border: 1px solid;
  }
  .alert-ok  { background: rgba(127,255,110,.08); border-color: rgba(127,255,110,.3); color: var(--accent); }
  .alert-err { background: rgba(255,80,80,.08);   border-color: rgba(255,80,80,.3);   color: #ff6060; }
  .rescan {
    text-align: center;
    margin-top: 18px;
    font-size: 11px;
    letter-spacing: 1px;
  }
  .rescan a {
    color: var(--muted);
    text-decoration: none;
    text-transform: uppercase;
    transition: color 0.2s;
  }
  .rescan a:hover { color: var(--accent2); }
  .keys-deco {
    display: flex;
    gap: 3px;
    margin-bottom: 24px;
  }
  .key-w {
    height: 32px;
    flex: 1;
    background: rgba(255,255,255,0.06);
    border-radius: 0 0 4px 4px;
    border: 1px solid var(--border);
    border-top: none;
  }
  .key-b {
    height: 20px;
    width: 16px;
    background: var(--accent);
    border-radius: 0 0 3px 3px;
    opacity: 0.15;
    flex-shrink: 0;
  }
</style>
</head>
<body>
<div class="wrap">
  <div class="header">
    <div class="label">ESP32 · Device )"
         + String(DEVICE_ID) + R"(</div>
    <h1>Piano<span>.</span><br>Setup</h1>
    <p>Connect to a network to enable MQTT sync<br>between the two piano devices.</p>
  </div>

  <div class="card">
    <div class="keys-deco">
      <div class="key-w"></div><div class="key-b"></div>
      <div class="key-w"></div><div class="key-b"></div>
      <div class="key-w"></div>
      <div class="key-w"></div><div class="key-b"></div>
      <div class="key-w"></div><div class="key-b"></div>
      <div class="key-w"></div><div class="key-b"></div>
      <div class="key-w"></div>
    </div>

    )" + alertHtml
         + R"(

    <form method="POST" action="/connect">
      <div class="field">
        <label>Select Network</label>
        <select name="ssid">)"
         + options + R"(</select>
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
  </div>

  <div class="rescan"><a href="/">↺ &nbsp;Rescan networks</a></div>
</div>
</body></html>)";
}

// =================================================================
//  Web Routes
// =================================================================
void handleRoot() {
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

  server.send(200, "text/html; charset=UTF-8", R"(<!DOCTYPE html>
<html><head><meta charset="UTF-8">
<style>
  @import url('https://fonts.googleapis.com/css2?family=Syne:wght@700;800&family=DM+Mono&display=swap');
  body{background:#0a0a0f;color:#e8e8f0;font-family:'DM Mono',monospace;
       display:flex;align-items:center;justify-content:center;height:100vh;margin:0}
  .box{text-align:center}
  .spinner{width:48px;height:48px;border:2px solid #222230;border-top-color:#7fff6e;
           border-radius:50%;animation:spin 0.8s linear infinite;margin:0 auto 24px}
  @keyframes spin{to{transform:rotate(360deg)}}
  h2{font-family:'Syne',sans-serif;font-size:22px;margin-bottom:8px}
  p{color:#55556a;font-size:12px;line-height:1.8}
  .ssid{color:#7fff6e}
</style></head>
<body><div class="box">
  <div class="spinner"></div>
  <h2>Connecting...</h2>
  <p>Joining network <span class="ssid">)" + ssid + R"(</span><br>
  This may take up to 15 seconds.<br>
  Hotspot will close on success.</p>
</div></body></html>)");

  delay(500);
  WiFi.begin(ssid.c_str(), pwd.c_str());
  Serial.printf("[WiFi] Connecting to: %s\n", ssid.c_str());

  currentAnim = ANIM_WIFI_CONNECTING;
  animStart = millis();

  int waited = 0;
  while (WiFi.status() != WL_CONNECTED && waited < 15000) {
#if USE_WS2812
    updateSystemLEDAnimation(millis());
#endif
    delay(100);
    waited += 100;
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    prefs.begin("cfg", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pwd", pwd);
    prefs.end();

    currentAnim = ANIM_WIFI_SUCCESS;
    animStart = millis();
#if USE_WS2812
    while (currentAnim == ANIM_WIFI_SUCCESS) {
      updateSystemLEDAnimation(millis());
      delay(20);
    }
#endif

    configMode = false;
    deviceOn = true;
    WiFi.softAPdisconnect(true);
    server.close();
    connectMQTT();

#if USE_WS2812
    setAllLEDs(CRGB::Black);
    showLEDs();
#endif
  } else {
    Serial.println("\n[WiFi] Connection failed");

    currentAnim = ANIM_WIFI_FAIL;
    animStart = millis();
#if USE_WS2812
    while (currentAnim == ANIM_WIFI_FAIL) {
      updateSystemLEDAnimation(millis());
      delay(20);
    }
#endif

    WiFi.disconnect();
  }
}

// =================================================================
//  Config Mode
// =================================================================
void startConfigMode() {
  configMode = true;
  deviceOn = false;
  currentAnim = ANIM_RESET_FLASH;
  animStart = millis();

  if (mqtt.connected()) {
    clearAllNotes();
    mqtt.disconnect();
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.println("[CONFIG] Hotspot started");
  Serial.printf("[CONFIG] SSID: %s  Password: %s\n", AP_SSID, AP_PASSWORD);
  Serial.println("[CONFIG] Open: http://192.168.4.1");

  server.on("/", handleRoot);
  server.on("/connect", HTTP_POST, handleConnect);
  server.begin();
}

// =================================================================
//  Power Button
// =================================================================
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
      bool turningOff = deviceOn;
      deviceOn = !deviceOn;

      Serial.println(deviceOn ? "[PWR] Device ON" : "[PWR] Device OFF");

      if (turningOff) {
        // OFF
        currentAnim = ANIM_NONE;
        clearAllNotes();

        if (mqtt.connected()) mqtt.disconnect();

        Serial.println("[WiFi] shutting down");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);

#if USE_WS2812
        setAllLEDs(CRGB::Black);
        showLEDs();
#endif
      } else {
        // ON
        Serial.println("[WiFi] starting WiFi");
        WiFi.mode(WIFI_STA);

        if (loadAndConnect()) {
          connectMQTT();
        } else {
          startConfigMode();
        }
      }
    }
  }
}

  // =================================================================
  //  WiFi
  // =================================================================
  bool loadAndConnect() {
    prefs.begin("cfg", true);
    String ssid = prefs.getString("ssid", "");
    String pwd = prefs.getString("pwd", "");
    prefs.end();

    if (ssid.isEmpty()) {
      Serial.println("[WiFi] No saved credentials");
      return false;
    }

    Serial.printf("[WiFi] Saved network: %s — connecting...\n", ssid.c_str());

#if USE_WS2812
    for (int i = 0; i < 7; i++) leds[GAP_LEDS[i]] = CRGB(0, 0, 180);
    showLEDs();
    delay(500);
    clearGapLEDs();
    showLEDs();
    delay(100);
#endif

    currentAnim = ANIM_WIFI_CONNECTING;
    animStart = millis();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pwd.c_str());

    int waited = 0;
    while (WiFi.status() != WL_CONNECTED && waited < 12000) {
#if USE_WS2812
      updateSystemLEDAnimation(millis());
#endif
      delay(100);
      waited += 100;
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      currentAnim = ANIM_WIFI_SUCCESS;
      animStart = millis();

#if USE_WS2812
      while (currentAnim == ANIM_WIFI_SUCCESS) {
        updateSystemLEDAnimation(millis());
        delay(20);
      }
#endif

      currentAnim = ANIM_NONE;
      return true;
    }

    currentAnim = ANIM_WIFI_FAIL;
    animStart = millis();

#if USE_WS2812
    while (currentAnim == ANIM_WIFI_FAIL) {
      updateSystemLEDAnimation(millis());
      delay(20);
    }
#endif

    currentAnim = ANIM_NONE;
    return false;
  }

  // =================================================================
  //  MQTT
  // =================================================================
  void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (String(topic) != otherTopic || length < 2) return;
    char cmd = (char)payload[0];
    int idx = ((char)payload[1]) - '0';
    Serial.printf("[MQTT] recv [%s]: %c%d\n", topic, cmd, idx);
    if (idx < 0 || idx > 7) return;
    if (cmd == 'D') remoteOn[idx] = true;
    else if (cmd == 'U') remoteOn[idx] = false;
  }

  void connectMQTT() {
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqttCallback);

    while (!mqtt.connected()) {
      Serial.print("[MQTT] Connecting...");
      String cid = String("ESP32-") + DEVICE_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
      bool ok = (strlen(MQTT_USER) > 0)
                  ? mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS)
                  : mqtt.connect(cid.c_str());

      if (ok) {
        Serial.println(" connected!");
        mqtt.subscribe(otherTopic.c_str());
        for (int i = 0; i < 8; i++) remoteOn[i] = false;
      } else {
        Serial.printf(" failed (rc=%d), retry in 5s\n", mqtt.state());
        delay(5000);
      }
    }
  }

  // =================================================================
  //  I2S Setup
  // =================================================================
  void setupI2S() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (err != ESP_OK) {
      Serial.printf("[I2S] i2s_new_channel failed: %d\n", err);
      while (true) delay(1000);
    }

    i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = (gpio_num_t)I2S_BCLK,
        .ws = (gpio_num_t)I2S_LRC,
        .dout = (gpio_num_t)I2S_DOUT,
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {
          .mclk_inv = false,
          .bclk_inv = false,
          .ws_inv = false },
      },
    };

    err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (err != ESP_OK) {
      Serial.printf("[I2S] init_std_mode failed: %d\n", err);
      while (true) delay(1000);
    }

    err = i2s_channel_enable(tx_handle);
    if (err != ESP_OK) {
      Serial.printf("[I2S] enable failed: %d\n", err);
      while (true) delay(1000);
    }

    Serial.println("[I2S] Ready");
  }

  // =================================================================
  //  Setup
  // =================================================================
  void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== ESP32 Piano Boot ===");

    pinMode(POWER_PIN, INPUT_PULLUP);
    for (int i = 0; i < 8; i++) {
      pinMode(keyPins[i], INPUT_PULLUP);
      rawLast[i] = false;
      stable[i] = false;
      localOn[i] = false;
      remoteOn[i] = false;
      lastChangeMs[i] = millis();
    }

    myTopic = String(BASE) + "/" + DEVICE_ID + "/evt";
    otherTopic = String(BASE) + "/" + (String(DEVICE_ID) == "A" ? "B" : "A") + "/evt";

    Serial.println("[I2S] Initialising...");
    setupI2S();

#if USE_WS2812
    Serial.println("[LED] Initialising...");
    setupLEDs();
    Serial.println("[LED] Ready");
#endif

    Serial.println("[AUDIO] Starting task on Core 0...");
    xTaskCreatePinnedToCore(audioTask, "audioTask", 8192, NULL, 1, NULL, 0);
    Serial.println("[AUDIO] Task started");

    WiFi.mode(WIFI_AP_STA);
    if (loadAndConnect()) {
      Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
      connectMQTT();
    } else {
      Serial.println("[WiFi] No saved network or connection failed — entering config mode");
      startConfigMode();
    }
  }

  // =================================================================
  //  Loop (Core 1)
  // =================================================================
  void loop() {
    checkPowerButton();

    if (configMode) {
      server.handleClient();
#if USE_WS2812
      static unsigned long lastLED_cfg = 0;
      unsigned long now_cfg = millis();
      if (now_cfg - lastLED_cfg > 30) {
        lastLED_cfg = now_cfg;
        updateSystemLEDAnimation(now_cfg);
      }
#endif
      return;
    }

    if (!deviceOn) {
      if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) connectMQTT();
      mqtt.loop();
      return;
    }

    if (WiFi.getMode() != WIFI_OFF && WiFi.status() != WL_CONNECTED) {
      if (loadAndConnect()) {
        connectMQTT();
      } else {
        startConfigMode();
        return;
      }
    }

    if (!mqtt.connected()) {
      Serial.println("[MQTT] Disconnected — reconnecting...");
      connectMQTT();
    }
    mqtt.loop();

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
        publishEvt(raw ? 'D' : 'U', i);
        Serial.printf("[KEY] key%d %s (GPIO%d)\n", i + 1, raw ? "DOWN" : "UP", keyPins[i]);
      }
    }

#if USE_WS2812
    static unsigned long lastLED = 0;
    if (now - lastLED > 30) {
      lastLED = now;
      updateKeyLEDs();
      updateSystemLEDAnimation(now);
    }
#endif
  }