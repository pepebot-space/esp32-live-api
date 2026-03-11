# SPEC.md — ESP32 Live API Client Development Specification

## 1. Overview

This project aims to build an ESP32 firmware that functions as a **voice assistant client** connected to an **LLM Live API** via a WebSocket proxy server (`ws://<host>:18790/v1/live`). The ESP32 captures audio from an INMP441 microphone, sends PCM audio data to the server, and receives audio responses from the LLM to play through a MAX98357 speaker amplifier.

### 1.1 References

| Document | Description |
|----------|-------------|
| `WIRING.md` | Hardware wiring guide for ESP32 + INMP441 + MAX98357 |
| `main.py` | Reference Python client (pyaudio + websockets) |

### 1.2 Target Hardware

| Component | Specification |
|-----------|---------------|
| MCU | ESP32 DevKit (dual-core, 240MHz, 520KB SRAM, 4MB Flash) |
| Microphone | INMP441 I2S MEMS Microphone |
| Amplifier | MAX98357 I2S Audio Amplifier |
| Speaker | 4-8Ω, min 3W |
| Connectivity | WiFi 802.11 b/g/n |

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        CLOUD / SERVER                           │
│                                                                 │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────┐   │
│  │  Gemini /     │◄──►│  Live API    │◄──►│  WebSocket       │   │
│  │  Vertex AI    │    │  Gateway     │    │  Proxy Server    │   │
│  │  (LLM)       │    │  (Pepebot)   │    │  :18790/v1/live  │   │
│  └──────────────┘    └──────────────┘    └───────┬──────────┘   │
│                                                   │              │
└───────────────────────────────────────────────────┼──────────────┘
                                                    │ WebSocket
                                                    │ (JSON + base64 PCM)
                                                    │
┌───────────────────────────────────────────────────┼──────────────┐
│                         ESP32                     │              │
│                                                   │              │
│  ┌──────────┐   ┌──────────┐   ┌─────────────────▼────────┐     │
│  │ INMP441  │──►│ I2S RX   │──►│                          │     │
│  │ (Mic)    │   │ DMA      │   │   Main Application       │     │
│  └──────────┘   └──────────┘   │                          │     │
│                                │  ┌────────────────────┐  │     │
│  ┌──────────┐   ┌──────────┐   │  │ WebSocket Client   │  │     │
│  │ MAX98357 │◄──│ I2S TX   │◄──│  │ (JSON serialize/   │  │     │
│  │ (Speaker)│   │ DMA      │   │  │  deserialize)      │  │     │
│  └──────────┘   └──────────┘   │  └────────────────────┘  │     │
│                                │  ┌────────────────────┐  │     │
│                                │  │ Audio Pipeline     │  │     │
│                                │  │ (ring buffer,      │  │     │
│                                │  │  base64 encode/    │  │     │
│                                │  │  decode)           │  │     │
│                                │  └────────────────────┘  │     │
│                                │  ┌────────────────────┐  │     │
│                                │  │ WiFi Manager       │  │     │
│                                │  └────────────────────┘  │     │
│                                └──────────────────────────┘     │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. WebSocket Communication Protocol

Based on `main.py` analysis, the communication protocol uses **JSON over WebSocket** with audio encoded as **Base64 PCM**.

### 3.1 Connection Flow

```
ESP32                                  Proxy Server
  │                                        │
  │──── WebSocket Connect ────────────────►│
  │                                        │
  │──── Setup Message (JSON) ─────────────►│
  │                                        │
  │◄─── Status: "connected" ──────────────│
  │◄─── setupComplete ────────────────────│
  │                                        │
  │──── realtimeInput (audio chunks) ────►│  ◄── loop
  │◄─── serverContent (audio + text) ─────│  ◄── loop
  │                                        │
  │──── Close ────────────────────────────►│
  │                                        │
```

### 3.2 Message Formats

#### 3.2.1 Setup Message (ESP32 → Server)

```json
{
  "setup": {
    "provider": "vertex",
    "model": "gemini-live-2.5-flash-native-audio",
    "agent": "default",
    "enable_tools": true
  }
}
```

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `provider` | string | LLM provider: `"vertex"`, `"gemini"`, `"openai"` |
| `model` | string | Model identifier |
| `agent` | string | Agent name for system prompt |
| `enable_tools` | bool | Enable function calling |

#### 3.2.2 Audio Input (ESP32 → Server)

```json
{
  "realtimeInput": {
    "mediaChunks": [
      {
        "mimeType": "audio/pcm;rate=16000",
        "data": "<base64-encoded PCM data>"
      }
    ]
  }
}
```

**Audio Specs:**

| Parameter | Value |
|-----------|-------|
| Sample Rate | 16000 Hz |
| Channels | 1 (mono) |
| Bit Depth | 16-bit signed (little-endian) |
| Chunk Size | 2048 samples (4096 bytes raw → ~5464 bytes base64) |
| MIME Type | `audio/pcm;rate=16000` |

