#ifndef HA_REMOTE_HA_WS_H
#define HA_REMOTE_HA_WS_H

#include <stdint.h>

/* Seed RNG used for WebSocket masking keys. */
void ha_ws_seed(uint32_t seed);

/* Start a short-lived HA WebSocket session (connect, handshake, auth, get_states). */
int  ha_session_start(const char *host, const char *token);

/* Backend poll tick: invoked by UI wrapper timer. */
void ha_poll_timer(void);

/* Close the current HA session (if any). */
void ha_session_close(void);

/* Called by input module on user activity to extend the idle grace window. */
void ha_session_note_activity(void);

#endif /* HA_REMOTE_HA_WS_H */