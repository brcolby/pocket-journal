#ifndef PJ_WIFI_STATE_H
#define PJ_WIFI_STATE_H

#include <stdint.h>

#define PJ_WIFI_DHCP_TIMEOUT_MS 30000u
#define PJ_WIFI_CONNECT_TIMEOUT_MS 15000u
#define PJ_WIFI_RETRY_MAX_MS 60000u

typedef enum {
    PJ_WIFI_PHASE_UNPROVISIONED = 0,
    PJ_WIFI_PHASE_DISCONNECTED,
    PJ_WIFI_PHASE_CONNECTING,
    PJ_WIFI_PHASE_AUTHENTICATION_FAILED,
    PJ_WIFI_PHASE_ACCESS_POINT_UNAVAILABLE,
    PJ_WIFI_PHASE_ASSOCIATION_FAILED,
    PJ_WIFI_PHASE_CONNECTION_FAILED,
    PJ_WIFI_PHASE_DHCP,
    PJ_WIFI_PHASE_DHCP_FAILED,
    PJ_WIFI_PHASE_CONNECTED,
    PJ_WIFI_PHASE_CONNECT_TIMEOUT,
    PJ_WIFI_PHASE_FAILED,
} pj_wifi_phase_t;

typedef enum {
    PJ_WIFI_DHCP_UNKNOWN = 0,
    PJ_WIFI_DHCP_REQUESTING,
    PJ_WIFI_DHCP_BOUND,
    PJ_WIFI_DHCP_TIMEOUT,
} pj_wifi_dhcp_state_t;

typedef enum {
    PJ_WIFI_RETRY_DISABLED = 0,
    PJ_WIFI_RETRY_IDLE,
    PJ_WIFI_RETRY_BACKOFF,
    PJ_WIFI_RETRY_RETRYING,
} pj_wifi_retry_state_t;

typedef enum {
    PJ_WIFI_DISCONNECT_OTHER = 0,
    PJ_WIFI_DISCONNECT_AUTHENTICATION,
    PJ_WIFI_DISCONNECT_ACCESS_POINT_UNAVAILABLE,
    PJ_WIFI_DISCONNECT_ASSOCIATION,
    PJ_WIFI_DISCONNECT_CONNECTION,
} pj_wifi_disconnect_class_t;

typedef enum {
    PJ_WIFI_ACTION_NONE = 0,
    PJ_WIFI_ACTION_CONNECT,
    PJ_WIFI_ACTION_RECONNECT,
} pj_wifi_action_t;

typedef struct {
    pj_wifi_phase_t phase;
    pj_wifi_dhcp_state_t dhcp_state;
    pj_wifi_retry_state_t retry_state;
    uint8_t provisioned;
    uint8_t has_ip;
    int8_t ap_visible;
    int16_t rssi_dbm;
    uint8_t channel;
    uint8_t auth_mode;
    uint8_t rssi_known;
    uint8_t channel_known;
    uint8_t auth_mode_known;
    uint16_t last_disconnect_reason;
    uint32_t retry_count;
    uint32_t backoff_ms;
    uint64_t next_retry_ms;
    uint64_t connect_deadline_ms;
    uint64_t dhcp_deadline_ms;
    uint64_t last_success_monotonic_ms;
    int64_t last_success_utc_s;
} pj_wifi_state_t;

void pj_wifi_state_init(pj_wifi_state_t *state, int provisioned,
                        uint64_t now_ms);
void pj_wifi_state_set_provisioned(pj_wifi_state_t *state, int provisioned,
                                   uint64_t now_ms);
void pj_wifi_state_on_driver_started(pj_wifi_state_t *state,
                                     uint64_t now_ms);
void pj_wifi_state_on_associated(pj_wifi_state_t *state, uint64_t now_ms,
                                 unsigned channel, unsigned auth_mode);
void pj_wifi_state_on_got_ip(pj_wifi_state_t *state, uint64_t now_ms,
                             int rssi_dbm, unsigned channel);
void pj_wifi_state_on_lost_ip(pj_wifi_state_t *state, uint64_t now_ms);
void pj_wifi_state_on_disconnected(pj_wifi_state_t *state, unsigned reason,
                                   int ap_observed, int rssi_dbm,
                                   uint64_t now_ms);
void pj_wifi_state_on_connect_request_failed(pj_wifi_state_t *state,
                                             uint64_t now_ms);
void pj_wifi_state_set_last_success_utc(pj_wifi_state_t *state,
                                        int64_t epoch_s);
pj_wifi_action_t pj_wifi_state_tick(pj_wifi_state_t *state, uint64_t now_ms);

pj_wifi_disconnect_class_t pj_wifi_disconnect_classify(unsigned reason);
const char *pj_wifi_phase_name(pj_wifi_phase_t phase);
const char *pj_wifi_dhcp_state_name(pj_wifi_dhcp_state_t state);
const char *pj_wifi_retry_state_name(pj_wifi_retry_state_t state);

#endif