#### 3.2.3 Text Input (ESP32 → Server) — Optional

```json
{
  "clientContent": {
    "turns": [
      {
        "role": "user",
        "parts": [{ "text": "Hello" }]
      }
    ],
    "turnComplete": true
  }
}
```

#### 3.2.4 Server Response Messages

**Status Connected:**
```json
{
  "status": "connected",
  "provider": "vertex",
  "model": "gemini-live-2.5-flash-native-audio"
}
```

**Setup Complete:**
```json
{
  "setupComplete": {}
}
```

**Model Turn (Audio + Text):**
```json
{
  "serverContent": {
    "modelTurn": {
      "parts": [
        { "text": "Hello! How can I help you?" },
        {
          "inlineData": {
            "mimeType": "audio/pcm;rate=24000",
            "data": "<base64-encoded PCM audio>"
          }
        }
      ]
    }
  }
}
```

**Error:**
```json
{
  "error": "error message string"
}
```

**Output Audio Specs:**

| Parameter | Value |
|-----------|-------|
| Sample Rate | 24000 Hz |
| Channels | 1 (mono) |
| Bit Depth | 16-bit signed (little-endian) |
| Encoding | Base64 |

**Note:** The server may also send **raw binary PCM** (not JSON). ESP32 must detect whether a message is JSON or raw binary.

---

## 4. Pin Configuration (from WIRING.md)

### 4.1 INMP441 — I2S Microphone (I2S Port 0)

| INMP441 Pin | ESP32 GPIO | Function |
|-------------|------------|----------|
| VDD | 3.3V | Power |
| GND | GND | Ground |
| SD | GPIO 33 | I2S Data In (Serial Data) |
| WS | GPIO 25 | I2S Word Select |
| SCK | GPIO 26 | I2S Serial Clock |
| L/R | GND | Channel Select (Left) |

### 4.2 MAX98357 — I2S Speaker Amplifier (I2S Port 1)

| MAX98357 Pin | ESP32 GPIO | Function |
|--------------|------------|----------|
| VIN | 5V | Power |
| GND | GND | Ground |
| DIN | GPIO 12 | I2S Data Out |
| BCLK | GPIO 27 | I2S Bit Clock |
| LRC | GPIO 14 | I2S Left/Right Clock |
| GAIN | Float | 15dB default |
| SD | Float/GND | Normal operation |

> ⚠️ **Note:** GPIO 12 is used for the speaker (DIN). This pin affects boot mode. Ensure GPIO 12 is LOW during boot, or consider an alternative pin.

---

## 5. Software Design

### 5.1 Framework & Toolchain

| Item | Choice |
|------|--------|
| Framework | Arduino-ESP32 (via PlatformIO) |
| Language | C/C++ (Arduino) |
| Build System | **PlatformIO** |
| WebSocket Library | `ArduinoWebsockets` by Gil Maimon |
| JSON Library | `ArduinoJson` v7 by Benoit Blanchon |
| Base64 Library | `base64` (built-in ESP32 Arduino) or custom |
| Audio Library | ESP-IDF I2S driver (accessible via Arduino-ESP32) |

### 5.2 Task Architecture (FreeRTOS)

```
┌────────────────────────────────────────────────┐
│                FreeRTOS Tasks                  │
│                                                │
│  ┌──────────────┐  ┌────────────────────────┐  │
│  │ wifi_task     │  │ ws_connect_task         │  │
│  │ (Core 0)     │  │ (Core 0)               │  │
│  │ Priority: 5  │  │ Priority: 5            │  │
│  └──────────────┘  └────────────────────────┘  │
│                                                │
│  ┌──────────────┐  ┌────────────────────────┐  │
│  │ mic_task      │  │ ws_send_task            │  │
│  │ (Core 1)     │  │ (Core 0)               │  │
│  │ Priority: 10 │  │ Priority: 8            │  │
│  │              │  │                        │  │
│  │ I2S RX DMA   │  │ Ring Buffer → base64   │  │
│  │ → Ring Buffer│  │ → JSON → WebSocket     │  │
│  └──────────────┘  └────────────────────────┘  │
│                                                │
│  ┌──────────────────────────────────────────┐  │
│  │ ws_recv_task (Core 0, Priority: 8)       │  │
│  │                                          │  │
│  │ WebSocket → JSON parse → base64 decode   │  │
│  │ → Ring Buffer → I2S TX DMA → Speaker     │  │
│  └──────────────────────────────────────────┘  │
│                                                │
│  ┌──────────────┐  ┌────────────────────────┐  │
│  │ button_task   │  │ led_status_task         │  │
│  │ (Core 0)     │  │ (Core 0)               │  │
│  │ Priority: 3  │  │ Priority: 1            │  │
│  └──────────────┘  └────────────────────────┘  │
│                                                │
└────────────────────────────────────────────────┘
```

**Task Descriptions:**

