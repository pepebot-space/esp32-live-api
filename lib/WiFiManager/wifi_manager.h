#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

void wifi_manager_init(const char *ssid, const char *password);
void wifi_manager_connect();
bool wifi_manager_is_connected();
void wifi_manager_wait_connected();
void wifi_manager_loop();

#endif // WIFI_MANAGER_H
