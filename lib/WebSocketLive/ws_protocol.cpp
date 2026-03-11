#include "ws_protocol.h"
#include "base64_codec.h"
#include "config.h"
#include <cstring>
#include <mbedtls/base64.h>

String ws_protocol_build_setup() {
  JsonDocument doc;
  JsonObject setup = doc["setup"].to<JsonObject>();
  setup["provider"] = WS_PROVIDER;
  setup["model"] = WS_MODEL;
  setup["agent"] = WS_AGENT;
  setup["enable_tools"] = true;

  String output;
  serializeJson(doc, output);
  return output;
}

String ws_protocol_build_audio_input(const String &b64_data) {
  // Manually construct JSON to avoid ArduinoJson heap allocation for large data
  String output;
  output.reserve(b64_data.length() + 100);
  output += "{\"realtimeInput\":{\"mediaChunks\":[{\"mimeType\":\"";
  output += INPUT_MIME;
  output += "\",\"data\":\"";
  output += b64_data;
  output += "\"}]}}";
  return output;
}

String ws_protocol_build_text_input(const String &text) {
  JsonDocument doc;
  JsonObject clientContent = doc["clientContent"].to<JsonObject>();
  JsonArray turns = clientContent["turns"].to<JsonArray>();

  JsonObject turn1 = turns.add<JsonObject>();
  turn1["role"] = "user";
  JsonArray parts = turn1["parts"].to<JsonArray>();

  JsonObject part1 = parts.add<JsonObject>();
  part1["text"] = text;

  clientContent["turnComplete"] = true;

  String output;
  serializeJson(doc, output);
  return output;
}

static const size_t MAX_PCM_DECODE_BYTES = 32 * 1024;

static const char *find_token(const char *data, size_t len, const char *token) {
  if (!data || !token)
    return nullptr;
  size_t token_len = strlen(token);
  if (token_len == 0 || len < token_len)
    return nullptr;

  for (size_t i = 0; i + token_len <= len; i++) {
    if (memcmp(data + i, token, token_len) == 0) {
      return data + i;
    }
  }
  return nullptr;
}

static const char *skip_ws(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
    p++;
  }
  return p;
}

static bool extract_quoted_value(const char *data, size_t len, const char *key,
                                 const char **out_start, size_t *out_len) {
  const char *match = find_token(data, len, key);
  if (!match)
    return false;

  const char *end = data + len;
  const char *p = match + strlen(key);
  p = skip_ws(p, end);
  if (p >= end || *p != ':')
    return false;

  p++;
  p = skip_ws(p, end);
  if (p >= end || *p != '"')
    return false;

  p++;
  const char *value_start = p;
  while (p < end && *p != '"') {
    p++;
  }

  if (p >= end)
    return false;

  *out_start = value_start;
  *out_len = static_cast<size_t>(p - value_start);
  return true;
}

// ─── Lightweight parser using strstr instead of ArduinoJson ───
// This avoids allocating a huge JsonDocument for large audio responses.
WsParsedMsg ws_protocol_parse(const char *data, size_t len, bool is_binary) {
  WsParsedMsg result;
  result.type = MSG_UNKNOWN;

  if (data == nullptr || len == 0) {
    return result;
  }

  // Fast-path: binary message that isn't JSON
  if (is_binary && data[0] != '{') {
    result.type = MSG_RAW_BINARY;
    result.pcm_len = len;
    result.pcm_data = (uint8_t *)malloc(result.pcm_len);
    if (result.pcm_data) {
      memcpy(result.pcm_data, data, result.pcm_len);
    }
    return result;
  }

  // ─── Small message fast-paths using strstr (no JSON parsing needed) ───

  // Check for status: connected
  if (find_token(data, len, "\"status\"") &&
      find_token(data, len, "\"connected\"")) {
    result.type = MSG_STATUS_CONNECTED;
    return result;
  }

  // Check for setupComplete
  if (find_token(data, len, "\"setupComplete\"")) {
    result.type = MSG_SETUP_COMPLETE;
    return result;
  }

  // Check for error
  const char *err_key = find_token(data, len, "\"error\"");
  if (err_key && !find_token(data, len, "\"serverContent\"")) {
    // Extract error message (simple extraction)
    result.type = MSG_ERROR;
    const char *colon = strchr(err_key, ':');
    if (colon) {
      const char *quote1 = strchr(colon, '"');
      if (quote1) {
        quote1++;
        const char *quote2 = strchr(quote1, '"');
        if (quote2) {
          result.error_message = String(quote1).substring(0, quote2 - quote1);
        }
      }
    }
    return result;
  }

  // ─── Check for text data ───
  const char *text_value_start = nullptr;
  size_t text_value_len = 0;
  if (find_token(data, len, "\"serverContent\"") &&
      extract_quoted_value(data, len, "\"text\"", &text_value_start,
                           &text_value_len) &&
      text_value_len < 2048) {
    result.type = MSG_TEXT_DATA;
    result.text = String(text_value_start).substring(0, text_value_len);
    // Don't return yet — same message might also contain audio
  }

  // ─── Check for inlineData audio ───
  const char *audio_value_start = nullptr;
  size_t audio_value_len = 0;
  if (find_token(data, len, "\"inlineData\"") &&
      extract_quoted_value(data, len, "\"data\"", &audio_value_start,
                           &audio_value_len)) {
    size_t padded_b64_len = audio_value_len;
    while (padded_b64_len % 4 != 0) {
      padded_b64_len++;
    }

    char *norm_b64 = (char *)malloc(padded_b64_len + 1);
    if (!norm_b64) {
      Serial.printf("OOM normalize b64 (%u bytes)\n", padded_b64_len + 1);
      return result;
    }

    for (size_t i = 0; i < audio_value_len; i++) {
      char c = audio_value_start[i];
      if (c == '-')
        c = '+';
      else if (c == '_')
        c = '/';
      norm_b64[i] = c;
    }
    while (audio_value_len < padded_b64_len) {
      norm_b64[audio_value_len++] = '=';
    }
    norm_b64[audio_value_len] = '\0';

    size_t decoded_capacity = (audio_value_len * 3 / 4) + 4;
    if (decoded_capacity > MAX_PCM_DECODE_BYTES) {
      Serial.printf("Audio decoded too large, dropped (%u bytes)\n",
                    decoded_capacity);
      free(norm_b64);
      return result;
    }

    result.pcm_data = (uint8_t *)malloc(decoded_capacity);
    if (!result.pcm_data) {
      Serial.printf("OOM decode target (%u bytes)\n", decoded_capacity);
      free(norm_b64);
      return result;
    }

    size_t decoded_len = 0;
    int ret = mbedtls_base64_decode(result.pcm_data, decoded_capacity,
                                    &decoded_len,
                                    (const unsigned char *)norm_b64,
                                    audio_value_len);
    free(norm_b64);

    if (ret != 0 || decoded_len == 0) {
      Serial.printf("Base64 decode failed: %d\n", ret);
      free(result.pcm_data);
      result.pcm_data = nullptr;
      result.pcm_len = 0;
      return result;
    }

    result.pcm_len = decoded_len;
    result.type = MSG_AUDIO_DATA;
  }

  return result;
}
