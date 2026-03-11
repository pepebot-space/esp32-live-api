#include "wifi_manager.h"
#include "config.h"
#include "runtime_config.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>

static unsigned long _last_reconnect_attempt = 0;
static uint32_t _wifi_reconnect_count = 0;
static bool _ap_mode = false;
static WebServer _server(80);
static DNSServer _dns;

static bool is_ip_address(const String &host) {
  for (size_t i = 0; i < host.length(); i++) {
    char c = host[i];
    if (!((c >= '0' && c <= '9') || c == '.')) {
      return false;
    }
  }
  return host.length() > 0;
}

static bool is_captive_portal_request() {
  if (!_ap_mode) {
    return false;
  }

  if (!_server.hasHeader("Host")) {
    return false;
  }

  String host = _server.header("Host");
  int colon = host.indexOf(':');
  if (colon > 0) {
    host = host.substring(0, colon);
  }

  IPAddress ap_ip = WiFi.softAPIP();
  String ap_ip_str = ap_ip.toString();
  if (host == ap_ip_str || is_ip_address(host)) {
    return false;
  }

  return true;
}

static void redirect_to_setup() {
  String url = String("http://") + WiFi.softAPIP().toString() + "/";
  _server.sendHeader("Location", url, true);
  _server.send(302, "text/plain", "");
}

static void start_setup_ap() {
  if (_ap_mode) {
    return;
  }

  Serial.printf("[WIFI] starting setup AP: %s\n", SETUP_AP_SSID);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_SSID);
  Serial.printf("[WIFI] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  _dns.start(53, "*", WiFi.softAPIP());
  _ap_mode = true;
}

static void stop_setup_ap() {
  if (!_ap_mode) {
    return;
  }
  _dns.stop();
  WiFi.softAPdisconnect(true);
  _ap_mode = false;
  Serial.println("[WIFI] setup AP stopped");
}

static RuntimeConfig request_to_cfg() {
  RuntimeConfig cfg = runtime_config_get();

  if (_server.hasArg("plain") && _server.arg("plain").length() > 0) {
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain")) == DeserializationError::Ok) {
      if (doc["wifi_ssid"].is<const char *>())
        cfg.wifi_ssid = String((const char *)doc["wifi_ssid"]);
      if (doc["wifi_pass"].is<const char *>())
        cfg.wifi_pass = String((const char *)doc["wifi_pass"]);
      if (doc["ws_uri"].is<const char *>())
        cfg.ws_uri = String((const char *)doc["ws_uri"]);
      if (doc["initial_prompt"].is<const char *>())
        cfg.initial_prompt = String((const char *)doc["initial_prompt"]);
      return cfg;
    }
  }

  if (_server.hasArg("wifi_ssid"))
    cfg.wifi_ssid = _server.arg("wifi_ssid");
  if (_server.hasArg("wifi_pass"))
    cfg.wifi_pass = _server.arg("wifi_pass");
  if (_server.hasArg("ws_uri"))
    cfg.ws_uri = _server.arg("ws_uri");
  if (_server.hasArg("initial_prompt"))
    cfg.initial_prompt = _server.arg("initial_prompt");
  return cfg;
}