| Task | Core | Priority | Description |
|------|------|----------|-------------|
| `wifi_task` | 0 | 5 | WiFi connection, reconnect, event handling |
| `ws_connect_task` | 0 | 5 | WebSocket lifecycle, setup messaging |
| `mic_task` | 1 | 10 | I2S read from INMP441, write to ring buffer |
| `ws_send_task` | 0 | 8 | Read mic ring buffer, base64 encode, send JSON |
| `ws_recv_task` | 0 | 8 | Receive JSON, parse, base64 decode, write to speaker ring buffer |
| `speaker_task` | 1 | 10 | Read speaker ring buffer, I2S write to MAX98357 |
| `button_task` | 0 | 3 | Control input (mute button, push-to-talk) |
| `led_status_task` | 0 | 1 | Status LED indicator (connecting, listening, speaking) |

### 5.3 Memory Layout

```
Flash (4MB):
├── Bootloader          (~28KB)
├── Partition Table      (~3KB)
├── NVS (WiFi config)   (~24KB)
├── Application          (~1.5MB)
└── OTA Partition        (~1.5MB)

SRAM (520KB):
├── FreeRTOS Kernel      (~20KB)
├── WiFi/TCP/IP Stack   (~60KB)
├── TLS (mbedTLS)        (~40KB)
├── I2S DMA Buffers
│   ├── Mic RX (4 x 2048 bytes)    (~8KB)
│   └── Speaker TX (8 x 2048 bytes) (~16KB)
├── Ring Buffers
│   ├── Mic Ring Buffer              (~16KB)
│   └── Speaker Ring Buffer          (~32KB)
├── WebSocket TX Buffer   (~8KB)
├── WebSocket RX Buffer   (~16KB)
├── JSON + Base64 Buffers (~12KB)
├── Task Stacks
│   ├── mic_task (4KB)
│   ├── ws_send_task (8KB)
│   ├── ws_recv_task (12KB)
│   ├── speaker_task (4KB)
│   └── Others (12KB)
└── Heap (free)           (~270KB)
```

> ⚠️ **Memory Critical:** ESP32 only has ~520KB SRAM. Large WebSocket frames from the server (audio responses) should be parsed in a streaming fashion when possible.

### 5.4 Audio Pipeline Detail

```
INPUT PIPELINE:
═══════════════

INMP441 ──► I2S RX DMA ──► mic_ring_buffer ──► ws_send_task
                              (16KB)              │
                                                  ▼
                                         ┌─────────────────┐
                                         │ 1. Read 2048     │
                                         │    samples (4KB) │
                                         │ 2. Base64 encode │
                                         │    (~5.4KB)      │
                                         │ 3. Build JSON    │
                                         │ 4. WS send       │
                                         └─────────────────┘

OUTPUT PIPELINE:
════════════════

ws_recv_task ──► spk_ring_buffer ──► I2S TX DMA ──► MAX98357 ──► Speaker
     │              (32KB)
     ▼
┌─────────────────┐
│ 1. WS recv msg  │
│ 2. Detect JSON  │
│    vs raw binary│
│ 3. JSON parse   │
│ 4. Extract      │
│    inlineData   │
│ 5. Base64 decode│
│ 6. Write to     │
│    ring buffer  │
└─────────────────┘
```

### 5.5 I2S Configuration

#### Microphone (I2S_NUM_0) — Input

```c
i2s_config_t i2s_mic_config = {
    .mode = I2S_MODE_MASTER | I2S_MODE_RX,
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,     // samples per DMA buffer
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0,
};

i2s_pin_config_t i2s_mic_pins = {
    .bck_io_num   = 26,  // SCK
    .ws_io_num    = 25,  // WS
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = 33,  // SD
};
```

#### Speaker (I2S_NUM_1) — Output

```c
i2s_config_t i2s_spk_config = {
    .mode = I2S_MODE_MASTER | I2S_MODE_TX,
    .sample_rate = 24000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0,
};

i2s_pin_config_t i2s_spk_pins = {
    .bck_io_num   = 27,  // BCLK
    .ws_io_num    = 14,  // LRC
    .data_out_num = 12,  // DIN
    .data_in_num  = I2S_PIN_NO_CHANGE,
};
```

---

## 6. State Machine

```
                    ┌───────────┐
                    │   BOOT    │
                    └─────┬─────┘
                          │
                          ▼
                    ┌───────────┐     WiFi Fail
              ┌────│ WIFI_CONN │────────────────┐
              │    └─────┬─────┘                │
              │          │ WiFi OK               │
              │          ▼                       ▼
              │    ┌───────────┐          ┌───────────┐
              │    │ WS_CONN   │          │ WIFI_RETRY│
              │    └─────┬─────┘          └───────────┘
              │          │ WS Connected
              │          ▼
              │    ┌───────────┐
              │    │ WS_SETUP  │──── Send setup JSON
              │    └─────┬─────┘
              │          │ setupComplete received
              │          ▼
              │    ┌───────────┐
              ├────│ LISTENING │◄──────────────────┐
              │    └─────┬─────┘                   │
              │          │ Audio Detected /         │
              │          │ Button Press             │
              │          ▼                         │
              │    ┌───────────┐                   │
              │    │ STREAMING │── Send audio ──►   │
              │    └─────┬─────┘                   │
              │          │ Server response          │
              │          ▼                         │
              │    ┌───────────┐                   │
              │    │ PLAYING   │── Play audio ─────┘
              │    └─────┬─────┘
              │          │ Error / Disconnect
              │          ▼
              │    ┌───────────┐
              └───►│ RECONNECT │
                   └───────────┘
```

