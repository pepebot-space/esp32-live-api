#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

void wifi_manager_init();
void wifi_manager_connect();
bool wifi_manager_is_connected();
void wifi_manager_wait_connected();
void wifi_manager_loop();
bool wifi_manager_is_ap_mode();
uint32_t wifi_manager_reconnect_count();

#endif // WIFI_MANAGER_H
