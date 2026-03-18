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
  #include <FastLED.h>
  #define LED_DATA_PIN    4
  #define NUM_LEDS        15
  #define LED_BRIGHTNESS  20
  CRGB leds[NUM_LEDS];
#endif

// =================== MQTT ===================
const char* MQTT_HOST = "mqtt.cetools.org";
const int   MQTT_PORT = 1884;
const char* MQTT_USER = MQTT_USERNAME;
const char* MQTT_PASS = MQTT_PASSWORD;
const char* DEVICE_ID = "B"; // A板改成"A"
const char* BASE      = "student/ucfnlwa/0021";

// =================== 配网热点 ===================
const char* AP_SSID     = "ESP32-Piano";
const char* AP_PASSWORD = "12345678";

// =================== 电源按钮 ===================
#define POWER_PIN     16
#define LONG_PRESS_MS 3000

// =================== I2S ===================
#define I2S_BCLK    18
#define I2S_LRC     19
#define I2S_DOUT    23
#define SAMPLE_RATE 44100
#define BUF_SAMPLES 256

const float freqs[8] = {262.0, 294.0, 330.0, 349.0, 392.0, 440.0, 494.0, 523.0};
float phase[8] = {0};

// =================== 琴键 ===================
const int keyPins[8]          = {17, 14, 22, 21, 13, 27, 26, 25};
bool      rawLast[8]          = {false};
bool      stable[8]           = {false};
unsigned long lastChangeMs[8] = {0};
const unsigned long DEBOUNCE_MS = 20;

bool localOn[8]  = {false};
bool remoteOn[8] = {false};

// =================== 全局对象 ===================
WiFiClient   espClient;
PubSubClient mqtt(espClient);
WebServer    server(80);
Preferences  prefs;

String myTopic;
String otherTopic;

bool deviceOn   = true;
bool configMode = false;

// =================== 前向声明 ===================
void connectMQTT();
void startConfigMode();
void clearAllNotes();
void publishEvt(char cmd, int idx);

