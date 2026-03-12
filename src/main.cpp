#include "audio_pipeline.h"
#include "config.h"
#include "display_ui.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "runtime_config.h"
#include "wifi_manager.h"
#include "ws_client.h"
#include "ws_protocol.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>

WsClient ws;

enum AppState {
  STATE_BOOT,
  STATE_WIFI_CONN,
  STATE_WS_CONN,
  STATE_WS_SETUP,
  STATE_LISTENING,
  STATE_STREAMING,
  STATE_PROCESSING,
  STATE_PLAYING,
  STATE_ERROR
};
volatile AppState current_state = STATE_BOOT;

typedef struct {
  uint16_t len;
  uint8_t pcm[MIC_CHUNK_BYTES];
} MicTxChunk;

static QueueHandle_t mic_tx_queue = NULL;
static TaskHandle_t mic_tx_task_handle = NULL;

static volatile bool is_playing_audio = false;
static volatile bool ws_need_reinit = true;
static volatile uint32_t tx_frames = 0;
static volatile uint32_t tx_drops = 0;
static volatile uint32_t tx_queue_drops = 0;
static volatile uint32_t ws_disconnect_count = 0;
static volatile uint32_t ws_watchdog_window_start_ms = 0;
static volatile uint32_t last_mic_activity_ms = 0;
static volatile bool initial_prompt_sent = false;

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
  case STATE_PROCESSING:
    return "PROCESSING";
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

static void flushMicTxQueue() {
  if (!mic_tx_queue)
    return;
  MicTxChunk tmp;
  while (xQueueReceive(mic_tx_queue, &tmp, 0) == pdTRUE) {
  }
}

static void onWsDisconnectWatchdog() {
  uint32_t now = millis();
  if (ws_watchdog_window_start_ms == 0 ||
      now - ws_watchdog_window_start_ms > WS_WATCHDOG_WINDOW_MS) {
    ws_watchdog_window_start_ms = now;
    ws_disconnect_count = 1;
  } else {
    ws_disconnect_count++;
  }

  Serial.printf("[WS WD] disconnects=%lu windowMs=%u\n",
                (unsigned long)ws_disconnect_count,
                (unsigned)(now - ws_watchdog_window_start_ms));

  if (ws_disconnect_count == WS_WATCHDOG_WARN_THRESHOLD) {
    Serial.println("[WS WD] warning threshold reached");
  }

  if (ws_disconnect_count == WS_WATCHDOG_WIFI_RESET_THRESHOLD) {
    Serial.println("[WS WD] forcing wifi reconnect");
    ws_need_reinit = true;
    wifi_manager_connect();
    setState(STATE_WIFI_CONN, "watchdog wifi reconnect");
  }

  if (ws_disconnect_count >= WS_WATCHDOG_REBOOT_THRESHOLD) {
    Serial.println("[WS WD] too many disconnects, rebooting");
    delay(200);
    ESP.restart();
  }
}

static void micTxTask(void *param) {
  static char b64_buf[BASE64_ENCODE_BUF];
  static char send_buf[JSON_TX_BUF_SIZE];
  MicTxChunk chunk;

  while (true) {
    if (xQueueReceive(mic_tx_queue, &chunk, pdMS_TO_TICKS(50)) != pdTRUE) {
      continue;
    }

    AppState s = current_state;
    if (!(s == STATE_LISTENING || s == STATE_STREAMING ||
          s == STATE_PROCESSING) ||
        !ws.isConnected() || is_playing_audio) {
      tx_drops++;
      continue;
    }

    size_t b64_len = 0;
    int b64_ret = mbedtls_base64_encode(
        (unsigned char *)b64_buf, sizeof(b64_buf) - 1, &b64_len,
        (const unsigned char *)chunk.pcm, chunk.len);
    if (b64_ret != 0 || b64_len == 0) {
      tx_drops++;
      Serial.printf("[MIC TX] b64 encode failed err=%d input=%u\n", b64_ret,
                    (unsigned)chunk.len);
      continue;
    }

    b64_buf[b64_len] = '\0';
    int len = snprintf(send_buf, sizeof(send_buf),
                       "{\"realtimeInput\":{\"mediaChunks\":[{\"mimeType\":\"%"
                       "s\",\"data\":\"%s\"}]}}",
                       INPUT_MIME, b64_buf);
    if (len <= 0 || len >= (int)sizeof(send_buf)) {
      tx_drops++;
      Serial.printf("[WS TX] json build overflow len=%d cap=%u\n", len,
                    (unsigned)sizeof(send_buf));
      continue;
    }

    bool ok = ws.sendRaw(send_buf, (size_t)len);
    tx_frames++;
    if (!ok) {
      tx_drops++;
      Serial.printf("[WS TX] sendRaw failed frame=%lu\n",
                    (unsigned long)tx_frames);
    }
  }
}