### State Descriptions

| State | LED Pattern | Description |
|-------|-------------|-------------|
| `BOOT` | Solid White | Hardware initialization |
| `WIFI_CONN` | Blinking Blue | Connecting to WiFi |
| `WIFI_RETRY` | Blinking Red | Retrying WiFi connection |
| `WS_CONN` | Blinking Cyan | Connecting WebSocket |
| `WS_SETUP` | Solid Cyan | Sending setup, waiting for setupComplete |
| `LISTENING` | Solid Green | Ready to receive voice input |
| `STREAMING` | Pulsing Yellow | Sending audio to server |
| `PLAYING` | Pulsing Purple | Playing audio response |
| `RECONNECT` | Blinking Orange | Reconnecting after error |
| `ERROR` | Solid Red | Fatal error |

---

## 7. Project Structure (PlatformIO)

```
esp32-live-api/
├── platformio.ini                    # PlatformIO project config
├── partitions.csv                    # Custom partition table (optional)
├── SPEC.md
├── WIRING.md
├── main.py                           # Reference Python client
│
├── src/
│   └── main.cpp                      # Entry point: setup() + loop()
│
├── include/
│   ├── config.h                      # Pin definitions, constants
│   └── credentials.h                 # WiFi & server credentials (gitignored)
│
├── lib/
│   ├── WiFiManager/
│   │   ├── wifi_manager.h
│   │   └── wifi_manager.cpp          # WiFi connect, reconnect, event handler
│   │
│   ├── AudioPipeline/
│   │   ├── i2s_mic.h
│   │   ├── i2s_mic.cpp               # INMP441 I2S RX setup and task
│   │   ├── i2s_speaker.h
│   │   ├── i2s_speaker.cpp           # MAX98357 I2S TX setup and task
│   │   ├── audio_pipeline.h
│   │   └── audio_pipeline.cpp        # Ring buffers, audio routing
│   │
│   ├── WebSocketLive/
│   │   ├── ws_client.h
│   │   ├── ws_client.cpp             # WebSocket connection, lifecycle
│   │   ├── ws_protocol.h
│   │   └── ws_protocol.cpp           # JSON build/parse, protocol handler
│   │
│   ├── Base64Codec/
│   │   ├── base64_codec.h
│   │   └── base64_codec.cpp          # Base64 encode/decode wrapper
│   │
│   ├── StateMachine/
│   │   ├── state_machine.h
│   │   └── state_machine.cpp         # Application state management
│   │
│   └── LedIndicator/
│       ├── led_indicator.h
│       └── led_indicator.cpp         # Status LED patterns
│
├── data/                              # SPIFFS data (optional, for web config)
│
└── test/                              # PlatformIO unit tests
    └── test_base64/
        └── test_base64.cpp
```

### 7.1 `platformio.ini`

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

; Serial monitor
monitor_speed = 115200
monitor_filters = esp32_exception_decoder

; Upload
upload_speed = 921600

; Build flags
build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DBOARD_HAS_PSRAM=0
    -DARDUINO_EVENT_RUNNING_CORE=0
    -DARDUINO_RUNNING_CORE=1

; Library dependencies
lib_deps =
    gilmaimon/ArduinoWebsockets@^0.5.4
    bblanchon/ArduinoJson@^7.3.0

; Partition table (use default huge_app if OTA not needed)
board_build.partitions = huge_app.csv

; Flash size
board_build.flash_size = 4MB
```

---

## 8. Module Implementation Details

### 8.1 `include/config.h` — Global Configuration

```cpp
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <driver/i2s.h>

// ─── WiFi (credentials in credentials.h) ───
#define WIFI_MAX_RETRY       10
#define WIFI_RETRY_DELAY_MS  5000

// ─── WebSocket ─────────────────────────────
#define WS_URI           "ws://192.168.1.100:18790/v1/live"
#define WS_BUFFER_SIZE   (20 * 1024)   // 20KB max frame
#define WS_RECONNECT_MS  3000

// ─── Audio Input (INMP441) ─────────────────
#define I2S_MIC_PORT     I2S_NUM_0
#define I2S_MIC_RATE     16000
#define I2S_MIC_WS       25
#define I2S_MIC_SCK      26
#define I2S_MIC_SD       33
#define MIC_CHUNK_SAMPLES 2048   // matches main.py INPUT_CHUNK
#define MIC_CHUNK_BYTES  (MIC_CHUNK_SAMPLES * 2)  // 16-bit = 2 bytes

