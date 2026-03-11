# ESP32 Live API Client (Pepebot)

Proyek ini adalah firmware ESP32 untuk melakukan streaming audio dua arah (Input & Output) ke server WebSocket (Pepebot Live API). Proyek ini menangkap suara dari microphone I2S, mengirimkannya lewat WebSocket, dan memutar audio balasan dari server melalui speaker I2S.

## Kebutuhan Perangkat Keras (Hardware Tools)

1. **ESP32 Development Board** (NodeMCU ESP-32S atau sejenisnya)
2. **Microphone I2S (INMP441)** - Untuk merekam suara
3. **I2S Amplifier (MAX98357A atau UDA1334A)** - Untuk memutar suara ke speaker
4. **Speaker Mini** (misal: 3W 4Ohm)
5. Kabel Jumper secukupnya

## Kebutuhan Perangkat Lunak (Software Tools)

1. **Visual Studio Code (VSCode)**
2. **PlatformIO IDE** (Extention di VSCode)
3. Driver USB ke Serial (CP210x atau CH340, tergantung board ESP32 Anda)

## Skema Rangkaian (Pinout ESP32)

Berikut adalah referensi pinout ESP32 (DevKit V1):
![ESP32 Pinout](assets/dev_kit_pinout.jpg)

Berdasarkan konfigurasi pada `include/config.h`, berikut adalah cara menyambungkan komponen ke ESP32:

### 1. Microphone INMP441 (Audio Input)
| Pin INMP441 | Pin ESP32 | Keterangan |
| :---: | :---: | :--- |
| **VDD** | 3.3V | Power (Gunakan 3.3V, jangan 5V) |
| **GND** | GND | Ground |
| **L/R** | GND | Channel Selection (Low = Left Channel) |
| **WS (Word Select)** | GPIO 25 | Left/Right Clock |
| **SCK (Serial Clock)** | GPIO 26 | Bit Clock |
| **SD (Serial Data)** | GPIO 33 | Data Audio Keluar |

### 2. Output Audio / Amplifier MAX98357A (Audio Output)
| Pin MAX98357A | Pin ESP32 | Keterangan |
| :---: | :---: | :--- |
| **VIN** | 5V / 3.3V | Power supply |
| **GND** | GND | Ground |
| **LRC (Word Select)** | GPIO 14 | Left/Right Clock |
| **BCLK (Bit Clock)** | GPIO 27 | Serial Clock |
| **DIN (Data In)** | GPIO 12 | Data Audio Masuk |
| **SD / SD_MODE**| VDD / Float | Biarkan kosong atau hubungkan ke VDD (untuk channel mix/right) |
| **Speaker + / -** | ke Speaker | Sambungkan ke terminal speaker |


## Cara Build, Upload, dan Monitoring

Pastikan PlatformIO sudah terinstall di VSCode Anda dan ESP32 sudah terhubung ke komputer via kabel USB.

### 1. Konfigurasi WiFi dan Server
Sebelum melakukan upload, edit atau buat file `include/credentials.h` terlebih dahulu dan masukkan pengaturan WiFi serta alamat WebSocket server:
```cpp
#define WIFI_SSID "NAMA_WIFI_ANDA"
#define WIFI_PASS "PASSWORD_WIFI_ANDA"
#define WS_URI "ws://ALAMAT_IP_SERVER:18790/v1/live"
```

### 2. Cara Build (Compile)
Untuk melakukan kompilasi kodingan tanpa mengupload:
- Klik icon **Tanda Centang (✓)** di status bar bawah VSCode (PlatformIO: Build).
- Atau buka terminal dan jalankan perintah:
  ```bash
  pio run
  ```

### 3. Cara Upload (Flash)
Untuk mengupload firmware ke ESP32:
- Klik icon **Tanda Panah Kanan (→)** di status bar bawah VSCode (PlatformIO: Upload).
- Atau jalankan perintah di terminal:
  ```bash
  pio run -t upload
  ```

### 4. Cara Monitoring (Serial Monitor)
Untuk melihat log dan pesan error dari ESP32 (kecepatan baudrate 115200):
- Klik icon **Colokan Listrik / Steker** di status bar bawah VSCode (PlatformIO: Serial Monitor).
- Atau jalankan perintah di terminal:
  ```bash
  pio device monitor -b 115200
  ```

### 5. Cara Cepat Cepat (Build, Upload, dan Monitor sekaligus)
Gunakan perintah ini di terminal bawaan PlatformIO/VSCode untuk melakukan ketiganya sekaligus secara berurutan:
```bash
pio run -t upload && pio device monitor -b 115200
```
