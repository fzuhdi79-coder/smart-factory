# Smart Factory Distributed Telemetry System

Sistem telemetri pabrik cerdas berbasis tiga node ESP32 yang terhubung melalui protokol MQTT, dengan data dikirim ke Firebase Realtime Database dan divisualisasikan melalui dashboard web real-time.

---

## Arsitektur Sistem

```
[ESP32 Node 1]          [ESP32 Node 2]
  Machine Monitor  ──┐    Environ Monitor ──┐
  (MQTT Publisher)   │    (MQTT Publisher)  │
                     ▼                      ▼
               [ MQTT Broker — 192.168.1.17:1883 ]
                           │
                    [ESP32 Node 3]
                     Gateway & Control Center
                    (MQTT Subscriber + HTTP Client)
                           │
                    [Firebase RTDB]
                  asia-southeast1
                           │
                   [Dashboard Web]
                  HTML + Chart.js + Firebase SDK
```

---

## Node & Komponen

### ESP32 Node 1 — Machine Monitoring

Membaca tiga parameter kondisi mesin menggunakan potensiometer sebagai simulator sensor.

| Parameter | Pin ADC | Threshold Anomali | Satuan |
|-----------|---------|-------------------|--------|
| Getaran   | GPIO 34 | ADC > 3000 (~7.3) | mm/s   |
| Arus      | GPIO 35 | ADC > 3500 (~42.7)| A      |
| Suhu Mesin| GPIO 32 | ADC > 3200 (~78)  | °C     |

**Output LED Indikator:**
- GPIO 25 → LED Getaran
- GPIO 26 → LED Arus
- GPIO 33 → LED Suhu
- GPIO 27 → Buzzer (aktif langsung saat anomali)

**MQTT Topic:** `factoryesp1in`

**Payload JSON:**
```json
{
  "node": "ESP32_1",
  "getaran": 4.23,
  "arus": 25.10,
  "suhu": 55.00,
  "anomali": false,
  "ts": 123456
}
```

---

### ESP32 Node 2 — Environment Monitoring

Membaca tiga parameter lingkungan. Buzzer dikontrol secara non-blocking menggunakan state machine (jumlah beep = jumlah parameter yang overload).

| Parameter  | Pin ADC | Threshold Anomali | Satuan |
|------------|---------|-------------------|--------|
| Suhu Ruang | GPIO 32 | ADC > 3686 (~72°C)| °C     |
| Kelembaban | GPIO 33 | ADC > 3482 (~85%) | %      |
| Gas / Asap | GPIO 34 | ADC > 2785 (~68)  | 0–100  |

> **Catatan skala gas:** Firmware memetakan ADC ke rentang 0–100 (`map(adc, 0, 4095, 0, 10000) / 100`). Dashboard dan threshold menggunakan skala yang sama.

**Output LED Indikator:**
- GPIO 25 → LED Suhu
- GPIO 26 → LED Kelembaban
- GPIO 27 → LED Gas
- GPIO 14 → Buzzer (non-blocking, jumlah beep = jumlah overload)

**MQTT Topic:** `factoryesp2in`

**Payload JSON:**
```json
{
  "node": "ESP32_2",
  "suhu": 50.00,
  "humidity": 72.00,
  "gas": 30.00,
  "kebocoran": false,
  "anomali": false,
  "n_overload": 0,
  "ts": 123456
}
```

---

### ESP32 Node 3 — Gateway & Control Center

Bertindak sebagai subscriber MQTT dan menggabungkan data dari Node 1 dan Node 2 sebelum dikirim ke Firebase setiap 5 detik.

**Subscribe Topics:** `factoryesp1in`, `factoryesp2in`  
**Publish Topic:** `factoryesp1out` (echo data diterima)

**Alarm Buzzer (Non-blocking State Machine):**
- 2 beep → anomali mesin terdeteksi (Node 1)
- 3 beep → kebocoran gas terdeteksi (Node 2)
- Cooldown 10 detik antar alarm

**Output:**
- GPIO 25 → Buzzer 1
- GPIO 26 → Buzzer 2
- GPIO 32 → LED Merah (error / alarm)
- GPIO 33 → LED Hijau (koneksi MQTT aktif)

**Struktur data Firebase:**
```
factory_data/
  └── {YYYY-MM-DD}/
        └── {HH_MM_SS_millis}/
              ├── waktu_lengkap: "2025-06-28 14:30:05"
              ├── node_machine:
              │     ├── getaran
              │     ├── arus
              │     ├── suhu
              │     └── anomali
              └── node_environment:
                    ├── suhu
                    ├── humidity
                    ├── gas
                    ├── kebocoran
                    └── anomali
```

---

## Konfigurasi Jaringan

| Parameter     | Nilai                    |
|---------------|--------------------------|
| WiFi SSID     | Gebang Wetan Anti Gedor 4G |
| MQTT Broker   | 192.168.1.17             |
| MQTT Port     | 1883                     |
| Firebase RTDB | asia-southeast1 region   |
| NTP Server    | pool.ntp.org             |
| Timezone      | WIB (UTC+7)              |