// ─── Audio Output (MAX98357) ───────────────
#define I2S_SPK_PORT     I2S_NUM_1
#define I2S_SPK_RATE     24000
#define I2S_SPK_BCLK     27
#define I2S_SPK_LRC      14
#define I2S_SPK_DOUT     12

// ─── Ring Buffers ──────────────────────────
#define MIC_RING_BUF_SIZE    (16 * 1024)   // 16KB
#define SPK_RING_BUF_SIZE    (32 * 1024)   // 32KB

// ─── Base64 ────────────────────────────────
#define BASE64_ENCODE_BUF    ((MIC_CHUNK_BYTES * 4 / 3) + 4 + 1)
#define JSON_TX_BUF_SIZE     (BASE64_ENCODE_BUF + 256)  // JSON overhead

// ─── Protocol ──────────────────────────────
#define INPUT_MIME       "audio/pcm;rate=16000"
#define WS_PROVIDER      "vertex"
#define WS_MODEL         "gemini-live-2.5-flash-native-audio"
#define WS_AGENT         "default"

// ─── LED (optional, built-in LED) ──────────
#define LED_PIN          2

#endif // CONFIG_H
```

### 8.1.1 `include/credentials.h` — WiFi & Server Credentials

> ⚠️ **This file must be `.gitignore`d!** Create from template `credentials.h.example`.

```cpp
#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#define WIFI_SSID     "YourWiFiSSID"
#define WIFI_PASS     "YourWiFiPassword"

// Override WS_URI if different from default in config.h
// #define WS_URI     "ws://192.168.1.200:18790/v1/live"

#endif // CREDENTIALS_H
```

### 8.2 `lib/WiFiManager/wifi_manager` — WiFi Management

**Responsibilities:**
- Initialize WiFi STA mode via Arduino WiFi library
- Event-driven connect/reconnect
- Expose status via callback and polling

**Key Functions:**

```cpp
void wifi_manager_init(const char *ssid, const char *password);
void wifi_manager_connect();
bool wifi_manager_is_connected();
void wifi_manager_wait_connected();  // blocking
void wifi_manager_loop();            // call in loop() or task
```

**Implementation Notes:**
- Use `WiFi.begin()`, `WiFi.status()`, `WiFi.onEvent()` (Arduino WiFi API)
- `WIFI_EVENT_STA_DISCONNECTED` → auto reconnect with backoff
- Credentials from `credentials.h` (compile-time)
- Set hostname: `WiFi.setHostname("esp32-live-api")`

### 8.3 `lib/AudioPipeline/i2s_mic` — Microphone Input

**Responsibilities:**
- Configure I2S RX for INMP441 (via ESP-IDF `driver/i2s.h` accessible from Arduino)
- FreeRTOS Task: read I2S DMA → write to mic ring buffer
- Simple gain adjustment (bit shifting)

**Key Functions:**

```cpp
bool i2s_mic_init();
void i2s_mic_start(RingbufHandle_t ring_buf);
void i2s_mic_stop();
```

**Task Pseudocode:**

```cpp
void mic_task(void *param) {
    int16_t buffer[MIC_CHUNK_SAMPLES];
    size_t bytes_read;
    
    while (true) {
        esp_err_t err = i2s_read(I2S_MIC_PORT, buffer, MIC_CHUNK_BYTES,
                                  &bytes_read, portMAX_DELAY);
        
        if (err == ESP_OK && bytes_read > 0) {
            // Optional: apply gain
            for (int i = 0; i < bytes_read / 2; i++) {
                int32_t sample = buffer[i] << 2;  // 12dB gain
                // Clamp to prevent overflow
                if (sample > 32767) sample = 32767;
                if (sample < -32768) sample = -32768;
                buffer[i] = (int16_t)sample;
            }
            
            xRingbufferSend(mic_ring_buf, buffer,
                           bytes_read, pdMS_TO_TICKS(100));
        }
    }
}
```

### 8.4 `lib/AudioPipeline/i2s_speaker` — Speaker Output

**Responsibilities:**
- Configure I2S TX for MAX98357 (via ESP-IDF `driver/i2s.h`)
- FreeRTOS Task: read from speaker ring buffer → write to I2S DMA

**Key Functions:**

```cpp
bool i2s_speaker_init();
void i2s_speaker_start(RingbufHandle_t ring_buf);
void i2s_speaker_stop();
```

**Task Pseudocode:**

```cpp
void speaker_task(void *param) {
    size_t item_size;
    size_t bytes_written;
    
    while (true) {
        void *item = xRingbufferReceive(spk_ring_buf,
                                         &item_size,
                                         pdMS_TO_TICKS(50));
        if (item != NULL) {
            i2s_write(I2S_SPK_PORT, item, item_size,
                      &bytes_written, portMAX_DELAY);
            vRingbufferReturnItem(spk_ring_buf, item);
        } else {
            // No data → write silence to prevent pop noise
            int16_t silence[256] = {0};
            i2s_write(I2S_SPK_PORT, silence, sizeof(silence),
                      &bytes_written, pdMS_TO_TICKS(10));
        }
    }
}
```

### 8.5 `lib/WebSocketLive/ws_client` — WebSocket Lifecycle

**Responsibilities:**
- Connect to proxy server via `ArduinoWebsockets` library
- Handle connect/disconnect/error events via callbacks
- Provide send/receive interface to protocol layer

**Key Functions:**

```cpp
#include <ArduinoWebsockets.h>
using namespace websockets;

