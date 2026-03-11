#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include <Arduino.h>

struct RuntimeConfig {
  String wifi_ssid;
  String wifi_pass;
  String ws_uri;
  String initial_prompt;
};

void runtime_config_init();
const RuntimeConfig &runtime_config_get();
bool runtime_config_save(const RuntimeConfig &cfg);
bool runtime_config_has_wifi();

#endif // RUNTIME_CONFIG_H
