/*
 * ============================================================
 * ESP32 Node 1 – Machine Monitoring (OPTIMIZED & FIXED)
 * Smart Factory Distributed Telemetry System
 * ============================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ── Konfigurasi WiFi & MQTT ───────────────────────────────────
const char* WIFI_SSID     = "Gebang Wetan Anti Gedor 4G"; 
const char* WIFI_PASS     = "gebangsukses"; 
const char* MQTT_BROKER   = "192.168.1.17"; 
const int   MQTT_PORT     = 1883; 
const char* MQTT_CLIENT   = "ESP32_Node1_Machine"; 
const char* MQTT_TOPIC    = "factoryesp1in"; 

// ── Pin definitions ───────────────────────────────────────────
#define PIN_POT_GETARAN   34   
#define PIN_POT_ARUS      35   
#define PIN_POT_SUHU      32   

#define PIN_LED_GETARAN   25   
#define PIN_LED_ARUS      26   
#define PIN_LED_SUHU      33   
#define PIN_BUZZER        27   

// ── Threshold anomali ─────────────────────────────────────────
#define THRESHOLD_GETARAN 3000   // ~7.3 mm/s
#define THRESHOLD_ARUS    3500   // ~42.7 A
#define THRESHOLD_SUHU    3200   // ~78 °C 

// ── Timer & Objects ───────────────────────────────────────────
const unsigned long SEND_INTERVAL = 5000;
unsigned long lastSendTime = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastWiFiReconnectAttempt = 0; // Tambahan untuk mencegah spam WiFi

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// Variabel Global Data Sensor
int adcGetaran = 0, adcArus = 0, adcSuhu = 0;
float getaran = 0.0, arus = 0.0, suhu = 0.0;
bool overloadGetaran = false, overloadArus = false, overloadSuhu = false;
bool adaAnomali = false;

// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_GETARAN, OUTPUT);
  pinMode(PIN_LED_ARUS, OUTPUT);
  pinMode(PIN_LED_SUHU, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED_GETARAN, LOW);
  digitalWrite(PIN_LED_ARUS, LOW);
  digitalWrite(PIN_LED_SUHU, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  connectWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
}

// ═════════════════════════════════════════════════════════════
void loop() {
  // Ambil waktu saat ini SATU KALI saja untuk seluruh operasi di dalam loop
  unsigned long currentMillis = millis();

  // 1. Pengecekan WiFi Non-Blocking (Jeda 5 detik antar percobaan)
  if (WiFi.status() != WL_CONNECTED) {
    if (currentMillis - lastWiFiReconnectAttempt > 5000) {
      lastWiFiReconnectAttempt = currentMillis;
      Serial.println("[WiFi Node 1] Mencoba menyambungkan kembali...");
      WiFi.disconnect(); // Putuskan proses sebelumnya (mencegah error)
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  } else {
    // 2. Jika WiFi terhubung, cek MQTT
    if (!mqtt.connected()) {
      if (currentMillis - lastMqttReconnectAttempt > 5000) {
        lastMqttReconnectAttempt = currentMillis;
        if (reconnectMQTT()) {
          lastMqttReconnectAttempt = 0; // Reset jika berhasil
        }
      }
    } else {
      mqtt.loop();
    }
  }

  // 3. Pembacaan Sensor Realtime (Non-blocking)
  tataKelolaSensor();

  // 4. Pengiriman Data berkala ke MQTT
  if (currentMillis - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = currentMillis;
    kirimDataMQTT();
  }
}

// ═════════════════════════════════════════════════════════════
void tataKelolaSensor() {
  adcGetaran = analogRead(PIN_POT_GETARAN);
  adcArus    = analogRead(PIN_POT_ARUS);
  adcSuhu    = analogRead(PIN_POT_SUHU);

  getaran = map(adcGetaran, 0, 4095, 0, 1000) / 100.0;
  arus    = map(adcArus,    0, 4095, 0, 5000) / 100.0;
  suhu    = map(adcSuhu,    0, 4095, 0, 10000) / 100.0;

  overloadGetaran = (adcGetaran > THRESHOLD_GETARAN);
  overloadArus    = (adcArus > THRESHOLD_ARUS);
  overloadSuhu    = (adcSuhu > THRESHOLD_SUHU);

  digitalWrite(PIN_LED_GETARAN, overloadGetaran ? HIGH : LOW);
  digitalWrite(PIN_LED_ARUS,    overloadArus ? HIGH : LOW);
  digitalWrite(PIN_LED_SUHU,    overloadSuhu ? HIGH : LOW);

  adaAnomali = (overloadGetaran || overloadArus || overloadSuhu);
  
  // Buzzer menyala realtime tanpa delay jika ada anomali
  digitalWrite(PIN_BUZZER, adaAnomali ? HIGH : LOW);
}

// ═════════════════════════════════════════════════════════════
void kirimDataMQTT() {
  StaticJsonDocument<250> doc;
  doc["node"]    = "ESP32_1";
  doc["getaran"] = getaran;
  doc["arus"]    = arus;
  doc["suhu"]    = suhu;
  doc["anomali"] = adaAnomali;
  doc["ts"]      = millis(); // Gunakan waktu real saat dikirim

  char buffer[250];
  serializeJson(doc, buffer);

  if (mqtt.connected() && mqtt.publish(MQTT_TOPIC, buffer)) {
    Serial.print("[MQTT Node 1] Terkirim: ");
    Serial.println(buffer);
  } else {
    Serial.println("[MQTT Node 1] Gagal kirim (Mqtt diskonek)!");
  }
}

// ═════════════════════════════════════════════════════════════
void connectWiFi() {
  Serial.print("[WiFi Node 1] Menghubungkan ke ");
  Serial.println(WIFI_SSID);
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

bool reconnectMQTT() {
  Serial.print("[MQTT Node 1] Menghubungkan...");
  if (mqtt.connect(MQTT_CLIENT)) {
    Serial.println(" OK");
    return true;
  } else {
    Serial.printf(" Gagal (rc=%d)\n", mqtt.state());
    return false;
  }
}
