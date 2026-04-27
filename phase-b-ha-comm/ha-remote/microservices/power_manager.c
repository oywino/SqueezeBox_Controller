#include "power_manager.h"

#include "fb.h"
#include "status_cache.h"

#define PM_BAT_SLEEP_MS (30ULL * 1000ULL)
#define PM_AC_SLEEP_MS  (300ULL * 1000ULL)

static uint64_t g_last_activity_ms = 0;
static int g_sleeping = 0;

void power_manager_init(uint64_t now_ms)
{
    g_last_activity_ms = now_ms;
    g_sleeping = 0;
}

int power_manager_note_activity(uint64_t now_ms)
{
    g_last_activity_ms = now_ms;

    if (g_sleeping) {
        lcd_wake();
        g_sleeping = 0;
        return 1;
    }

    return 0;
}

void power_manager_tick(uint64_t now_ms)
{
    if (g_sleeping) return;

    struct hal_power_state st;
    int timeout_ms = PM_BAT_SLEEP_MS;

    if (status_cache_get_power(&st) == 0 && st.on_ac == 1) {
        timeout_ms = PM_AC_SLEEP_MS;
    }

    if ((now_ms - g_last_activity_ms) >= (uint64_t)timeout_ms) {
        lcd_sleep();
        g_sleeping = 1;
    }
}

int power_manager_is_sleeping(void)
{
    return g_sleeping ? 1 : 0;
}
