#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <driver/i2s.h>

// ─── WiFi ───────────────────────────────────
#define WIFI_MAX_RETRY 10
#define WIFI_RETRY_DELAY_MS 5000
#define SETUP_AP_SSID "PEPEBOT-SETUP"

// ─── WebSocket ─────────────────────────────
#define WS_URI "ws://192.168.1.100:18790/v1/live"
#define WS_BUFFER_SIZE (20 * 1024) // 20KB max frame
#define WS_RECONNECT_MS 3000
#define WS_WATCHDOG_WINDOW_MS 60000
#define WS_WATCHDOG_WARN_THRESHOLD 3
#define WS_WATCHDOG_WIFI_RESET_THRESHOLD 6
#define WS_WATCHDOG_REBOOT_THRESHOLD 10

// ─── Audio Input (INMP441) ─────────────────
#define I2S_MIC_PORT I2S_NUM_0
#define I2S_MIC_RATE 16000
#define I2S_MIC_WS 25
#define I2S_MIC_SCK 26
#define I2S_MIC_SD 33
#define MIC_CHUNK_SAMPLES 1024
#define MIC_CHUNK_BYTES (MIC_CHUNK_SAMPLES * 2) // 16-bit = 2 bytes

// ─── Audio Output (MAX98357) ───────────────
#define I2S_SPK_PORT I2S_NUM_1
#define I2S_SPK_RATE 24000
#define I2S_SPK_BCLK 27
#define I2S_SPK_LRC 14
#define I2S_SPK_DOUT 12

// ─── Ring Buffers ──────────────────────────
#define MIC_RING_BUF_SIZE (8 * 1024)  // 8KB
#define SPK_RING_BUF_SIZE (16 * 1024) // 16KB

// ─── Base64 ────────────────────────────────
#define BASE64_ENCODE_BUF ((MIC_CHUNK_BYTES * 4 / 3) + 4 + 1)
#define JSON_TX_BUF_SIZE (BASE64_ENCODE_BUF + 256) // JSON overhead
#define MIC_TX_QUEUE_LEN 4
#define STREAMING_TO_PROCESSING_MS 350

// ─── Protocol ──────────────────────────────
#define INPUT_MIME "audio/pcm;rate=16000"
#define WS_PROVIDER "vertex"
#define WS_MODEL "gemini-live-2.5-flash-native-audio"
#define WS_AGENT "default"
#define DEFAULT_INITIAL_PROMPT "Halo, perkenalkan diri kamu secara singkat dalam bahasa Indonesia."

// ─── LED (optional, built-in LED) ──────────
#define LED_PIN 2

#endif // CONFIG_H
