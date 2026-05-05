#ifndef HA_REMOTE_HA_WS_H
#define HA_REMOTE_HA_WS_H

#include <stdint.h>

/* Seed RNG used for WebSocket masking keys. */
void ha_ws_seed(uint32_t seed);

/* Start a short-lived HA WebSocket session (connect, handshake, auth, get_states). */
int  ha_session_start(const char *host, const char *token);

/* Start persistent HA state_changed subscription for configured entities. */
int ha_session_subscribe_state_changes(const char *base_url, const char *token);

/* Drain parsed HA state updates on the UI thread. */
void ha_ws_drain_state_updates(void);

/* Close the current HA session (if any). */
void ha_session_close(void);

/* Called by input module on user activity to extend the idle grace window. */
void ha_session_note_activity(void);

#endif /* HA_REMOTE_HA_WS_H */