class WsClient {
public:
    bool init(const char *uri);
    bool connect();
    bool sendText(const String &json_str);
    void disconnect();
    bool isConnected();
    void loop();   // call in FreeRTOS task — polls incoming messages

    // Callbacks
    void onMessage(std::function<void(WebsocketsMessage)> cb);
    void onEvent(std::function<void(WebsocketsEvent, String)> cb);

private:
    WebsocketsClient _client;
    String _uri;
    bool _connected = false;
};
```

**Events to Handle:**

| Event | Action |
|-------|--------|
| `WebsocketsEvent::ConnectionOpened` | Set state → WS_SETUP, send setup message |
| `onMessage` callback | Parse incoming data, route to protocol handler |
| `WebsocketsEvent::ConnectionClosed` | Set state → RECONNECT, trigger reconnect |
| `WebsocketsEvent::GotPing` | Auto-reply pong (handled by library) |

### 8.6 `lib/WebSocketLive/ws_protocol` — Protocol Handler

**Responsibilities:**
- Build JSON messages (setup, realtimeInput, clientContent) via `ArduinoJson`
- Parse JSON responses (serverContent, setupComplete, error)
- Base64 encode audio data for sending
- Base64 decode audio data from responses
- Detect raw binary vs JSON messages

**Key Functions:**

```cpp
#include <ArduinoJson.h>

// ─── Send functions ────────────────────────
String ws_protocol_build_setup();
String ws_protocol_build_audio_input(const uint8_t *pcm, size_t len);
String ws_protocol_build_text_input(const char *text);

// ─── Receive / Parse ───────────────────────
enum WsMsgType {
    MSG_STATUS_CONNECTED,
    MSG_SETUP_COMPLETE,
    MSG_AUDIO_DATA,
    MSG_TEXT_DATA,
    MSG_ERROR,
    MSG_RAW_BINARY,
    MSG_UNKNOWN,
};

struct WsParsedMsg {
    WsMsgType type;
    // For audio
    uint8_t *pcm_data = nullptr;
    size_t   pcm_len = 0;
    // For text
    String text;
    // For error
    String error_message;

    void cleanup() {
        if (pcm_data) { free(pcm_data); pcm_data = nullptr; }
    }
};

WsParsedMsg ws_protocol_parse(const char *data, size_t len, bool is_binary);
```

**Parse Logic (from main.py):**

```
1. If message is binary:
   a. Try to parse as JSON
   b. If fails → treat as raw PCM → send to speaker ring buffer
   c. If succeeds → process as JSON (continue to step 2)

2. If message is JSON (text or parsed binary):
   a. Check "status" == "connected" → log, update state
   b. Check "setupComplete" → update state to LISTENING
   c. Check "error" → log error
   d. Check "serverContent" → {
        - Extract "modelTurn" → "parts" array
        - For each part:
          - If has "text" → log (optional display)
          - If has "inlineData"."data" → base64 decode → speaker ring buffer
      }
```

### 8.7 `lib/Base64Codec/base64_codec` — Base64 Wrapper

```cpp
#include <base64.h>  // ESP32 Arduino built-in

// Encode raw PCM bytes to base64 String
String base64_encode_audio(const uint8_t *input, size_t input_len);

// Decode base64 string to raw bytes
// ⚠️ Handle URL-safe base64: replace '-' with '+', '_' with '/'
// ⚠️ Add padding '=' if needed
// Returns: number of bytes written to output
size_t base64_decode_audio(const char *input, size_t input_len,
                           uint8_t *output, size_t output_buf_size);

