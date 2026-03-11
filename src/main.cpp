#include "audio_pipeline.h"
#include "base64_codec.h"
#include "config.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "wifi_manager.h"
#include "ws_client.h"
#include "ws_protocol.h"
#include <Arduino.h>
#include <new>

// If no credentials.h is present, fallback to defaults
#if __has_include("credentials.h")
#include "credentials.h"
#else
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASS "YourWiFiPassword"
#endif

// Global WebSocket Client
WsClient ws;

// Application State
enum AppState {
  STATE_BOOT,
  STATE_WIFI_CONN,
  STATE_WS_CONN,
  STATE_WS_SETUP,
  STATE_LISTENING,
  STATE_STREAMING,
  STATE_PLAYING,
  STATE_ERROR
};
volatile AppState current_state = STATE_BOOT;

static const char *stateName(AppState s) {
  switch (s) {
  case STATE_BOOT:
    return "BOOT";
  case STATE_WIFI_CONN:
    return "WIFI_CONN";
  case STATE_WS_CONN:
    return "WS_CONN";
  case STATE_WS_SETUP:
    return "WS_SETUP";
  case STATE_LISTENING:
    return "LISTENING";
  case STATE_STREAMING:
    return "STREAMING";
  case STATE_PLAYING:
    return "PLAYING";
  case STATE_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

static void setState(AppState next, const char *reason) {
  if (current_state == next) {
    return;
  }
  Serial.printf("[STATE] %s -> %s | reason=%s\n", stateName(current_state),
                stateName(next), reason ? reason : "-");
  current_state = next;
}

// Simple "echo suppression" - mute mic while speaker is active
volatile bool is_playing_audio = false;

static void flushRingBuffer(RingbufHandle_t rb) {
  if (!rb)
    return;

  while (true) {
    size_t item_size = 0;
    void *item = xRingbufferReceive(rb, &item_size, 0);
    if (!item)
      break;
    vRingbufferReturnItem(rb, item);
  }
}

// ─── WebSocket Event Handler ─────────────────
void handleWsEvent(WebsocketsEvent event, String data) {
  Serial.printf("[WS EVENT] type=%d data=%s\n", (int)event, data.c_str());

  if (event == WebsocketsEvent::ConnectionOpened) {
    is_playing_audio = false;
    flushRingBuffer(spk_ring_buf);
    Serial.println("WS Connected! Sending Setup...");
    setState(STATE_WS_SETUP, "ws connection opened");

    String setup_json = ws_protocol_build_setup();
    Serial.printf("[WS TX] setup payload bytes=%u\n", setup_json.length());
    ws.sendText(setup_json);

  } else if (event == WebsocketsEvent::ConnectionClosed) {
    is_playing_audio = false;
    flushRingBuffer(spk_ring_buf);
    Serial.println("WS Disconnected!");
    setState(STATE_WS_CONN, "ws closed, reconnect");
  }
}

// ─── WebSocket Message parsing ─────────────────
void handleWsMessage(const WebsocketsMessage &msg) {
  static uint32_t rx_count = 0;
  rx_count++;
  Serial.printf("[WS RX] #%lu len=%u binary=%d\n", (unsigned long)rx_count,
                msg.length(), msg.isBinary() ? 1 : 0);

  WsParsedMsg parsed =
      ws_protocol_parse(msg.c_str(), msg.length(), msg.isBinary());

  Serial.printf("[WS PARSE] type=%d textLen=%u audioLen=%u\n", (int)parsed.type,
                parsed.text.length(), parsed.pcm_len);

  switch (parsed.type) {
  case MSG_STATUS_CONNECTED:
    Serial.println("Server Status: connected");
    break;

  case MSG_SETUP_COMPLETE:
    Serial.println("Setup Complete! Ready to stream.");
    is_playing_audio = false;
    flushRingBuffer(mic_ring_buf);
    flushRingBuffer(spk_ring_buf);
    setState(STATE_LISTENING, "server setupComplete");
    break;

  case MSG_TEXT_DATA:
    Serial.printf("LLM: %s\n", parsed.text.c_str());
    break;

  case MSG_AUDIO_DATA:
  case MSG_RAW_BINARY:
    if (parsed.pcm_data && parsed.pcm_len > 0) {
      is_playing_audio = true;
      setState(STATE_PLAYING, "incoming audio frame");

      if (spk_ring_buf) {
        BaseType_t ok = xRingbufferSend(spk_ring_buf, parsed.pcm_data,
                                        parsed.pcm_len, pdMS_TO_TICKS(500));
        Serial.printf("[SPK BUF] enqueue len=%u ok=%d\n", parsed.pcm_len,
                      (int)ok);
      }
    }
    break;

  case MSG_ERROR:
    Serial.printf("Server ERROR: %s\n", parsed.error_message.c_str());
    break;

  default:
    break;
  }

  parsed.cleanup();
}

// ─── Inline mic send (called from loop, same thread as ws.loop) ───
// Pre-allocate a static send buffer to avoid repeated heap allocs
static char _send_buf[JSON_TX_BUF_SIZE];

void processMicAndSend() {
  static uint32_t tx_frames = 0;
  static uint32_t tx_drops = 0;

  if (is_playing_audio) {
    flushRingBuffer(mic_ring_buf);
    static unsigned long last_suppressed_log = 0;
    if (millis() - last_suppressed_log > 1500) {
      last_suppressed_log = millis();
      Serial.println("[MIC] suppressed while speaker active");
    }
    return;
  }

  // Only process if we're in an active state
  if (current_state != STATE_LISTENING && current_state != STATE_STREAMING &&
      current_state != STATE_PLAYING) {
    return;
  }

  size_t item_size;
  void *item = xRingbufferReceive(mic_ring_buf, &item_size, 0); // Non-blocking

  if (item == NULL) {
    return;
  }

  if (current_state == STATE_LISTENING) {
    setState(STATE_STREAMING, "first mic chunk ready");
  }

  // Check heap before doing any allocations
  if (ESP.getFreeHeap() < 30000) {
    Serial.printf("Low heap: %u bytes, skipping send\n", ESP.getFreeHeap());
    vRingbufferReturnItem(mic_ring_buf, item);
    tx_drops++;
    return;
  }

  // Base64 encode into String (heap-allocated, but we free it quickly)
  String b64 = base64_encode_audio((uint8_t *)item, item_size);
  vRingbufferReturnItem(mic_ring_buf, item); // Return ring buf item ASAP

  if (b64.length() == 0 || !ws.isConnected()) {
    tx_drops++;
    return;
  }

  // Build JSON directly into static buffer to avoid stack/heap String pressure
  int len = snprintf(_send_buf, sizeof(_send_buf),
                     "{\"realtimeInput\":{\"mediaChunks\":[{\"mimeType\":\"%"
                     "s\",\"data\":\"%s\"}]}}",
                     INPUT_MIME, b64.c_str());

  b64 = ""; // Free b64 String immediately

  if (len > 0 && len < (int)sizeof(_send_buf)) {
    bool ok = ws.sendRaw(_send_buf, len);
    tx_frames++;
    if (!ok) {
      tx_drops++;
      Serial.printf("[WS TX] sendRaw failed frame=%lu\n",
                    (unsigned long)tx_frames);
    }

    if (tx_frames % 25 == 0) {
      Serial.printf("[MIC TX] frames=%lu drops=%lu heap=%u\n",
                    (unsigned long)tx_frames, (unsigned long)tx_drops,
                    ESP.getFreeHeap());
    }
  } else {
    tx_drops++;
    Serial.printf("[WS TX] json build overflow len=%d cap=%u\n", len,
                  sizeof(_send_buf));
  }
}

// ─── Setup ──────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n--- ESP32 Live API Client ---");
  Serial.println("Phase 3: Full Audio Streaming");

  // Init modules
  wifi_manager_init(WIFI_SSID, WIFI_PASS);

  if (!audio_pipeline_init() || !i2s_mic_init() || !i2s_speaker_init()) {
    Serial.println("HAL init failed!");
    while (true)
      delay(1000);
  }

  // Start Audio Hardware (mic and speaker tasks run on their own cores)
  i2s_mic_start(mic_ring_buf);
  i2s_speaker_start(spk_ring_buf);

  // Setup WebSocket Callbacks
  ws.onEvent(handleWsEvent);
  ws.onMessage(handleWsMessage);

  current_state = STATE_WIFI_CONN;
  Serial.printf("[BOOT] initial state=%s\n", stateName(current_state));

  Serial.printf("Free heap after init: %u bytes\n", ESP.getFreeHeap());
}

// ─── Main Loop ───────────────────────────────
void loop() {
  // 1. WiFi Management
  wifi_manager_loop();

  bool wifi_ok = wifi_manager_is_connected();

  if (wifi_ok && current_state == STATE_WIFI_CONN) {
    setState(STATE_WS_CONN, "wifi connected");
  }

  // 2. WebSocket Connection Management
  if (wifi_ok && !ws.isConnected() &&
      (current_state == STATE_WS_CONN || current_state == STATE_WIFI_CONN)) {
    static unsigned long last_ws_attempt = 0;
    if (millis() - last_ws_attempt > WS_RECONNECT_MS) {
      last_ws_attempt = millis();

#ifdef WS_URI_OVERRIDE
      ws.init(WS_URI_OVERRIDE);
#else
      ws.init(WS_URI);
#endif

      ws.connect();
      Serial.printf("[WS] connect attempt done, connected=%d\n",
                    ws.isConnected() ? 1 : 0);
    }
  }

  // 3. Keep WebSocket alive & process incoming msgs
  if (ws.isConnected()) {
    try {
      ws.loop();
    } catch (const std::bad_alloc &) {
      Serial.printf("WebSocket OOM while polling (free heap=%u), reconnecting...\n",
                    ESP.getFreeHeap());
      ws.disconnect();
      setState(STATE_WS_CONN, "ws poll oom");
    } catch (...) {
      Serial.println("WebSocket exception while polling, reconnecting...");
      ws.disconnect();
      setState(STATE_WS_CONN, "ws poll exception");
    }
  }

  // 4. Read mic ring buffer and send audio via WS (same thread = thread-safe)
  if (ws.isConnected()) {
    processMicAndSend();
  }

  // 5. Update speaker status flag for echo suppression
  if (current_state == STATE_PLAYING) {
    static unsigned long _last_play_time = 0;
    UBaseType_t items_in_queue;
    vRingbufferGetInfo(spk_ring_buf, NULL, NULL, NULL, NULL, &items_in_queue);

    if (items_in_queue > 0) {
      _last_play_time = millis();
    } else if (millis() - _last_play_time > 1000) {
      is_playing_audio = false;
      setState(STATE_LISTENING, "speaker queue drained");
      Serial.println("Finished playing, resuming listening.");
    }
  }

  static unsigned long last_heartbeat = 0;
  if (millis() - last_heartbeat > 2000) {
    last_heartbeat = millis();
    UBaseType_t mic_items = 0;
    UBaseType_t spk_items = 0;
    if (mic_ring_buf) {
      vRingbufferGetInfo(mic_ring_buf, NULL, NULL, NULL, NULL, &mic_items);
    }
    if (spk_ring_buf) {
      vRingbufferGetInfo(spk_ring_buf, NULL, NULL, NULL, NULL, &spk_items);
    }
    Serial.printf("[HB] state=%s wifi=%d ws=%d playing=%d mic_items=%u spk_items=%u heap=%u\n",
                  stateName(current_state), wifi_ok ? 1 : 0,
                  ws.isConnected() ? 1 : 0, is_playing_audio ? 1 : 0,
                  (unsigned)mic_items, (unsigned)spk_items, ESP.getFreeHeap());
  }

  delay(5); // Short delay, keep loop responsive
}
