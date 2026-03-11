#ifndef WS_PROTOCOL_H
#define WS_PROTOCOL_H

#include <Arduino.h>
#include <ArduinoJson.h>

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
  size_t pcm_len = 0;
  // For text
  String text;
  // For error
  String error_message;

  void cleanup() {
    if (pcm_data) {
      free(pcm_data);
      pcm_data = nullptr;
    }
  }
};

// ─── Send functions ────────────────────────
String ws_protocol_build_setup();
String ws_protocol_build_audio_input(const String &b64_data);
String ws_protocol_build_text_input(const String &text);

// ─── Receive / Parse ───────────────────────
WsParsedMsg ws_protocol_parse(const char *data, size_t len, bool is_binary);

#endif // WS_PROTOCOL_H
