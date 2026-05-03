#ifndef HA_REMOTE_HA_ACTION_QUEUE_H
#define HA_REMOTE_HA_ACTION_QUEUE_H

int ha_action_queue_start(const char *base_url, const char *token);
void ha_action_queue_stop(void);

int ha_action_enqueue_service(const char *service, const char *entity_id);
int ha_action_enqueue_fetch_state(const char *entity_id);

int ha_action_consume_refresh_pending(void);

#endif /* HA_REMOTE_HA_ACTION_QUEUE_H */