// Helper: convert URL-safe base64 to standard base64
String url_safe_to_standard_b64(const String &input);
```

> **Important:** The server uses URL-safe base64 (see `decode_base64_audio` in `main.py`). ESP32 must handle conversion of `-` → `+` and `_` → `/` before decoding. Use `base64.h` built into Arduino-ESP32 or `mbedtls_base64_encode/decode`.

---

## 9. Challenges & Edge Cases

### 9.1 Memory Constraints

| Challenge | Solution |
|-----------|----------|
| Large JSON responses (audio data) | Parse in streaming fashion, extract base64 chunk by chunk |
| Base64 encoded audio expands ~33% | Preallocate fixed buffer, decode in-place |
| Multiple audio parts in a single response | Process sequentially, don't buffer everything at once |
| WebSocket frame size | Set `buffer_size` in WebSocket config = 20KB |

### 9.2 Audio Timing

| Challenge | Solution |
|-----------|----------|
| Mic and speaker use different sample rates (16K vs 24K) | Use separate I2S ports (I2S_NUM_0 and I2S_NUM_1) |
| Audio dropout when network is busy | Speaker ring buffer 32KB (~0.67 seconds @ 24kHz) |
| Latency too high | Send audio chunks immediately (don't batch), minimize JSON overhead |
| Echo (speaker audio entering mic) | Simple implementation: mute mic while playing |

### 9.3 Network Reliability

| Challenge | Solution |
|-----------|----------|
| WiFi disconnect | Auto reconnect with exponential backoff |
| WebSocket disconnect | Auto reconnect, re-send setup message |
| Slow network | Ring buffer absorbs jitter, timeout on send |
| Server unavailable | Retry with backoff, LED indicator for user |

### 9.4 Concurrency & Thread Safety

| Resource | Protection |
|----------|-----------|
| Mic Ring Buffer | FreeRTOS Ring Buffer (thread-safe by design) |
| Speaker Ring Buffer | FreeRTOS Ring Buffer |
| WebSocket send | Mutex, only one task sends |
| Application State | Atomic variable or mutex |

---

## 10. Configuration (PlatformIO Build Flags)

Since we use PlatformIO + Arduino, configuration is done via:
1. **`include/credentials.h`** — WiFi credentials (gitignored)
2. **`include/config.h`** — Pin definitions and constants
3. **`platformio.ini` build_flags** — Compile-time overrides

### Override via `platformio.ini`

```ini
; Example: override config without editing .h files
build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DWIFI_SSID='"MyNetwork"'
    -DWIFI_PASS='"MyPassword"'
    -DWS_URI='"ws://192.168.1.200:18790/v1/live"'
    -DWS_PROVIDER='"vertex"'
    -DWS_MODEL='"gemini-live-2.5-flash-native-audio"'
    -DWS_AGENT='"default"'
    -DMIC_GAIN_SHIFT=2
```

### Environment Variants

```ini
; Development (verbose logging)
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
build_flags =
    -DCORE_DEBUG_LEVEL=4
    -DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE
lib_deps =
    gilmaimon/ArduinoWebsockets@^0.5.4
    bblanchon/ArduinoJson@^7.3.0
board_build.partitions = huge_app.csv

; Production (minimal logging)
[env:esp32prod]
platform = espressif32
board = esp32dev
framework = arduino
build_flags =
    -DCORE_DEBUG_LEVEL=1
    -Os
lib_deps =
    gilmaimon/ArduinoWebsockets@^0.5.4
    bblanchon/ArduinoJson@^7.3.0
board_build.partitions = huge_app.csv
```

---

## 11. Development Phases

### Phase 1: Foundation (Week 1)

**Goal:** Set up PlatformIO project, WiFi, and I2S audio loopback.

- [ ] Set up PlatformIO project (`platformio.ini`, `src/main.cpp`, `include/`, `lib/`)
- [ ] Create `credentials.h.example` and `.gitignore` for `credentials.h`
- [ ] Implement `wifi_manager` (connect, reconnect via Arduino WiFi)
- [ ] Implement `i2s_mic` (INMP441 I2S RX, read data to buffer)
- [ ] Implement `i2s_speaker` (MAX98357 I2S TX, play from buffer)
- [ ] **Test:** Audio loopback — mic directly to speaker (no network)
- [ ] Verify wiring per `WIRING.md`

**Deliverable:** ESP32 can capture audio from mic and play to speaker (loopback).

---

### Phase 2: WebSocket + Protocol (Week 2)

**Goal:** WebSocket connection and protocol messaging.

- [ ] Implement `ws_client` (connect, disconnect, event handling)
- [ ] Implement `ws_protocol` — build setup message JSON
- [ ] Implement `ws_protocol` — parse server response JSON
- [ ] Implement `base64_codec` (encode/decode with URL-safe handling)
- [ ] **Test:** Connect to proxy server, send setup, receive `setupComplete`
- [ ] **Test:** Send dummy audio (silent PCM), verify server receives it

**Deliverable:** ESP32 can connect to proxy and complete handshake successfully.

---

### Phase 3: Full Audio Streaming (Week 3)

**Goal:** Full duplex audio streaming to Live API.

- [ ] Integrate mic task → ring buffer → ws_send_task (base64 + JSON)
- [ ] Integrate ws_recv_task → parse audio → ring buffer → speaker task
- [ ] Handle raw binary PCM from server
- [ ] Handle mixed text + audio parts in `serverContent`
- [ ] Implement simple echo suppression (mute mic while playing)
- [ ] **Test:** End-to-end voice conversation with LLM

**Deliverable:** Voice conversation works: speak → LLM responds via speaker.

---

### Phase 4: Robustness & Polish (Week 4)

**Goal:** Error handling, reconnect, and UX polish.

- [ ] Implement `state_machine` with all states
- [ ] Implement `led_indicator` for status feedback
- [ ] WiFi reconnect with exponential backoff
- [ ] WebSocket auto reconnect
- [ ] Memory optimization and leak testing
- [ ] Optional: Push-to-talk button support
- [ ] Optional: Volume control via button
- [ ] **Test:** Stress test — long conversation, network interruption, recovery
- [ ] **Test:** Memory monitoring via `esp_get_free_heap_size()`

**Deliverable:** Production-ready firmware that is robust and user-friendly.

---

## 12. Build & Flash Commands (PlatformIO)

```bash
# Install PlatformIO CLI (if not already installed)
pip install platformio

