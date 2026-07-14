#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Values mirror the public esp_reset_reason_t API. */
typedef enum {
    PJ_RESET_REASON_UNKNOWN = 0,
    PJ_RESET_REASON_POWER_ON,
    PJ_RESET_REASON_EXTERNAL,
    PJ_RESET_REASON_SOFTWARE,
    PJ_RESET_REASON_PANIC,
    PJ_RESET_REASON_INTERRUPT_WATCHDOG,
    PJ_RESET_REASON_TASK_WATCHDOG,
    PJ_RESET_REASON_WATCHDOG,
    PJ_RESET_REASON_DEEP_SLEEP,
    PJ_RESET_REASON_BROWNOUT,
    PJ_RESET_REASON_SDIO,
    PJ_RESET_REASON_USB,
    PJ_RESET_REASON_JTAG,
    PJ_RESET_REASON_EFUSE,
    PJ_RESET_REASON_POWER_GLITCH,
    PJ_RESET_REASON_CPU_LOCKUP,
} pj_reset_reason_t;

const char *pj_reset_reason_name(pj_reset_reason_t reason);

#ifdef __cplusplus
}
#endif
