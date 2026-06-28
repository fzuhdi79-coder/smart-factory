/*
 * ============================================================
 * ESP32 Node 2 – Environment Monitoring (OPTIMIZED & FIXED)
 * Smart Factory Distributed Telemetry System
 * ============================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ── Konfigurasi WiFi & MQTT ───────────────────────────────────
const char* WIFI_SSID   = "Gebang Wetan Anti Gedor 4G";
const char* WIFI_PASS   = "gebangsukses";
const char* MQTT_BROKER = "192.168.1.17";
const int   MQTT_PORT   = 1883;
const char* MQTT_CLIENT = "ESP32_Node2_Environment"; 
const char* MQTT_TOPIC  = "factoryesp2in";

// ── Pin Definitions ───────────────────────────────────────────
#define PIN_POT_SUHU   32
#define PIN_POT_HUMID  33
#define PIN_POT_GAS    34
#define PIN_LED_SUHU   25  
#define PIN_LED_HUMID  26  
#define PIN_LED_GAS    27  
#define PIN_BUZZER     14

// ── Threshold Anomali ─────────────────────────────────────────
#define THRESHOLD_SUHU   3686
#define THRESHOLD_HUMID  3482
#define THRESHOLD_GAS    2785

// ── Objek Global & Timer ──────────────────────────────────────
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

const unsigned long SEND_INTERVAL = 5000;
unsigned long lastSendTime = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastWiFiReconnectAttempt = 0; // <-- Diperbaiki: dipindah ke atas

// Variabel untuk non-blocking buzzer
unsigned long prevBuzzerMillis = 0;
bool buzzerState = false;
int beepCount = 0;
int targetBeep = 0;
unsigned long intervalOn = 150;
unsigned long intervalOff = 100;

// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_SUHU,   OUTPUT);
  pinMode(PIN_LED_HUMID, OUTPUT);
  pinMode(PIN_LED_GAS,   OUTPUT);
  pinMode(PIN_BUZZER,    OUTPUT);
  
  digitalWrite(PIN_LED_SUHU,   LOW);
  digitalWrite(PIN_LED_HUMID, LOW);
  digitalWrite(PIN_LED_GAS,   LOW);
  digitalWrite(PIN_BUZZER,    LOW);

  connectWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
}

// ═════════════════════════════════════════════════════════════
void loop() {
  // Ambil waktu saat ini SATU KALI saja untuk seluruh operasi di dalam loop
  unsigned long currentMillis = millis();

  // 1. Pengecekan WiFi Non-Blocking (Jeda 5 detik)
  if (WiFi.status() != WL_CONNECTED) {
    if (currentMillis - lastWiFiReconnectAttempt > 5000) {
      lastWiFiReconnectAttempt = currentMillis;
      Serial.println("[WiFi] Mencoba menyambungkan kembali...");
      WiFi.disconnect(); 
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
      mqtt.loop(); // Jalankan proses MQTT
    }
  }

  // 3. Jalankan kontrol buzzer secara asinkron
  updateBuzzerAsinkron();

  // 4. Kirim Data setiap SEND_INTERVAL (5 detik)
  if (currentMillis - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = currentMillis;
    readAndSend();
  }
}

// ═════════════════════════════════════════════════════════════
void readAndSend() {
  int adcSuhu  = analogRead(PIN_POT_SUHU);
  int adcHumid = analogRead(PIN_POT_HUMID);
  int adcGas   = analogRead(PIN_POT_GAS);

  float suhu     = map(adcSuhu,  0, 4095, 0, 8000) / 100.0;  
  float humidity = map(adcHumid, 0, 4095, 0, 10000) / 100.0; 
  float gas      = map(adcGas,   0, 4095, 0, 10000) / 100.0; 

  bool overSuhu  = (adcSuhu  > THRESHOLD_SUHU);
  bool overHumid = (adcHumid > THRESHOLD_HUMID);
  bool overGas   = (adcGas   > THRESHOLD_GAS);

  digitalWrite(PIN_LED_SUHU,  overSuhu  ? HIGH : LOW);
  digitalWrite(PIN_LED_HUMID, overHumid ? HIGH : LOW);
  digitalWrite(PIN_LED_GAS,   overGas   ? HIGH : LOW);

  int jumlahOverload = (int)overSuhu + (int)overHumid + (int)overGas;

  // Set target beep berdasarkan overload (Non-blocking trigger)
  if (jumlahOverload == 0) {
    targetBeep = 0;
    digitalWrite(PIN_BUZZER, LOW);
  } else {
    targetBeep = jumlahOverload;
    intervalOn = (jumlahOverload >= 3) ? 400 : 150;
    intervalOff = (jumlahOverload >= 3) ? 150 : 100;
  }

  // Siapkan dan kirim payload JSON
  StaticJsonDocument<256> doc;
  doc["node"]        = "ESP32_2";
  doc["suhu"]        = suhu;
  doc["humidity"]    = humidity;
  doc["gas"]         = gas;
  doc["kebocoran"]   = overGas;
  doc["anomali"]     = (jumlahOverload > 0);
  doc["n_overload"]  = jumlahOverload;
  doc["ts"]          = millis(); // Gunakan waktu real saat dikirim

  char buffer[256];
  serializeJson(doc, buffer);

  if (mqtt.connected()) {
    mqtt.publish(MQTT_TOPIC, buffer);
  }

  Serial.printf("Suhu=%.1f°C | Humid=%.1f%% | Gas=%.1fppm | Overload:%d\n", suhu, humidity, gas, jumlahOverload);
}

// ═════════════════════════════════════════════════════════════
// Logika Buzzer Non-blocking menggunakan State Machine
void updateBuzzerAsinkron() {
  if (targetBeep == 0) {
    digitalWrite(PIN_BUZZER, LOW);
    beepCount = 0;
    buzzerState = false;
    return;
  }

  unsigned long currentMillis = millis();
  
  if (!buzzerState && (beepCount < targetBeep)) {
    if (currentMillis - prevBuzzerMillis >= intervalOff) {
      prevBuzzerMillis = currentMillis;
      buzzerState = true;
      digitalWrite(PIN_BUZZER, HIGH);
    }
  } else if (buzzerState) {
    if (currentMillis - prevBuzzerMillis >= intervalOn) {
      prevBuzzerMillis = currentMillis;
      buzzerState = false;
      digitalWrite(PIN_BUZZER, LOW);
      beepCount++;
      if (beepCount >= targetBeep) {
        // Reset perulangan setelah jeda agar bunyi tidak menyambung terus
        beepCount = 0; 
      }
    }
  }
}

// ═════════════════════════════════════════════════════════════
void connectWiFi() {
  Serial.print("[WiFi] Menghubungkan ke ");
  Serial.println(WIFI_SSID);
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

bool reconnectMQTT() {
  Serial.print("[MQTT] Menghubungkan ulang...");
  if (mqtt.connect(MQTT_CLIENT)) {
    Serial.println(" OK");
    return true;
  }
  Serial.println(" Gagal");
  return false;
}
