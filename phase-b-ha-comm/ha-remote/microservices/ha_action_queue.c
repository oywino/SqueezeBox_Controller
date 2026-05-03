#include "ha_action_queue.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ha_rest.h"

#define HA_ACTION_QUEUE_LEN 8
#define HA_ACTION_STOP_SLICE_US 50000

enum ha_action_type {
    HA_ACTION_NONE = 0,
    HA_ACTION_SERVICE,
    HA_ACTION_FETCH_STATE
};

struct ha_action {
    enum ha_action_type type;
    char service[64];
    char entity_id[64];
};

static pthread_t g_thread;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_started = 0;
static int g_running = 0;
static int g_refresh_pending = 0;
static char g_base_url[128];
static char g_token[256];
static struct ha_action g_queue[HA_ACTION_QUEUE_LEN];
static unsigned int g_head = 0;
static unsigned int g_tail = 0;

static int queue_empty(void)
{
    return g_head == g_tail;
}

static int queue_full(void)
{
    return ((g_tail + 1U) % HA_ACTION_QUEUE_LEN) == g_head;
}

static int queue_push(const struct ha_action *action)
{
    if (queue_full()) {
        fprintf(stderr, "[ha_action_queue] dropped action: queue full\n");
        return 0;
    }
    g_queue[g_tail] = *action;
    g_tail = (g_tail + 1U) % HA_ACTION_QUEUE_LEN;
    return 1;
}

static int queue_pop(struct ha_action *action)
{
    if (queue_empty()) return 0;
    *action = g_queue[g_head];
    g_head = (g_head + 1U) % HA_ACTION_QUEUE_LEN;
    return 1;
}

static void mark_refresh_pending(void)
{
    pthread_mutex_lock(&g_lock);
    g_refresh_pending = 1;
    pthread_mutex_unlock(&g_lock);
}

static void *worker_main(void *arg)
{
    (void)arg;

    for (;;) {
        struct ha_action action;
        char base_url[128];
        char token[256];
        int running;
        int have_action;

        pthread_mutex_lock(&g_lock);
        running = g_running;
        have_action = queue_pop(&action);
        snprintf(base_url, sizeof(base_url), "%s", g_base_url);
        snprintf(token, sizeof(token), "%s", g_token);
        pthread_mutex_unlock(&g_lock);

        if (!running && !have_action) break;
        if (!have_action) {
            usleep(HA_ACTION_STOP_SLICE_US);
            continue;
        }

        if (action.type == HA_ACTION_SERVICE) {
            if (ha_rest_call_service(base_url, token, action.service, action.entity_id)) {
                mark_refresh_pending();
            }
        } else if (action.type == HA_ACTION_FETCH_STATE) {
            if (ha_rest_fetch_state(base_url, token, action.entity_id)) {
                mark_refresh_pending();
            }
        }
    }

    return NULL;
}

int ha_action_queue_start(const char *base_url, const char *token)
{
    pthread_mutex_lock(&g_lock);
    if (g_started) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    snprintf(g_base_url, sizeof(g_base_url), "%s", base_url ? base_url : "");
    snprintf(g_token, sizeof(g_token), "%s", token ? token : "");
    g_head = 0;
    g_tail = 0;
    g_refresh_pending = 0;
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

void ha_action_queue_stop(void)
{
    int started;

    pthread_mutex_lock(&g_lock);
    started = g_started;
    g_running = 0;
    pthread_mutex_unlock(&g_lock);

    if (started) pthread_join(g_thread, NULL);

    pthread_mutex_lock(&g_lock);
    g_started = 0;
    pthread_mutex_unlock(&g_lock);
}

int ha_action_enqueue_service(const char *service, const char *entity_id)
{
    struct ha_action action;
    int ok;

    if (!service || !*service || !entity_id || !*entity_id) return 0;

    memset(&action, 0, sizeof(action));
    action.type = HA_ACTION_SERVICE;
    snprintf(action.service, sizeof(action.service), "%s", service);
    snprintf(action.entity_id, sizeof(action.entity_id), "%s", entity_id);

    pthread_mutex_lock(&g_lock);
    ok = g_running && queue_push(&action);
    pthread_mutex_unlock(&g_lock);
    return ok;
}

int ha_action_enqueue_fetch_state(const char *entity_id)
{
    struct ha_action action;
    int ok;

    if (!entity_id || !*entity_id) return 0;

    memset(&action, 0, sizeof(action));
    action.type = HA_ACTION_FETCH_STATE;
    snprintf(action.entity_id, sizeof(action.entity_id), "%s", entity_id);

    pthread_mutex_lock(&g_lock);
    ok = g_running && queue_push(&action);
    pthread_mutex_unlock(&g_lock);
    return ok;
}

int ha_action_consume_refresh_pending(void)
{
    int pending;

    pthread_mutex_lock(&g_lock);
    pending = g_refresh_pending;
    g_refresh_pending = 0;
    pthread_mutex_unlock(&g_lock);
    return pending;
}
