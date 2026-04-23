// // ESP32: Hold button -> LED ON + buzzer ON; Release -> OFF
// // Button: GPIO14 to GND (uses internal pull-up)
// // LED: GPIO26 -> 220~330 ohm -> LED+ ; LED- to GND
// // Buzzer (PKM22EPP-40): GPIO25 to GND (piezo)

// const int BUTTON_PIN = 14;
// const int LED_PIN    = 26;
// const int BUZZER_PIN = 25;

// // PWM channel for ESP32 buzzer
// const int PWM_CH   = 0;
// const int PWM_RES  = 10;     // 0..1023
// const int PWM_DUTY = 512;    // ~50%

// // Pick a tone frequency (Hz). 2000~4000 Hz usually audible and clear for piezo.
// const int BUZZER_FREQ = 2500;

// void setup() {
//   pinMode(BUTTON_PIN, INPUT_PULLUP);
//   pinMode(LED_PIN, OUTPUT);
//   digitalWrite(LED_PIN, LOW);
//   noTone(BUZZER_PIN);  // stop
// }

// void loop() {
//   bool pressed = (digitalRead(BUTTON_PIN) == LOW);

//   if (pressed) {
//     digitalWrite(LED_PIN, HIGH);
//     tone(BUZZER_PIN, BUZZER_FREQ);
//   } else {
//     digitalWrite(LED_PIN, LOW);
//     noTone(BUZZER_PIN);
//   }
// }

// from wifi_secrets.h: MQTT_USER / MQTT_PASS
#include <WiFi.h>
#include <PubSubClient.h>
#include "wifi_secrets.h"  // #define WIFI_SSID "...", #define WIFI_PASS "..."

const char* MQTT_HOST = "mqtt.cetools.org";
const int   MQTT_PORT = 1884;

const char* DEVICE_ID = "A";   // B on the other board

const int BUTTON_PIN = 14;   // OUT from button module
const int LED_PIN    = 26;
const int BUZZER_PIN = 25;

const bool ACTIVE_LOW = true;  // flip if reversed
const int BUZZER_FREQ = 2500;

WiFiClient espClient;
PubSubClient client(espClient);

bool localPressed  = false;
bool remotePressed = false;

String myTopic;
String otherTopic;

bool readPressed() {
  return digitalRead(BUTTON_PIN) == HIGH;
}


void applyOutput(bool active) {
  digitalWrite(LED_PIN, active ? HIGH : LOW);
  if (active) tone(BUZZER_PIN, BUZZER_FREQ);
  else noTone(BUZZER_PIN);
}

void onMessage(char* topic, byte* payload, unsigned int length) {
  if (length == 0) return;
  bool pressed = ((char)payload[0] == '1');
  if (String(topic) == otherTopic) {
    remotePressed = pressed;
    Serial.print("Remote pressed = "); Serial.println(remotePressed);
  }
}

