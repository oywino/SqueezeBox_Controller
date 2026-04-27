/* HAL implementation — input (wheel/keys/accel) + power shim (C99)
 * Step: HAL owns device lifecycle + polling; input.c consumes HAL only.
 * Devices (SB_A): /dev/input/event1=Wheel, /dev/input/event2=Matrix keys, /dev/input/event3=lis302dl accel.
 * Power: unchanged (jivectl 23/25).
 */

#include "hal.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* paths */
#define PATH_WHEEL "/dev/input/event1"
#define PATH_KEYS  "/dev/input/event2"
#define PATH_ACCEL "/dev/input/event3"

/* state */
static int g_fd_wheel = -1;
static int g_fd_keys  = -1;
static int g_fd_accel = -1;
static int g_hal_refcnt = 0;

/* helpers */
static int64_t mono_ms_now(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

/* jivectl helper: parse last integer in output */
static int parse_last_int(int code, long *out_val) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "/usr/bin/jivectl %d 2>&1", code);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -errno;
    char buf[256];
    long last = 0; int have = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        char *p = buf;
        while (*p) {
            char *end;
            long v = strtol(p, &end, 10);
            if (end != p) { last = v; have = 1; p = end; }
            else { p++; }
        }
    }
    (void)pclose(fp);
    if (!have) return -EIO;
    *out_val = last;
    return 0;
}

/* lifecycle */
int hal_init(void) {
    if (g_hal_refcnt++ > 0) return 0;

    g_fd_wheel = open(PATH_WHEEL, O_RDONLY | O_NONBLOCK);
    if (g_fd_wheel >= 0) {
        (void)ioctl(g_fd_wheel, EVIOCGRAB, 1);
    } else if (errno != ENOENT) {
        fprintf(stderr, "WARN: open(%s) failed: %s\n", PATH_WHEEL, strerror(errno));
    }

    g_fd_keys = open(PATH_KEYS, O_RDONLY | O_NONBLOCK);
    if (g_fd_keys >= 0) {
        (void)ioctl(g_fd_keys, EVIOCGRAB, 1);
    } else if (errno != ENOENT) {
        fprintf(stderr, "WARN: open(%s) failed: %s\n", PATH_KEYS, strerror(errno));
    }

    g_fd_accel = open(PATH_ACCEL, O_RDONLY | O_NONBLOCK);
    if (g_fd_accel >= 0) {
        (void)ioctl(g_fd_accel, EVIOCGRAB, 1);
    } else if (errno != ENOENT) {
        fprintf(stderr, "WARN: open(%s) failed: %s\n", PATH_ACCEL, strerror(errno));
    }

    return 0;
}

void hal_shutdown(void) {
    if (g_hal_refcnt == 0) return;
    if (--g_hal_refcnt > 0) return;

    if (g_fd_wheel >= 0) {
        (void)ioctl(g_fd_wheel, EVIOCGRAB, 0);
        close(g_fd_wheel);
        g_fd_wheel = -1;
    }
    if (g_fd_keys >= 0) {
        (void)ioctl(g_fd_keys, EVIOCGRAB, 0);
        close(g_fd_keys);
        g_fd_keys = -1;
    }
    if (g_fd_accel >= 0) {
        (void)ioctl(g_fd_accel, EVIOCGRAB, 0);
        close(g_fd_accel);
        g_fd_accel = -1;
    }
}

/* input polling */
int hal_poll_input(struct hal_input_event *ev, int timeout_ms) {
    if (!ev) return -EINVAL;
    if (timeout_ms < 0) return -EINVAL;

    struct pollfd pfds[3];
    int n = 0;
    if (g_fd_wheel >= 0) { pfds[n].fd = g_fd_wheel; pfds[n].events = POLLIN; n++; }
    if (g_fd_keys  >= 0) { pfds[n].fd = g_fd_keys;  pfds[n].events = POLLIN; n++; }
    if (g_fd_accel >= 0) { pfds[n].fd = g_fd_accel; pfds[n].events = POLLIN; n++; }

    if (n == 0) return -ENODEV;

    int prc = poll(pfds, n, timeout_ms);
    if (prc == 0) return 0;
    if (prc < 0)  return -errno;

    int fd  = -1;
    int src = HAL_INPUT_SRC_UNKNOWN;
    for (int i = 0; i < n; i++) {
        if (pfds[i].revents & POLLIN) {
            if (pfds[i].fd == g_fd_wheel) { fd = g_fd_wheel; src = HAL_INPUT_SRC_WHEEL; break; }
            if (pfds[i].fd == g_fd_keys)  { fd = g_fd_keys;  src = HAL_INPUT_SRC_BUTTONS; break; }
            if (pfds[i].fd == g_fd_accel) { fd = g_fd_accel; src = HAL_INPUT_SRC_ACCEL;  break; }
        }
    }
    if (fd < 0) return 0;

    struct input_event iev;
    ssize_t r = read(fd, &iev, sizeof(iev));
    if (r < (ssize_t)sizeof(iev)) return -EAGAIN;

    ev->source = src;
    ev->type   = iev.type;
    ev->code   = iev.code;
    ev->value  = iev.value;
    ev->ts_ms  = mono_ms_now();
    return 1;
}

/* power telemetry (unchanged) */
int hal_get_power(struct hal_power_state *st) {
    if (st == NULL) return -EINVAL;

    st->bat_raw  = -1;
    st->bat_pct  = -1;
    st->on_ac    = -1;
    st->charging = -1;

    long cradle = -1;
    long chg    = -1;

    if (parse_last_int(23, &cradle) == 0) st->on_ac    = (cradle == 0) ? 1 : 0;
    if (parse_last_int(25, &chg)    == 0) st->charging = (chg == 0) ? 1 : 0;

    return 0;
}

int hal_get_wifi(struct hal_wifi_state *st) {
    if (st == NULL) return -EINVAL;

    st->connected = -1;
    st->signal_level = -1;

    FILE *fp = popen("/sbin/iwconfig eth0 2>/dev/null", "r");
    if (!fp) return -errno;

    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        if (strstr(buf, "Access Point:")) {
            st->connected = strstr(buf, "Not-Associated") ? 0 : 1;
        }
    }

    (void)pclose(fp);
    if (st->connected < 0) return -EIO;
    if (st->connected == 1) st->signal_level = 4;
    return 0;
}
