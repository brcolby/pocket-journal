#ifndef PJ_TIME_SYNC_H
#define PJ_TIME_SYNC_H

#include <stdint.h>

#define PJ_TIME_SYNC_TIMEOUT_MS 30000u
#define PJ_TIME_SYNC_REFRESH_MS (6ull * 60ull * 60ull * 1000ull)
#define PJ_TIME_SYNC_STALE_MS (24ull * 60ull * 60ull * 1000ull)
#define PJ_TIME_SYNC_LARGE_CORRECTION_MS (35ll * 60ll * 1000ll)

typedef enum {
    PJ_TIME_SYNC_UNKNOWN = 0,
    PJ_TIME_SYNC_WAITING_NETWORK,
    PJ_TIME_SYNC_SCHEDULED,
    PJ_TIME_SYNC_SYNCHRONIZING,
    PJ_TIME_SYNC_SYNCHRONIZED,
    PJ_TIME_SYNC_STALE,
    PJ_TIME_SYNC_FAILED,
} pj_time_sync_state_name_t;

typedef enum {
    PJ_TIME_SYNC_FAILURE_NONE = 0,
    PJ_TIME_SYNC_FAILURE_NETWORK,
    PJ_TIME_SYNC_FAILURE_START,
    PJ_TIME_SYNC_FAILURE_TIMEOUT,
    PJ_TIME_SYNC_FAILURE_INVALID_TIME,
    PJ_TIME_SYNC_FAILURE_SYSTEM_CLOCK,
} pj_time_sync_failure_t;

typedef enum {
    PJ_TIME_SYNC_PUBLICATION_NOT_READY = 0,
    PJ_TIME_SYNC_PUBLICATION_TIMEZONE_REQUIRED,
    PJ_TIME_SYNC_PUBLICATION_PUBLISHED,
    PJ_TIME_SYNC_PUBLICATION_RTC_FAILED,
    PJ_TIME_SYNC_PUBLICATION_CIVIL_FAILED,
} pj_time_sync_publication_t;

typedef enum {
    PJ_TIME_SYNC_CORRECTION_INITIAL = 0,
    PJ_TIME_SYNC_CORRECTION_SMALL,
    PJ_TIME_SYNC_CORRECTION_STEP_FORWARD,
    PJ_TIME_SYNC_CORRECTION_STEP_BACKWARD,
    PJ_TIME_SYNC_CORRECTION_REJECTED,
} pj_time_sync_correction_t;

typedef enum {
    PJ_TIME_SYNC_ACTION_NONE = 0,
    PJ_TIME_SYNC_ACTION_START,
} pj_time_sync_action_t;

typedef struct {
    pj_time_sync_state_name_t state;
    pj_time_sync_failure_t failure;
    pj_time_sync_publication_t publication;
    pj_time_sync_correction_t correction;
    uint8_t has_ip;
    uint8_t time_known;
    uint8_t utc_offset_known;
    int16_t utc_offset_minutes;
    uint32_t attempt_count;
    uint32_t backoff_ms;
    uint64_t next_attempt_ms;
    uint64_t attempt_deadline_ms;
    uint64_t last_success_monotonic_ms;
    int64_t last_success_utc_s;
    int64_t last_offset_ms;
} pj_time_sync_state_t;

void pj_time_sync_init(pj_time_sync_state_t *state, int time_known,
                       uint64_t now_ms);
void pj_time_sync_on_ip(pj_time_sync_state_t *state, uint64_t now_ms);
void pj_time_sync_on_network_lost(pj_time_sync_state_t *state,
                                  uint64_t now_ms);
void pj_time_sync_on_start_failed(pj_time_sync_state_t *state,
                                  uint64_t now_ms);
void pj_time_sync_on_system_clock_failed(pj_time_sync_state_t *state,
                                         uint64_t now_ms);
int pj_time_sync_epoch_valid(int64_t epoch_s);
int pj_time_sync_expected_epoch_ms(const pj_time_sync_state_t *state,
                                   uint64_t now_ms, int64_t *epoch_ms);
pj_time_sync_correction_t pj_time_sync_correction_policy(int old_time_valid,
                                                         int64_t old_epoch_ms,
                                                         int64_t new_epoch_ms);
int pj_time_sync_on_success(pj_time_sync_state_t *state, int64_t epoch_s,
                            int old_time_valid, int64_t old_epoch_ms,
                            uint64_t now_ms);
void pj_time_sync_set_publication(pj_time_sync_state_t *state,
                                  pj_time_sync_publication_t publication);
pj_time_sync_action_t pj_time_sync_tick(pj_time_sync_state_t *state,
                                        uint64_t now_ms);

const char *pj_time_sync_state_name(pj_time_sync_state_name_t state);
const char *pj_time_sync_failure_name(pj_time_sync_failure_t failure);
const char *pj_time_sync_publication_name(pj_time_sync_publication_t publication);
const char *pj_time_sync_correction_name(pj_time_sync_correction_t correction);

#endif