void connectWiFi() {
  Serial.print("Connecting WiFi to "); Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.print("\nWiFi OK, IP: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(onMessage);

  while (!client.connected()) {
    String cid = String("ESP32-") + DEVICE_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.print("Connecting MQTT as "); Serial.println(cid);

    if (client.connect(cid.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("MQTT connected");
    } else {
      Serial.print("MQTT failed, rc=");
      Serial.println(client.state());
      delay(1000);
    }
  }

  client.subscribe(otherTopic.c_str());
  remotePressed = false; 
  Serial.print("Subscribed: "); Serial.println(otherTopic);

  client.publish(myTopic.c_str(), "0", true);
  Serial.print("Published init: "); Serial.println(myTopic);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  //pinMode(BUTTON_PIN, INPUT_PULLUP);     // module provides output level
  pinMode(BUTTON_PIN, INPUT); 
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  noTone(BUZZER_PIN);

  myTopic    = String("student/ucfnlwa/0021/") + DEVICE_ID + "/pressed";
  otherTopic = String("student/ucfnlwa/0021/") + (String(DEVICE_ID)=="A" ? "B" : "A") + "/pressed";

  Serial.print("My topic: "); Serial.println(myTopic);
  Serial.print("Other topic: "); Serial.println(otherTopic);

  connectWiFi();
  connectMQTT();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!client.connected()) connectMQTT();
  client.loop();

  bool nowPressed = readPressed();

  static bool lastSent = false;
  if (nowPressed != lastSent) {
    client.publish(myTopic.c_str(), nowPressed ? "1" : "0", true);
    Serial.print("Sent "); Serial.print(nowPressed); Serial.print(" to "); Serial.println(myTopic);
    lastSent = nowPressed;
  }

  localPressed = nowPressed;

  // ✅ mirror OR
  bool active = localPressed || remotePressed;
  applyOutput(active);

  delay(10);
}


// #include <WiFi.h>
// #include <PubSubClient.h>
// #include "wifi_secrets.h"

// #include <driver/i2s.h>
// #include <math.h>

// // ========= MQTT =========
// const char* MQTT_HOST = "mqtt.cetools.org";
// const int   MQTT_PORT = 1883;

// // One board set "A", the other set "B"
// const char* DEVICE_ID = "A";

// // Base topic
// const char* BASE = "student/ucfnlwa/0021";

// // ========= I2S (MAX98357A) =========
// #define I2S_BCLK 18
// #define I2S_LRC  19
// #define I2S_DOUT 23
// #define SAMPLE_RATE 44100
// #define BUF_SAMPLES 256

// // ========= Keys (7 notes) =========
// // Each key: GPIO -> button -> GND, use INPUT_PULLUP
// const int keyPins[7] = {14, 27, 33, 32, 21, 22, 17};

// // C major: C4 D4 E4 F4 G4 A4 B4
// const float freqs[7] = {262.0, 294.0, 330.0, 349.0, 392.0, 440.0, 494.0};

// // ========= State =========
// WiFiClient espClient;
// PubSubClient client(espClient);

// String myTopic;     // e.g. student/ucfnlwa/0021/A/evt
// String otherTopic;  // e.g. student/ucfnlwa/0021/B/evt

// bool localActive[7]  = {false};
// bool remoteActive[7] = {false};

// // debounce
// bool rawLast[7] = {false};
// bool stable[7]  = {false};
// unsigned long lastChangeMs[7] = {0};
// const unsigned long DEBOUNCE_MS = 20;

// // audio synth
// float phase[7] = {0};

// // ========= Helpers =========
// void connectWiFi() {
//   WiFi.mode(WIFI_STA);
//   WiFi.begin(WIFI_SSID, WIFI_PASS);
//   while (WiFi.status() != WL_CONNECTED) delay(300);
// }

// void setupI2S() {
//   i2s_config_t cfg = {
//     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
//     .sample_rate = SAMPLE_RATE,
//     .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
//     .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // mono
//     .communication_format = I2S_COMM_FORMAT_I2S,
//     .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
//     .dma_buf_count = 8,
//     .dma_buf_len = BUF_SAMPLES,
//     .use_apll = false,
//     .tx_desc_auto_clear = true,
//     .fixed_mclk = 0
//   };

//   i2s_pin_config_t pins = {
//     .bck_io_num = I2S_BCLK,
//     .ws_io_num = I2S_LRC,
//     .data_out_num = I2S_DOUT,
//     .data_in_num = I2S_PIN_NO_CHANGE
//   };

//   i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
//   i2s_set_pin(I2S_NUM_0, &pins);
// }

// void onMessage(char* topic, byte* payload, unsigned int length) {
//   if (String(topic) != otherTopic || length < 2) return;

//   // payload: 'D''0'..'6'  OR  'U''0'..'6'
//   char cmd = (char)payload[0];
//   int idx = ((char)payload[1]) - '0';
//   if (idx < 0 || idx > 6) return;

//   if (cmd == 'D') remoteActive[idx] = true;
//   if (cmd == 'U') remoteActive[idx] = false;
// }

// void connectMQTT() {
//   client.setServer(MQTT_HOST, MQTT_PORT);
//   client.setCallback(onMessage);

//   while (!client.connected()) {
//     String cid = String("ESP32-") + DEVICE_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
//     client.connect(cid.c_str(), MQTT_USER, MQTT_PASS);
//     delay(300);
//   }
//   client.subscribe(otherTopic.c_str());
// }

// void publishEvt(char cmd, int idx) {
//   char msg[3] = {cmd, (char)('0' + idx), '\0'};
//   client.publish(myTopic.c_str(), msg, false /*retain*/);
// }

// // Read + debounce each key -> update localActive + publish only changes
// void scanKeysAndPublish() {
//   unsigned long now = millis();

//   for (int i = 0; i < 7; i++) {
//     bool rawPressed = (digitalRead(keyPins[i]) == LOW); // INPUT_PULLUP => pressed LOW

//     if (rawPressed != rawLast[i]) {
//       rawLast[i] = rawPressed;
//       lastChangeMs[i] = now;
//     }

//     if ((now - lastChangeMs[i]) > DEBOUNCE_MS) {
//       if (stable[i] != rawPressed) {
//         stable[i] = rawPressed;
//         localActive[i] = stable[i];

//         // publish event
//         publishEvt(localActive[i] ? 'D' : 'U', i);
//       }
//     }
//   }
// }

// // Generate one audio buffer and write to I2S (polyphonic)
// void audioTick() {
//   static int16_t buf[BUF_SAMPLES];

//   for (int s = 0; s < BUF_SAMPLES; s++) {
//     float mix = 0.0f;
//     int cnt = 0;

//     for (int i = 0; i < 7; i++) {
//       bool on = localActive[i] || remoteActive[i];
//       if (on) {
//         mix += sinf(phase[i]);
//         phase[i] += 2.0f * (float)M_PI * freqs[i] / (float)SAMPLE_RATE;
//         if (phase[i] > 2.0f * (float)M_PI) phase[i] -= 2.0f * (float)M_PI;
//         cnt++;
//       }
//     }

//     if (cnt > 0) mix /= (float)cnt;  // prevent clipping
//     buf[s] = (int16_t)(mix * 4000);   // volume (adjust 2000~8000)
//   }

//   size_t written = 0;
//   i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
// }

// void setup() {
//   Serial.begin(115200);
//   delay(200);

//   // keys
//   for (int i = 0; i < 7; i++) {
//     pinMode(keyPins[i], INPUT_PULLUP);
//     rawLast[i] = false;
//     stable[i] = false;
//     localActive[i] = false;
//     remoteActive[i] = false;
//     lastChangeMs[i] = millis();
//   }

//   // topics
//   myTopic    = String(BASE) + "/" + DEVICE_ID + "/evt";
//   otherTopic = String(BASE) + "/" + (String(DEVICE_ID) == "A" ? "B" : "A") + "/evt";

//   setupI2S();

//   connectWiFi();
//   connectMQTT();

//   // On boot, publish all keys up to avoid stale state on the other side
//   for (int i = 0; i < 7; i++) publishEvt('U', i);
// }

// void loop() {
//   if (WiFi.status() != WL_CONNECTED) connectWiFi();
//   if (!client.connected()) connectMQTT();
//   client.loop();

//   scanKeysAndPublish();
//   audioTick(); // keeps audio continuous
// }