static void setup_routes() {
  const char *header_keys[] = {"Host"};
  _server.collectHeaders(header_keys, 1);

  _server.on("/", HTTP_GET, []() {
    if (is_captive_portal_request()) {
      redirect_to_setup();
      return;
    }

    if (SPIFFS.exists("/setup.html")) {
      File f = SPIFFS.open("/setup.html", "r");
      _server.streamFile(f, "text/html");
      f.close();
    } else {
      _server.send(200, "text/plain",
                   "setup.html not found. Run make build-data && make upload-data");
    }
  });

  _server.on("/api/config", HTTP_GET, []() {
    const RuntimeConfig &cfg = runtime_config_get();
    JsonDocument doc;
    doc["wifi_ssid"] = cfg.wifi_ssid;
    doc["ws_uri"] = cfg.ws_uri;
    doc["initial_prompt"] = cfg.initial_prompt;
    doc["ap_mode"] = _ap_mode;
    doc["connected"] = WiFi.status() == WL_CONNECTED;
    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
  });

  _server.on("/api/config", HTTP_POST, []() {
    RuntimeConfig cfg = request_to_cfg();
    cfg.wifi_ssid.trim();
    cfg.ws_uri.trim();
    cfg.initial_prompt.trim();

    if (cfg.wifi_ssid.length() == 0) {
      _server.send(400, "application/json", "{\"ok\":false,\"error\":\"wifi_ssid required\"}");
      return;
    }

    bool ok = runtime_config_save(cfg);
    if (!ok) {
      _server.send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    _wifi_reconnect_count = 0;
    _server.send(200, "application/json", "{\"ok\":true,\"message\":\"saved\"}");

    delay(100);
    stop_setup_ap();
    wifi_manager_connect();
  });

  _server.on("/generate_204", HTTP_GET, []() { redirect_to_setup(); });
  _server.on("/hotspot-detect.html", HTTP_GET, []() { redirect_to_setup(); });
  _server.on("/connecttest.txt", HTTP_GET, []() { redirect_to_setup(); });
  _server.on("/ncsi.txt", HTTP_GET, []() { redirect_to_setup(); });

  _server.onNotFound([]() {
    if (is_captive_portal_request()) {
      redirect_to_setup();
      return;
    }
    _server.send(404, "text/plain", "not found");
  });
  _server.begin();
}

void wifi_manager_init() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[WIFI] SPIFFS mount failed");
  }

  runtime_config_init();
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("esp32-live-api");

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.printf("[WIFI EVENT] id=%d\n", (int)event);
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("[WIFI] disconnected reason=%d\n",
                    info.wifi_sta_disconnected.reason);
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      Serial.print("WiFi Connected! IP: ");
      Serial.println(WiFi.localIP());
      Serial.printf("[WIFI] rssi=%d\n", WiFi.RSSI());
      _wifi_reconnect_count = 0;
      stop_setup_ap();
    }
  });

  setup_routes();

  if (!runtime_config_has_wifi()) {
    Serial.println("[WIFI] no SSID configured, entering setup AP mode");
    start_setup_ap();
  }
}

void wifi_manager_connect() {
  const RuntimeConfig &cfg = runtime_config_get();
  if (cfg.wifi_ssid.length() == 0) {
    Serial.println("[WIFI] connect skipped: empty SSID");
    start_setup_ap();
    return;
  }

  Serial.printf("[WIFI] begin connect ssid=%s\n", cfg.wifi_ssid.c_str());
  WiFi.disconnect();
  WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());
}

bool wifi_manager_is_connected() { return WiFi.status() == WL_CONNECTED; }

void wifi_manager_wait_connected() {
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (_ap_mode) {
      _server.handleClient();
      _dns.processNextRequest();
    }
  }
  Serial.println("");
}

void wifi_manager_loop() {
  static wl_status_t _last_status = WL_IDLE_STATUS;
  wl_status_t status = WiFi.status();
  if (status != _last_status) {
    Serial.printf("[WIFI] status change %d -> %d\n", (int)_last_status,
                  (int)status);
    _last_status = status;
  }

  if (_ap_mode) {
    _server.handleClient();
    _dns.processNextRequest();
  }

  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - _last_reconnect_attempt > WIFI_RETRY_DELAY_MS) {
      _last_reconnect_attempt = now;
      _wifi_reconnect_count++;
      Serial.printf("[WIFI] reconnect attempt #%lu\n",
                    (unsigned long)_wifi_reconnect_count);
      wifi_manager_connect();

      if (_wifi_reconnect_count >= WIFI_MAX_RETRY) {
        start_setup_ap();
      }
    }
  }
}

bool wifi_manager_is_ap_mode() { return _ap_mode; }

uint32_t wifi_manager_reconnect_count() { return _wifi_reconnect_count; }
