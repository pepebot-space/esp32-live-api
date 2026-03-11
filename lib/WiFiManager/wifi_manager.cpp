#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>

static const char *_ssid = nullptr;
static const char *_password = nullptr;
static unsigned long _last_reconnect_attempt = 0;
static uint32_t _wifi_reconnect_count = 0;

void wifi_manager_init(const char *ssid, const char *password) {
  _ssid = ssid;
  _password = password;
  Serial.printf("[WIFI] init ssid=%s\n", _ssid ? _ssid : "(null)");
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
    }
  });
}

void wifi_manager_connect() {
  if (!_ssid)
    return;
  Serial.printf("[WIFI] begin connect ssid=%s\n", _ssid);
  WiFi.begin(_ssid, _password);
}

bool wifi_manager_is_connected() { return WiFi.status() == WL_CONNECTED; }

void wifi_manager_wait_connected() {
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
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

  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - _last_reconnect_attempt > WIFI_RETRY_DELAY_MS) {
      _last_reconnect_attempt = now;
      _wifi_reconnect_count++;
      Serial.printf("[WIFI] reconnect attempt #%lu\n",
                    (unsigned long)_wifi_reconnect_count);
      WiFi.disconnect();
      WiFi.begin(_ssid, _password);
    }
  }
}