// =================================================================
//  音频任务（跑在 Core 0，完全独立）
// =================================================================
void audioTask(void* param) {
  int16_t* buf = (int16_t*)malloc(BUF_SAMPLES * sizeof(int16_t));
  if (!buf) {
    Serial.println("❌ audioTask 内存分配失败");
    vTaskDelete(NULL);
    return;
  }

  while (true) {
    if (!deviceOn || configMode) {
      // 设备关闭时输出静音，防止I2S卡住
      memset(buf, 0, BUF_SAMPLES * sizeof(int16_t));
      size_t w = 0;
      // i2s_write(I2S_NUM_0, buf, BUF_SAMPLES * sizeof(int16_t), &w, portMAX_DELAY);
      i2s_channel_write(tx_handle, buf, BUF_SAMPLES * sizeof(int16_t), &w, portMAX_DELAY);
      vTaskDelay(1);
      continue;
    }

    for (int s = 0; s < BUF_SAMPLES; s++) {
      float mix = 0.0f;
      int   cnt = 0;
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
    // i2s_write(I2S_NUM_0, buf, BUF_SAMPLES * sizeof(int16_t), &written, portMAX_DELAY);
    i2s_channel_write(tx_handle, buf, BUF_SAMPLES * sizeof(int16_t), &written, portMAX_DELAY);
  }
}

// =================================================================
//  MQTT 发布
// =================================================================
void publishEvt(char cmd, int idx) {
  char msg[3] = {cmd, (char)('0' + idx), '\0'};
  bool ok = mqtt.publish(myTopic.c_str(), msg, false);
  Serial.printf("发布 %s -> %s : %s\n", myTopic.c_str(), msg, ok ? "✅" : "❌");
}

void clearAllNotes() {
  for (int i = 0; i < 8; i++) {
    localOn[i] = false;
    if (mqtt.connected()) publishEvt('U', i);
  }
}

// =================================================================
//  配网页面
// =================================================================
String buildPage(String msg = "") {
  int n = WiFi.scanNetworks();
  String options = "";
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    String lock = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "🔓" : "🔒";
    options += "<option value='" + ssid + "'>" + lock + " " + ssid +
               "  (" + String(WiFi.RSSI(i)) + "dBm)</option>";
  }
  WiFi.scanDelete();

  String statusBlock = "";
  if (!msg.isEmpty()) {
    String color = msg.startsWith("\u2705") ? "#2ecc71" : "#e74c3c";
    statusBlock = "<div style='background:" + color +
                  ";color:white;padding:12px;border-radius:8px;margin-bottom:16px'>" +
                  msg + "</div>";
  }

  return R"(<!DOCTYPE html><html lang="zh">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32 Piano 配网</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:-apple-system,sans-serif;background:#1a1a2e;
         min-height:100vh;display:flex;align-items:center;justify-content:center}
    .card{background:#16213e;border-radius:16px;padding:32px;width:90%;
          max-width:380px;box-shadow:0 4px 32px rgba(0,0,0,.5);color:#eee}
    h2{font-size:20px;color:#e94560;margin-bottom:6px}
    p.sub{color:#888;font-size:13px;margin-bottom:24px}
    label{font-size:13px;color:#aaa;display:block;margin:14px 0 6px}
    select,input{width:100%;padding:10px 14px;background:#0f3460;
                 border:1.5px solid #444;border-radius:8px;
                 font-size:14px;color:#eee;outline:none}
    select:focus,input:focus{border-color:#e94560}
    button{width:100%;padding:12px;margin-top:22px;background:#e94560;
           color:white;border:none;border-radius:8px;font-size:15px;cursor:pointer}
    button:hover{background:#c73652}
    .tip{text-align:center;margin-top:14px;font-size:13px}
    .tip a{color:#e94560;text-decoration:none}
  </style>
</head>
<body>
<div class="card">
  <h2>🎹 Piano 配网</h2>
  <p class="sub">连接成功后自动恢复 MQTT，热点自动关闭</p>
)" + statusBlock + R"(
  <form method="POST" action="/connect">
    <label>选择 WiFi</label>
    <select name="ssid">)" + options + R"(</select>
    <label>或手动输入 WiFi 名称</label>
    <input type="text" name="manual_ssid" placeholder="留空则用上方选择">
    <label>WiFi 密码</label>
    <input type="password" name="password" placeholder="输入密码">
    <button type="submit">连接并保存</button>
  </form>
  <div class="tip"><a href="/">🔄 重新扫描</a></div>
</div>
</body></html>)";
}

// =================================================================
//  Web 路由
// =================================================================
void handleRoot() {
  server.send(200, "text/html; charset=UTF-8", buildPage());
}

void handleConnect() {
  String ssid = server.arg("manual_ssid");
  if (ssid.isEmpty()) ssid = server.arg("ssid");
  String pwd = server.arg("password");

  if (ssid.isEmpty()) {
    server.send(200, "text/html; charset=UTF-8", buildPage("❌ WiFi 名称不能为空"));
    return;
  }

  server.send(200, "text/html; charset=UTF-8",
    "<html><head><meta charset='UTF-8'></head>"
    "<body style='font-family:sans-serif;background:#1a1a2e;color:#eee;"
    "display:flex;align-items:center;justify-content:center;height:100vh'>"
    "<div style='text-align:center'><div style='font-size:48px'>⏳</div>"
    "<h3 style='margin-top:16px'>正在连接 " + ssid + "</h3>"
    "<p style='color:#888;margin-top:8px;font-size:13px'>约10秒，成功后热点自动关闭</p>"
    "</div></body></html>");

  delay(500);
  WiFi.begin(ssid.c_str(), pwd.c_str());
  Serial.printf("连接 WiFi: %s ...\n", ssid.c_str());

  int waited = 0;
  while (WiFi.status() != WL_CONNECTED && waited < 15000) {
    delay(500); waited += 500; Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n✅ WiFi已连接! IP: %s\n", WiFi.localIP().toString().c_str());
    prefs.begin("cfg", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pwd", pwd);
    prefs.end();

    configMode = false;
    deviceOn   = true;
    WiFi.softAPdisconnect(true);
    server.close();
    connectMQTT();

#if USE_WS2812
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Black;
    FastLED.show();
#endif
  } else {
    Serial.println("\n❌ WiFi连接失败，请重试");
    WiFi.disconnect();
  }
}

// =================================================================
//  配网模式
// =================================================================
void startConfigMode() {
  configMode = true;
  deviceOn   = false;

  if (mqtt.connected()) {
    clearAllNotes();
    mqtt.disconnect();
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.println("====== 配网模式 ======");
  Serial.printf("热点: %s  密码: %s\n", AP_SSID, AP_PASSWORD);
  Serial.println("浏览器打开: http://192.168.4.1");
  Serial.println("======================");

  server.on("/",        handleRoot);
  server.on("/connect", HTTP_POST, handleConnect);
  server.begin();

#if USE_WS2812
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Blue;
  FastLED.show();
#endif
}

// =================================================================
//  电源按钮
// =================================================================
void checkPowerButton() {
  static unsigned long pressStart  = 0;
  static unsigned long releaseTime = 0;
  static bool          pressing    = false;
  static bool          longFired   = false;

  bool cur = (digitalRead(POWER_PIN) == LOW);

  if (cur && !pressing) {
    if (millis() - releaseTime < 200) return;
    pressing   = true;
    longFired  = false;
    pressStart = millis();
  }

  if (!cur && pressing) {
    pressing    = false;
    releaseTime = millis();
    if (millis() - pressStart < 30) return;

    if (!longFired) {
      deviceOn = !deviceOn;
      Serial.println(deviceOn ? "▶️  设备已开启" : "⏸️  设备已暂停");
      if (!deviceOn) {
        clearAllNotes();
#if USE_WS2812
        for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Black;
        FastLED.show();
#endif
      }
    }
  }

  if (pressing && !longFired) {
    if (millis() - pressStart >= LONG_PRESS_MS) {
      longFired = true;
      Serial.println("🔧 长按 → 进入配网模式");
      startConfigMode();
    }
  }
}

// =================================================================
//  WiFi
// =================================================================
bool loadAndConnect() {
  prefs.begin("cfg", true);
  String ssid = prefs.getString("ssid", "");
  String pwd  = prefs.getString("pwd",  "");
  prefs.end();

  if (ssid.isEmpty()) {
    Serial.println("Flash 中无 WiFi 配置");
    return false;
  }

  Serial.printf("读取已保存 WiFi: %s 连接中...\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pwd.c_str());

  int waited = 0;
  while (WiFi.status() != WL_CONNECTED && waited < 12000) {
    delay(500); waited += 500; Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

// =================================================================
//  MQTT
// =================================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != otherTopic || length < 2) return;
  char cmd = (char)payload[0];
  int  idx = ((char)payload[1]) - '0';
  Serial.printf("收到 [%s]: %c%d\n", topic, cmd, idx);
  if (idx < 0 || idx > 7) return;
  if      (cmd == 'D') remoteOn[idx] = true;
  else if (cmd == 'U') remoteOn[idx] = false;
}

void connectMQTT() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  while (!mqtt.connected()) {
    Serial.print("连接 MQTT...");
    String cid = String("ESP32-") + DEVICE_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    bool ok = (strlen(MQTT_USER) > 0)
      ? mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS)
      : mqtt.connect(cid.c_str());

    if (ok) {
      Serial.println(" ✅ 已连接!");
      mqtt.subscribe(otherTopic.c_str());
      for (int i = 0; i < 8; i++) remoteOn[i] = false;
      // ⚠️ 不在这里调用 clearAllNotes()，避免重连时打断音频
    } else {
      Serial.printf(" ❌ 失败(rc=%d)，5秒后重试\n", mqtt.state());
      delay(5000);
    }
  }
}

// =================================================================
//  I2S
// =================================================================
// void setupI2S() {
//   i2s_config_t cfg = {
//     .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
//     .sample_rate          = SAMPLE_RATE,
//     .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
//     .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
//     .communication_format = I2S_COMM_FORMAT_I2S,
//     .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
//     .dma_buf_count        = 8,
//     .dma_buf_len          = BUF_SAMPLES,
//     .use_apll             = false,
//     .tx_desc_auto_clear   = true,
//     .fixed_mclk           = 0
//   };
//   i2s_pin_config_t pins = {
//     .bck_io_num   = I2S_BCLK,
//     .ws_io_num    = I2S_LRC,
//     .data_out_num = I2S_DOUT,
//     .data_in_num  = I2S_PIN_NO_CHANGE
//   };
//   i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
//   i2s_set_pin(I2S_NUM_0, &pins);
// }

void setupI2S() {
  // 创建 TX 通道
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

  esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
  if (err != ESP_OK) {
    Serial.printf("❌ i2s_new_channel failed: %d\n", err);
    while (true) delay(1000);
  }

  // 标准 I2S 模式配置
  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK,
      .ws   = (gpio_num_t)I2S_LRC,
      .dout = (gpio_num_t)I2S_DOUT,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv   = false,
      },
    },
  };

  err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
  if (err != ESP_OK) {
    Serial.printf("❌ i2s_channel_init_std_mode failed: %d\n", err);
    while (true) delay(1000);
  }

  err = i2s_channel_enable(tx_handle);
  if (err != ESP_OK) {
    Serial.printf("❌ i2s_channel_enable failed: %d\n", err);
    while (true) delay(1000);
  }

  Serial.println("✅ New I2S driver ready");
}

// =================================================================
//  LED
// =================================================================
#if USE_WS2812
void setupLEDs() {
  FastLED.addLeds<WS2812, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(LED_BRIGHTNESS);
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB::Black;
  FastLED.show();
}

CRGB noteColor(int i) {
  switch (i) {
    case 0: return CRGB(255, 0,   0);
    case 1: return CRGB(255, 80,  0);
    case 2: return CRGB(255, 180, 0);
    case 3: return CRGB(0,   255, 0);
    case 4: return CRGB(0,   255, 180);
    case 5: return CRGB(0,   80,  255);
    case 6: return CRGB(180, 0,   255);
    case 7: return CRGB(255, 255, 255);
  }
  return CRGB::White;
}

void updateLEDs() {
  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < 8 && (localOn[i] || remoteOn[i])) {
      leds[i] = noteColor(i);
    } else {
      leds[i] = CRGB::Black;
    }
  }
  FastLED.show();
}
#endif

// =================================================================
//  琴键扫描
// =================================================================
void scanKeysAndPublish() {
  unsigned long now = millis();
  for (int i = 0; i < 8; i++) {
    bool raw = (digitalRead(keyPins[i]) == LOW);
    if (raw != rawLast[i]) { rawLast[i] = raw; lastChangeMs[i] = now; }
    if ((now - lastChangeMs[i]) > DEBOUNCE_MS && stable[i] != raw) {
      stable[i]  = raw;
      localOn[i] = raw;
      publishEvt(raw ? 'D' : 'U', i);
      Serial.printf("键%d %s (GPIO%d)\n", i+1, raw ? "按下" : "松开", keyPins[i]);
    }
  }
}

// =================================================================
//  Setup
// =================================================================
void setup() {
  Serial.begin(115200);
  delay(500);  // 改长一点，等Serial稳定

  Serial.println("=== 启动 ===");  // 加这行确认能输出 

  pinMode(POWER_PIN, INPUT_PULLUP);
  for (int i = 0; i < 8; i++) {
    pinMode(keyPins[i], INPUT_PULLUP);
    rawLast[i] = stable[i] = localOn[i] = remoteOn[i] = false;
    lastChangeMs[i] = millis();
  }

  myTopic    = String(BASE) + "/" + DEVICE_ID + "/evt";
  otherTopic = String(BASE) + "/" + (String(DEVICE_ID) == "A" ? "B" : "A") + "/evt";
  Serial.println("初始化 I2S...");
  setupI2S();
  Serial.println("I2S 完成");

#if USE_WS2812
  Serial.println("初始化 LED...");
  setupLEDs();
  Serial.println("LED 完成");
#endif

  Serial.println("启动音频任务...");
  // 音频任务跑在 Core 0
  // 音频任务跑在 Core 0
  xTaskCreatePinnedToCore(
    audioTask,
    "audioTask",
    8192,
    NULL,
    2,
    NULL,
    0
  );
  Serial.println("音频任务已启动");
// #if USE_WS2812
//   setupLEDs();
// #endif

  WiFi.mode(WIFI_AP_STA);
  if (loadAndConnect()) {
    Serial.printf("✅ WiFi已连接! IP: %s\n", WiFi.localIP().toString().c_str());
    connectMQTT();
  } else {
    Serial.println("无已保存 WiFi，自动进入配网模式");
    startConfigMode();
  }
}

// =================================================================
//  Loop（跑在 Core 1）
// =================================================================
void loop() {
  checkPowerButton();

  if (configMode) {
    server.handleClient();
    return;
  }

  if (!deviceOn) {
    if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) connectMQTT();
    mqtt.loop();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) loadAndConnect();
  if (!mqtt.connected()) {
    Serial.println("MQTT断了，重连...");
    connectMQTT();
  }
  mqtt.loop();

  scanKeysAndPublish();

#if USE_WS2812
  static unsigned long lastLED = 0;
  if (millis() - lastLED > 30) {
    lastLED = millis();
    updateLEDs();
  }
#endif
}
