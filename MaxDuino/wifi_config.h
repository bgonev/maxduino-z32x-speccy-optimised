#ifndef WIFI_CONFIG_H_INCLUDED
#define WIFI_CONFIG_H_INCLUDED

#include "configs.h"

#if defined(WIFI_CONFIG_PORTAL) && defined(ESP32)
void wifi_config_begin();
void wifi_config_loop();
#else
static inline void wifi_config_begin() {}
static inline void wifi_config_loop() {}
#endif

#endif // WIFI_CONFIG_H_INCLUDED
