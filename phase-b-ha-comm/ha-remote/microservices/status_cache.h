#ifndef HA_REMOTE_STATUS_CACHE_H
#define HA_REMOTE_STATUS_CACHE_H

#include "hal.h"

int  status_cache_start(void);
void status_cache_stop(void);

int status_cache_get_power(struct hal_power_state *st);
int status_cache_get_wifi(struct hal_wifi_state *st);

#endif
