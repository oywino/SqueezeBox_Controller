#ifndef HA_REMOTE_POWER_MANAGER_H
#define HA_REMOTE_POWER_MANAGER_H

#include <stdint.h>

void power_manager_init(uint64_t now_ms);
int power_manager_note_activity(uint64_t now_ms);
void power_manager_tick(uint64_t now_ms);
int power_manager_is_sleeping(void);

#endif
