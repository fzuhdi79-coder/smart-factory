/*
 * ============================================================
 * ESP32 Node 3 – Gateway & Control Center (FIXED & OPTIMIZED)
 * Smart Factory Distributed Telemetry System
 * ============================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <time.h>

// ── Konfigurasi WiFi & MQTT ───────────────────────────────────
const char* WIFI_SSID   = "Gebang Wetan Anti Gedor 4G";
const char* WIFI_PASS   = "gebangsukses";
const char* MQTT_BROKER = "192.168.1.17";
const int   MQTT_PORT   = 1883;
const char* MQTT_CLIENT = "ESP32_Node3_Gateway";
const char* TOPIC_NODE1 = "factoryesp1in";
const char* TOPIC_NODE2 = "factoryesp2in";
const char* TOPIC_OUT   = "factoryesp1out";

// ── Konfigurasi Firebase ──────────────────────────────────────
const String FIREBASE_URL =
  "https://smart-factory-a05f3-default-rtdb.asia-southeast1.firebasedatabase.app/";

// ── Timezone WIB (UTC+7) ──────────────────────────────────────
#define TIMEZONE_OFFSET (7 * 3600)
#define NTP_SERVER      "pool.ntp.org"

// ── Pin ───────────────────────────────────────────────────────
#define PIN_BUZZER1    25
#define PIN_BUZZER2    26
#define PIN_LED_MERAH  32
#define PIN_LED_HIJAU  33

// ── Interval & Timeout ────────────────────────────────────────
const unsigned long UPLOAD_INTERVAL = 5000;
const unsigned long DATA_STALE_MS   = 15000;
const unsigned long MQTT_RETRY_MS   = 3000;
const unsigned long WIFI_RETRY_MS   = 5000;
const unsigned long ALARM_COOLDOWN  = 10000;

// ── Struktur Data Node ────────────────────────────────────────
struct NodeData {
  float         getaran  = 0, arus = 0, suhu = 0;
  float         suhu_env = 0, humidity = 0, gas = 0;
  bool          anomali  = false, kebocoran = false;
  bool          valid    = false;
  unsigned long receivedAt = 0;
};

NodeData node1, node2;

// ── State Alarm (Non-blocking) ────────────────────────────────
struct AlarmState {
  bool          active      = false;
  int           beepCount   = 0;
  int           targetBeep  = 0;
  bool          buzzerOn    = false;
  unsigned long prevMs      = 0;
  unsigned long lastAlarmMs = 0;
  const unsigned long ON_MS  = 200;
  const unsigned long OFF_MS = 150;
} buzzerAlarm;

// ── Timer ─────────────────────────────────────────────────────
unsigned long lastUploadMs    = 0;
unsigned long lastMqttRetryMs = 0;
unsigned long lastWifiRetryMs = 0;

// ── WiFi & MQTT ───────────────────────────────────────────────
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// ════════════════════════════════════════════════════════════════
// Helper Waktu
// ════════════════════════════════════════════════════════════════
struct tm getWIBTime() {
  time_t now; struct tm t;
  time(&now); now += TIMEZONE_OFFSET;
  gmtime_r(&now, &t);
  return t;
}

String getTimestamp() {
  struct tm t = getWIBTime(); char b[25];
  strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &t);
  return String(b);
}

String getTanggal() {
  struct tm t = getWIBTime(); char b[11];
  strftime(b, sizeof(b), "%Y-%m-%d", &t);
  return String(b);
}

// Path unik: jam_menit_detik_millis4digit
// Contoh: "14_30_05_3872"
String getPathKey() {
  struct tm t = getWIBTime(); char b[9];
  strftime(b, sizeof(b), "%H_%M_%S", &t);
  return String(b) + "_" + String(millis() % 10000);
}

// ════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(PIN_BUZZER1,   OUTPUT); digitalWrite(PIN_BUZZER1,   LOW);
  pinMode(PIN_BUZZER2,   OUTPUT); digitalWrite(PIN_BUZZER2,   LOW);
  pinMode(PIN_LED_MERAH, OUTPUT); digitalWrite(PIN_LED_MERAH, LOW);
  pinMode(PIN_LED_HIJAU, OUTPUT); digitalWrite(PIN_LED_HIJAU, LOW);

  connectWiFiBlocking();

  configTime(0, 0, NTP_SERVER);
  Serial.print("[NTP] Sinkronisasi");
  struct tm t;
  for (int i = 0; i < 20 && !getLocalTime(&t); i++) {
    Serial.print("."); delay(500);
  }
  Serial.printf("\n[NTP] Waktu WIB: %s\n", getTimestamp().c_str());

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(512);
}

// ════════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // 1. Jaga koneksi WiFi (Non-blocking)
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED_HIJAU, LOW);
    if (now - lastWifiRetryMs >= WIFI_RETRY_MS) {
      lastWifiRetryMs = now;
      Serial.println("[WiFi] Mencoba sambung ulang...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    return;
  }

  // 2. Jaga koneksi MQTT (Non-blocking)
  if (!mqtt.connected()) {
    digitalWrite(PIN_LED_HIJAU, LOW);
    if (now - lastMqttRetryMs >= MQTT_RETRY_MS) {
      lastMqttRetryMs = now;
      reconnectMQTTOnce();
    }
  } else {
    mqtt.loop();
    digitalWrite(PIN_LED_HIJAU, HIGH);
  }

  // 3. Update alarm buzzer (Non-blocking)
  updateAlarm(now);

  // 4. Upload ke Firebase setiap UPLOAD_INTERVAL
  if (now - lastUploadMs >= UPLOAD_INTERVAL) {
    lastUploadMs = now;
    uploadKeFirebase(now);
  }
}

// ════════════════════════════════════════════════════════════════
// Callback MQTT
// ════════════════════════════════════════════════════════════════
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  char msg[length + 1];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, msg) != DeserializationError::Ok) {
    Serial.printf("[MQTT] JSON error dari topic: %s\n", topic);
    return;
  }

  unsigned long now = millis();

  if (strcmp(topic, TOPIC_NODE1) == 0) {
    node1.getaran    = doc["getaran"]  | 0.0f;
    node1.arus       = doc["arus"]     | 0.0f;
    node1.suhu       = doc["suhu"]     | 0.0f;
    node1.anomali    = doc["anomali"]  | false;
    node1.valid      = true;
    node1.receivedAt = now;
    Serial.printf("[Node1] G=%.2f A=%.2f S=%.2f Anomali=%d\n",
                  node1.getaran, node1.arus, node1.suhu, node1.anomali);
    if (node1.anomali) triggerAlarm(now, 2);

  } else if (strcmp(topic, TOPIC_NODE2) == 0) {
    node2.suhu_env   = doc["suhu"]      | 0.0f;
    node2.humidity   = doc["humidity"]  | 0.0f;
    node2.gas        = doc["gas"]       | 0.0f;
    node2.kebocoran  = doc["kebocoran"] | false;
    node2.anomali    = doc["anomali"]   | false;
    node2.valid      = true;
    node2.receivedAt = now;
    Serial.printf("[Node2] S=%.2f H=%.2f G=%.2f Kebocoran=%d\n",
                  node2.suhu_env, node2.humidity, node2.gas, node2.kebocoran);
    if (node2.kebocoran) triggerAlarm(now, 3);
  }

  mqtt.publish(TOPIC_OUT, msg);
}

// ════════════════════════════════════════════════════════════════
// Upload ke Firebase
// ════════════════════════════════════════════════════════════════
void uploadKeFirebase(unsigned long now) {
  bool ada1 = node1.valid && (now - node1.receivedAt < DATA_STALE_MS);
  bool ada2 = node2.valid && (now - node2.receivedAt < DATA_STALE_MS);

  if (!ada1 && !ada2) {
    Serial.println("[Firebase] Tidak ada data baru, upload dilewati.");
    return;
  }

  String url = FIREBASE_URL + "factory_data/"
             + getTanggal() + "/"
             + getPathKey() + ".json";

  StaticJsonDocument<512> doc;
  doc["waktu_lengkap"] = getTimestamp();

  if (ada1) {
    JsonObject m = doc.createNestedObject("node_machine");
    m["getaran"] = node1.getaran;
    m["arus"]    = node1.arus;
    m["suhu"]    = node1.suhu;
    m["anomali"] = node1.anomali;
  }

  if (ada2) {
    JsonObject e = doc.createNestedObject("node_environment");
    e["suhu"]      = node2.suhu_env;
    e["humidity"]  = node2.humidity;
    e["gas"]       = node2.gas;
    e["kebocoran"] = node2.kebocoran;
    e["anomali"]   = node2.anomali;
  }

  char body[512];
  serializeJson(doc, body);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(body);

  if (code == 200 || code == 201) {
    Serial.printf("[Firebase] OK (HTTP %d) — N1:%d N2:%d\n", code, ada1, ada2);
    node1.valid = false;
    node2.valid = false;
  } else {
    Serial.printf("[Firebase] GAGAL! HTTP %d\n%s\n", code, http.getString().c_str());
    digitalWrite(PIN_LED_MERAH, HIGH);
  }
  http.end();
}

// ════════════════════════════════════════════════════════════════
// Alarm Non-blocking (State Machine)
// ════════════════════════════════════════════════════════════════
void triggerAlarm(unsigned long now, int beeps) {
  if (now - buzzerAlarm.lastAlarmMs < ALARM_COOLDOWN) return;
  buzzerAlarm.lastAlarmMs = now;
  buzzerAlarm.targetBeep  = beeps;
  buzzerAlarm.beepCount   = 0;
  buzzerAlarm.active      = true;
  buzzerAlarm.buzzerOn    = false;
  buzzerAlarm.prevMs      = now;
  digitalWrite(PIN_LED_MERAH, HIGH);
}

void updateAlarm(unsigned long now) {
  if (!buzzerAlarm.active) return;

  if (!buzzerAlarm.buzzerOn) {
    if (now - buzzerAlarm.prevMs >= buzzerAlarm.OFF_MS) {
      if (buzzerAlarm.beepCount < buzzerAlarm.targetBeep) {
        buzzerAlarm.prevMs   = now;
        buzzerAlarm.buzzerOn = true;
        digitalWrite(PIN_BUZZER1, HIGH);
        digitalWrite(PIN_BUZZER2, HIGH);
      } else {
        buzzerAlarm.active = false;
        digitalWrite(PIN_BUZZER1, LOW);
        digitalWrite(PIN_BUZZER2, LOW);
        digitalWrite(PIN_LED_MERAH, LOW);
      }
    }
  } else {
    if (now - buzzerAlarm.prevMs >= buzzerAlarm.ON_MS) {
      buzzerAlarm.prevMs   = now;
      buzzerAlarm.buzzerOn = false;
      buzzerAlarm.beepCount++;
      digitalWrite(PIN_BUZZER1, LOW);
      digitalWrite(PIN_BUZZER2, LOW);
    }
  }
}

// ════════════════════════════════════════════════════════════════
// Koneksi & Reconnect
// ════════════════════════════════════════════════════════════════
void connectWiFiBlocking() {
  Serial.printf("[WiFi] Menghubungkan ke %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.printf("\n[WiFi] Terhubung! IP: %s\n", WiFi.localIP().toString().c_str());
}

void reconnectMQTTOnce() {
  Serial.print("[MQTT] Mencoba hubung...");
  if (mqtt.connect(MQTT_CLIENT)) {
    Serial.println(" OK");
    mqtt.subscribe(TOPIC_NODE1);
    mqtt.subscribe(TOPIC_NODE2);
    digitalWrite(PIN_LED_HIJAU, HIGH);
  } else {
    Serial.printf(" Gagal (rc=%d)\n", mqtt.state());
  }
}