static void pumpMicToTxQueue() {
  if (!ws.isConnected())
    return;

  AppState s = current_state;
  if (!(s == STATE_LISTENING || s == STATE_STREAMING ||
        s == STATE_PROCESSING)) {
    return;
  }

  size_t item_size = 0;
  void *item = xRingbufferReceive(mic_ring_buf, &item_size, 0);
  if (!item) {
    return;
  }

  MicTxChunk out;
  size_t copy_len = item_size;
  if (copy_len > sizeof(out.pcm)) {
    copy_len = sizeof(out.pcm);
  }
  out.len = (uint16_t)copy_len;
  memcpy(out.pcm, item, copy_len);
  vRingbufferReturnItem(mic_ring_buf, item);

  if (xQueueSend(mic_tx_queue, &out, 0) != pdTRUE) {
    tx_queue_drops++;
    if (tx_queue_drops % 25 == 0) {
      Serial.printf("[MIC TX] queue full drops=%lu\n",
                    (unsigned long)tx_queue_drops);
    }
    return;
  }

  last_mic_activity_ms = millis();
  if (current_state == STATE_LISTENING || current_state == STATE_PROCESSING) {
    setState(STATE_STREAMING, "mic chunk queued");
  }
}

void handleWsEvent(WebsocketsEvent event, String data) {
  Serial.printf("[WS EVENT] type=%d data=%s\n", (int)event, data.c_str());

  if (event == WebsocketsEvent::ConnectionOpened) {
    is_playing_audio = false;
    ws_need_reinit = false;
    initial_prompt_sent = false;
    ws_disconnect_count = 0;
    ws_watchdog_window_start_ms = millis();
    flushRingBuffer(spk_ring_buf);
    flushMicTxQueue();
    Serial.println("WS Connected! Sending Setup...");
    setState(STATE_WS_SETUP, "ws connection opened");

    String setup_json = ws_protocol_build_setup();
    Serial.printf("[WS TX] setup payload bytes=%u\n", setup_json.length());
    ws.sendText(setup_json);
  } else if (event == WebsocketsEvent::ConnectionClosed) {
    is_playing_audio = false;
    flushRingBuffer(spk_ring_buf);
    flushMicTxQueue();
    Serial.println("WS Disconnected!");
    initial_prompt_sent = false;
    onWsDisconnectWatchdog();
    setState(STATE_WS_CONN, "ws closed, reconnect");
  }
}

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

  case MSG_SETUP_COMPLETE: {
    Serial.println("Setup Complete! Ready to stream.");
    is_playing_audio = false;
    flushRingBuffer(mic_ring_buf);
    flushRingBuffer(spk_ring_buf);
    flushMicTxQueue();
    setState(STATE_LISTENING, "server setupComplete");

    const RuntimeConfig &cfg = runtime_config_get();
    if (!initial_prompt_sent && cfg.initial_prompt.length() > 0) {
      String prompt_json = ws_protocol_build_text_input(cfg.initial_prompt);
      bool ok = ws.sendText(prompt_json);
      Serial.printf("[WS TX] initial prompt bytes=%u ok=%d\n",
                    prompt_json.length(), ok ? 1 : 0);
      initial_prompt_sent = ok;
      if (ok) {
        setState(STATE_PROCESSING, "initial prompt sent");
      }
    }
    break;
  }

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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n--- ESP32 Live API Client ---");
  Serial.println("Phase 4: queue + processing + watchdog + ui");

  display_ui_init();
  wifi_manager_init();

  if (!audio_pipeline_init() || !i2s_mic_init() || !i2s_speaker_init()) {
    Serial.println("HAL init failed!");
    while (true)
      delay(1000);
  }

  i2s_mic_start(mic_ring_buf);
  i2s_speaker_start(spk_ring_buf);

  mic_tx_queue = xQueueCreate(MIC_TX_QUEUE_LEN, sizeof(MicTxChunk));
  if (!mic_tx_queue) {
    Serial.println("[BOOT] failed to create mic_tx_queue");
    while (true)
      delay(1000);
  }

  xTaskCreatePinnedToCore(micTxTask, "mic_tx_task", 4096, NULL, 4,
                          &mic_tx_task_handle, 0);

  ws.onEvent(handleWsEvent);
  ws.onMessage(handleWsMessage);

  current_state = STATE_WIFI_CONN;
  ws_watchdog_window_start_ms = millis();
  Serial.printf("[BOOT] initial state=%s\n", stateName(current_state));
  Serial.printf("[BOOT] free heap=%u maxblk=%u\n", ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void loop() {
  display_ui_loop();
  wifi_manager_loop();
  bool wifi_ok = wifi_manager_is_connected();

  if (wifi_ok && current_state == STATE_WIFI_CONN) {
    setState(STATE_WS_CONN, "wifi connected");
  }

  if (wifi_ok && !ws.isConnected() &&
      (current_state == STATE_WS_CONN || current_state == STATE_WIFI_CONN)) {
    static unsigned long last_ws_attempt = 0;
    if (millis() - last_ws_attempt > WS_RECONNECT_MS) {
      last_ws_attempt = millis();

      if (ws_need_reinit) {
        ws.init(runtime_config_get().ws_uri.c_str());
        ws_need_reinit = false;
      }

      bool connected = ws.connect();
      Serial.printf("[WS] connect attempt connected=%d reinit=%d\n",
                    connected ? 1 : 0, ws_need_reinit ? 1 : 0);
    }
  }

  if (ws.isConnected()) {
    try {
      ws.loop();
    } catch (const std::bad_alloc &) {
      Serial.printf("[WS] OOM while polling heap=%u\n", ESP.getFreeHeap());
      ws_need_reinit = true;
      ws.disconnect();
      setState(STATE_WS_CONN, "ws poll oom");
    } catch (...) {
      Serial.println("[WS] exception while polling, reconnecting");
      ws_need_reinit = true;
      ws.disconnect();
      setState(STATE_WS_CONN, "ws poll exception");
    }
  }

  if (!ws.isConnected() &&
      (current_state == STATE_STREAMING || current_state == STATE_PROCESSING ||
       current_state == STATE_PLAYING || current_state == STATE_WS_SETUP)) {
    flushMicTxQueue();
    flushRingBuffer(mic_ring_buf);
    setState(STATE_WS_CONN, "ws lost during active state");
  }

  if (current_state == STATE_PLAYING || current_state == STATE_WS_SETUP ||
      current_state == STATE_WS_CONN || current_state == STATE_WIFI_CONN) {
    flushRingBuffer(mic_ring_buf);
    flushMicTxQueue();
  } else {
    pumpMicToTxQueue();
  }

  if (current_state == STATE_STREAMING) {
    uint32_t now = millis();
    if (last_mic_activity_ms != 0 &&
        now - last_mic_activity_ms > STREAMING_TO_PROCESSING_MS) {
      setState(STATE_PROCESSING, "waiting server response");
    }
  }

  if (current_state == STATE_PLAYING) {
    static unsigned long last_play_time = 0;
    UBaseType_t items_in_queue = 0;
    vRingbufferGetInfo(spk_ring_buf, NULL, NULL, NULL, NULL, &items_in_queue);

    if (items_in_queue > 0) {
      last_play_time = millis();
    } else if (millis() - last_play_time > 1000) {
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
    UBaseType_t tx_q_items = 0;
    if (mic_ring_buf) {
      vRingbufferGetInfo(mic_ring_buf, NULL, NULL, NULL, NULL, &mic_items);
    }
    if (spk_ring_buf) {
      vRingbufferGetInfo(spk_ring_buf, NULL, NULL, NULL, NULL, &spk_items);
    }
    if (mic_tx_queue) {
      tx_q_items = uxQueueMessagesWaiting(mic_tx_queue);
    }

    Serial.printf(
        "[HB] state=%s wifi=%d ws=%d playing=%d mic_q=%u tx_q=%u spk_q=%u "
        "tx=%lu txDrop=%lu qDrop=%lu wd=%lu heap=%u maxblk=%u\n",
        stateName(current_state), wifi_ok ? 1 : 0, ws.isConnected() ? 1 : 0,
        is_playing_audio ? 1 : 0, (unsigned)mic_items, (unsigned)tx_q_items,
        (unsigned)spk_items, (unsigned long)tx_frames, (unsigned long)tx_drops,
        (unsigned long)tx_queue_drops, (unsigned long)ws_disconnect_count,
        ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  }

  delay(5);
}
