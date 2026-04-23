#ifndef HA_REMOTE_HAL_H
#define HA_REMOTE_HAL_H

/* Minimal hardware abstraction layer (HAL) contract.
 *
 * Scope (Phase B / Step 5.1a):
 * - Event stream unification (wheel/buttons/accel) via hal_poll_input()
 * - Power telemetry snapshot via hal_get_power()
 *
 * This header is stable API. Implementation in hal.c is initially a no-op
 * shim to avoid behavioral drift until wired to real device code.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Input event sources (expand in later steps as needed). */
enum {
    HAL_INPUT_SRC_UNKNOWN = 0,
    HAL_INPUT_SRC_BUTTONS = 1,
    HAL_INPUT_SRC_WHEEL   = 2,
    HAL_INPUT_SRC_ACCEL   = 3
};

/* Input event types/codes use project-local integers.
 * Mapping to Linux input codes or device specifics will be handled inside hal.c
 * in later steps to keep the interface stable.
 */
struct hal_input_event {
    int   source;     /* HAL_INPUT_SRC_* */
    int   type;       /* semantic type (e.g., key/rel/abs/custom) */
    int   code;       /* semantic code (e.g., KEY_OK, REL_WHEEL, etc.) */
    int   value;      /* value or 1/0 for press/release */
    int64_t ts_ms;    /* monotonic timestamp in milliseconds */
};

/* Power/charging telemetry snapshot. */
struct hal_power_state {
    int bat_raw;   /* raw ADC/driver reading if available, else -1 */
    int bat_pct;   /* 0–100 if available, else -1 */
    int on_ac;     /* 1 if on external power/cradle, 0 if not, -1 unknown */
    int charging;  /* 1 charging, 0 not charging, -1 unknown */
};

/* Initialize/shutdown HAL lifetime. */
int  hal_init(void);
void hal_shutdown(void);

/* Poll next input event with timeout (ms).
 * Returns:
 *   1  => event filled into *ev
 *   0  => timeout, no event
 *  <0  => error (negative errno-style code)
 */
int  hal_poll_input(struct hal_input_event *ev, int timeout_ms);

/* Read current power/charging snapshot.
 * Returns:
 *   0  => success, *st filled (unknown fields set to -1)
 *  <0  => error (negative errno-style code)
 */
int  hal_get_power(struct hal_power_state *st);

#ifdef __cplusplus
}
#endif

#endif /* HA_REMOTE_HAL_H */
