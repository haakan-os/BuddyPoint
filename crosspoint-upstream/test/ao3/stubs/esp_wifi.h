#pragma once
using wifi_ps_type_t = int;
constexpr int WIFI_PS_MIN_MODEM = 1;
constexpr int WIFI_PS_NONE = 0;
inline int esp_wifi_get_ps(int* value) {
  *value = WIFI_PS_MIN_MODEM;
  return 0;
}
inline int esp_wifi_set_ps(int) { return 0; }