# Or install PlatformIO IDE extension in VS Code

# ─── Build ──────────────────────────────────
pio run                         # Build default env
pio run -e esp32dev             # Build specific env

# ─── Upload (Flash) ─────────────────────────
pio run -t upload               # Build + Flash
pio run -e esp32dev -t upload   # Flash specific env

# ─── Serial Monitor ─────────────────────────
pio device monitor              # Monitor serial output
pio device monitor -b 115200    # With baud rate

# ─── Build + Flash + Monitor (single command) 
pio run -t upload && pio device monitor

# ─── Clean Build ─────────────────────────────
pio run -t clean
pio run -t cleanall             # Clean everything including dependencies

# ─── List serial ports ──────────────────────
pio device list

# ─── Unit Tests ──────────────────────────────
pio test -e esp32dev            # Run unit tests

# ─── Update dependencies ────────────────────
pio pkg update                  # Update all libraries
```

> **VS Code shortcut:** Install the "PlatformIO IDE" extension → use the PlatformIO sidebar for one-click Build/Upload/Monitor.

---

## 13. Dependencies

### PlatformIO Libraries (`lib_deps`)

| Library | Version | Usage |
|---------|---------|-------|
| `gilmaimon/ArduinoWebsockets` | ^0.5.4 | WebSocket client (ws:// and wss://) |
| `bblanchon/ArduinoJson` | ^7.3.0 | JSON serialize/deserialize |

### Arduino-ESP32 Built-in (no installation needed)

| Component | Usage |
|-----------|-------|
| `WiFi.h` | WiFi STA connection |
| `driver/i2s.h` | I2S audio input/output (ESP-IDF driver via Arduino) |
| `freertos/FreeRTOS.h` | Tasks, ring buffers, mutexes, semaphores |
| `freertos/ringbuf.h` | Ring buffer for audio pipeline |
| `base64.h` | Base64 encode/decode |
| `esp_system.h` | `esp_get_free_heap_size()`, restart, etc. |

---

## 14. Testing Checklist

### Unit Tests

- [ ] Base64 encode/decode with URL-safe characters
- [ ] JSON build: setup message format correct
- [ ] JSON build: realtimeInput message format correct
- [ ] JSON parse: serverContent with audio data
- [ ] JSON parse: serverContent with text data
- [ ] JSON parse: setupComplete message
- [ ] JSON parse: error message
- [ ] Ring buffer write/read integrity

### Integration Tests

- [ ] WiFi connect → disconnect → reconnect
- [ ] WebSocket connect → setup → setupComplete
- [ ] Audio loopback: mic → buffer → speaker (no network)
- [ ] Send audio chunk → receive response
- [ ] Full conversation cycle
- [ ] Reconnect after server restart
- [ ] Reconnect after WiFi drop

### Performance Metrics

| Metric | Target |
|--------|--------|
| Boot to ready | < 5 seconds |
| Audio latency (mic to server) | < 200ms |
| Audio latency (server to speaker) | < 200ms |
| Free heap during operation | > 100KB |
| WiFi reconnect time | < 10 seconds |
| WS reconnect time | < 5 seconds |

---

## 15. Risks & Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| Insufficient SRAM for buffers | High | Optimize buffer sizes, streaming JSON parse |
| GPIO 12 boot issue | Medium | Use alternative pin or efuse disable flash voltage |
| WebSocket frame too large | High | Set max frame size, chunked processing |
| Poor audio quality | Medium | Tune gain, filter DC offset, proper grounding |
| High latency | Medium | Minimize buffer sizes, prioritize audio tasks |
| WiFi interference | Low | Use 5GHz if ESP32 supports it, or reduce distance |

---

## 16. References

- [PlatformIO ESP32 Platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)
- [Arduino-ESP32 Documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
- [ArduinoWebsockets Library](https://github.com/gilmaimon/ArduinoWebsockets)
- [ArduinoJson v7 Documentation](https://arduinojson.org/v7/)
- [ESP-IDF I2S Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html)
- [INMP441 Datasheet](https://invensense.tdk.com/products/digital/inmp441/)
- [MAX98357 Datasheet](https://www.maximintegrated.com/en/products/analog/audio/MAX98357A.html)
- [Gemini Live API Protocol](https://ai.google.dev/gemini-api/docs/live)
- [FreeRTOS Ring Buffer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos_additions.html#ring-buffers)
