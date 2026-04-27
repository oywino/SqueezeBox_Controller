#include "status_cache.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define STATUS_CACHE_SLEEP_MS 5000
#define STATUS_CACHE_STOP_SLICE_US 100000

static pthread_t g_thread;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_running = 0;
static int g_started = 0;
static int g_have_power = 0;
static int g_have_wifi = 0;
static struct hal_power_state g_power;
static struct hal_wifi_state g_wifi;

static void set_unknown(void)
{
    memset(&g_power, 0, sizeof(g_power));
    g_power.bat_raw = -1;
    g_power.bat_pct = -1;
    g_power.on_ac = -1;
    g_power.charging = -1;

    memset(&g_wifi, 0, sizeof(g_wifi));
    g_wifi.connected = -1;
    g_wifi.signal_level = -1;
}

static void sleep_or_stop(void)
{
    int slices = STATUS_CACHE_SLEEP_MS * 1000 / STATUS_CACHE_STOP_SLICE_US;

    for (int i = 0; i < slices; ++i) {
        pthread_mutex_lock(&g_lock);
        int running = g_running;
        pthread_mutex_unlock(&g_lock);

        if (!running) return;
        usleep(STATUS_CACHE_STOP_SLICE_US);
    }
}

static void *worker_main(void *arg)
{
    (void)arg;

    for (;;) {
        pthread_mutex_lock(&g_lock);
        int running = g_running;
        pthread_mutex_unlock(&g_lock);
        if (!running) break;

        struct hal_wifi_state wifi;
        struct hal_power_state power;
        int wifi_ok = hal_get_wifi(&wifi);
        int power_ok = hal_get_power(&power);

        pthread_mutex_lock(&g_lock);
        if (wifi_ok == 0) {
            g_wifi = wifi;
            g_have_wifi = 1;
        }
        if (power_ok == 0) {
            g_power = power;
            g_have_power = 1;
        }
        pthread_mutex_unlock(&g_lock);

        sleep_or_stop();
    }

    return NULL;
}

int status_cache_start(void)
{
    pthread_mutex_lock(&g_lock);
    if (g_started) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }

    set_unknown();
    g_have_power = 0;
    g_have_wifi = 0;
    g_running = 1;
    pthread_mutex_unlock(&g_lock);

    if (pthread_create(&g_thread, NULL, worker_main, NULL) != 0) {
        pthread_mutex_lock(&g_lock);
        g_running = 0;
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    pthread_mutex_lock(&g_lock);
    g_started = 1;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

void status_cache_stop(void)
{
    pthread_mutex_lock(&g_lock);
    int started = g_started;
    g_running = 0;
    pthread_mutex_unlock(&g_lock);

    if (started) {
        pthread_join(g_thread, NULL);
    }

    pthread_mutex_lock(&g_lock);
    g_started = 0;
    pthread_mutex_unlock(&g_lock);
}

int status_cache_get_power(struct hal_power_state *st)
{
    if (!st) return -1;

    pthread_mutex_lock(&g_lock);
    int have = g_have_power;
    if (have) *st = g_power;
    pthread_mutex_unlock(&g_lock);

    return have ? 0 : -1;
}

int status_cache_get_wifi(struct hal_wifi_state *st)
{
    if (!st) return -1;

    pthread_mutex_lock(&g_lock);
    int have = g_have_wifi;
    if (have) *st = g_wifi;
    pthread_mutex_unlock(&g_lock);

    return have ? 0 : -1;
}
