#include "runtime_config.h"
#include "config.h"
#include <Preferences.h>

static Preferences _prefs;
static RuntimeConfig _cfg;

static void apply_defaults(RuntimeConfig &cfg) {
  if (cfg.ws_uri.length() == 0) {
    cfg.ws_uri = WS_URI;
  }
  if (cfg.initial_prompt.length() == 0) {
    cfg.initial_prompt = DEFAULT_INITIAL_PROMPT;
  }
}

void runtime_config_init() {
  _prefs.begin("appcfg", false);
  _cfg.wifi_ssid = _prefs.getString("wifi_ssid", "");
  _cfg.wifi_pass = _prefs.getString("wifi_pass", "");
  _cfg.ws_uri = _prefs.getString("ws_uri", "");
  _cfg.initial_prompt = _prefs.getString("init_prompt", "");
  _prefs.end();

  apply_defaults(_cfg);
  Serial.printf("[CFG] loaded ssid=%s ws=%s promptLen=%u\n",
                _cfg.wifi_ssid.length() ? _cfg.wifi_ssid.c_str() : "(empty)",
                _cfg.ws_uri.c_str(), (unsigned)_cfg.initial_prompt.length());
}

const RuntimeConfig &runtime_config_get() { return _cfg; }

bool runtime_config_save(const RuntimeConfig &cfg) {
  RuntimeConfig normalized = cfg;
  normalized.wifi_ssid.trim();
  normalized.ws_uri.trim();
  normalized.initial_prompt.trim();
  apply_defaults(normalized);

  _prefs.begin("appcfg", false);
  bool ok = true;
  ok = ok && _prefs.putString("wifi_ssid", normalized.wifi_ssid) > 0;
  ok = ok && _prefs.putString("wifi_pass", normalized.wifi_pass) >= 0;
  ok = ok && _prefs.putString("ws_uri", normalized.ws_uri) > 0;
  ok = ok && _prefs.putString("init_prompt", normalized.initial_prompt) >= 0;
  _prefs.end();

  if (ok) {
    _cfg = normalized;
    Serial.println("[CFG] saved to NVS");
  } else {
    Serial.println("[CFG] failed saving to NVS");
  }
  return ok;
}

bool runtime_config_has_wifi() { return _cfg.wifi_ssid.length() > 0; }