---

## Threshold Sensor & Status

### Node 1 — Machine

| Sensor      | Warn      | Danger (Anomali) |
|-------------|-----------|------------------|
| Getaran     | ≥ 6.6 mm/s | ≥ 7.3 mm/s     |
| Arus        | ≥ 38.4 A  | ≥ 42.7 A        |
| Suhu Mesin  | ≥ 70.2 °C | ≥ 78.0 °C       |

### Node 2 — Environment

| Sensor      | Warn      | Danger (Anomali) |
|-------------|-----------|------------------|
| Suhu Ruang  | ≥ 64.8 °C | ≥ 72.0 °C       |
| Kelembaban  | ≥ 76.5 %  | ≥ 85.0 %        |
| Gas / Asap  | ≥ 61.2    | ≥ 68.0           |

---

## Dashboard Web

Dashboard berbasis HTML murni dengan Firebase JS SDK v10 dan Chart.js v4.

### Fitur

- **Real-time listener** via `onValue` ke node `factory_data` Firebase RTDB
- **KPI Cards** dengan status NORMAL / WARNING / ANOMALI per parameter
- **6 Chart** (Area, Bar, Step, Gradient, Threshold, Dashed) untuk setiap sensor
- **Node Status** — online/offline otomatis berdasarkan timestamp data (stale > 30 detik = offline)
- **Log Data** — riwayat 30 data terbaru dalam tabel
- **Alarm Center** — riwayat event warning & anomali
- **Test Insert** — injeksi data manual atau preset ke Firebase untuk pengujian

### Cara Menjalankan Dashboard

Buka file `dashboard.html` langsung di browser modern (Chrome, Firefox, Edge). Tidak memerlukan server lokal karena Firebase diakses via CDN.

---

## Library & Dependensi

### Firmware ESP32

| Library       | Fungsi                        |
|---------------|-------------------------------|
| WiFi.h        | Koneksi WiFi                  |
| PubSubClient  | MQTT client                   |
| ArduinoJson   | Serialisasi / deserialisasi JSON |
| HTTPClient    | Upload data ke Firebase REST API |
| time.h        | NTP time sync (Node 3)        |

### Dashboard Web

| Library            | Versi   | Fungsi               |
|--------------------|---------|----------------------|
| Firebase JS SDK    | 10.12.0 | Realtime Database    |
| Chart.js           | 4.4.1   | Visualisasi grafik   |
| Google Fonts       | —       | Inter & JetBrains Mono |

---

## Interval & Timing

| Parameter              | Nilai    |
|------------------------|----------|
| Interval kirim data    | 5 detik  |
| Data dianggap stale    | 15 detik |
| Alarm cooldown         | 10 detik |
| Retry WiFi (non-blocking) | 5 detik |
| Retry MQTT (non-blocking) | 3 detik |
| Node offline threshold (dashboard) | 30 detik |

---

## Alur Data

```
Sensor ADC (Pot)
    │
    ▼
Node 1 / Node 2
  ├─ Baca ADC → konversi ke nilai fisik (map)
  ├─ Cek threshold → nyalakan LED / buzzer
  └─ Publish JSON ke MQTT Broker setiap 5 detik
                      │
                      ▼
               MQTT Broker (Mosquitto)
                      │
                      ▼
              Node 3 (Subscriber)
  ├─ Terima data N1 & N2
  ├─ Trigger alarm buzzer (non-blocking)
  └─ Upload ke Firebase setiap 5 detik (PUT REST API)
                      │
                      ▼
             Firebase Realtime Database
                      │
                      ▼
            Dashboard Web (onValue listener)
  ├─ Parse & validasi data
  ├─ Hitung status (ok / warn / crit)
  ├─ Update KPI cards, chart, log, alarm
  └─ Deteksi node online/offline
```

---

## Struktur File Proyek

```
smart-factory/
├── firmware/
│   ├── machine.ino      # ESP32 Node 1
│   ├── environment.ino  # ESP32 Node 2
│   └── gateway.ino      # ESP32 Node 3
└── dashboard/
    └── index.html       # Dashboard web
```

---

## Catatan Pengembangan

- Seluruh operasi WiFi, MQTT, dan buzzer diimplementasikan secara **non-blocking** menggunakan `millis()` agar ESP32 tidak hang.
- Node 3 hanya mengupload ke Firebase jika ada data baru dari minimal satu node (tidak upload saat idle).
- Threshold gas pada dashboard sebelumnya salah menggunakan skala 250–300, padahal firmware memetakan nilai ke 0–100. Sudah dikoreksi.
- Firebase menggunakan metode `PUT` (bukan `POST`) sehingga path key harus unik — digunakan format `HH_MM_SS_millis4digit`.
