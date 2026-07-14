#include "pj_runtime_diagnostics.h"

const char *pj_reset_reason_name(pj_reset_reason_t reason)
{
    switch (reason) {
    case PJ_RESET_REASON_POWER_ON: return "power_on";
    case PJ_RESET_REASON_EXTERNAL: return "external";
    case PJ_RESET_REASON_SOFTWARE: return "software";
    case PJ_RESET_REASON_PANIC: return "panic";
    case PJ_RESET_REASON_INTERRUPT_WATCHDOG: return "interrupt_watchdog";
    case PJ_RESET_REASON_TASK_WATCHDOG: return "task_watchdog";
    case PJ_RESET_REASON_WATCHDOG: return "watchdog";
    case PJ_RESET_REASON_DEEP_SLEEP: return "deep_sleep";
    case PJ_RESET_REASON_BROWNOUT: return "brownout";
    case PJ_RESET_REASON_SDIO: return "sdio";
    case PJ_RESET_REASON_USB: return "usb";
    case PJ_RESET_REASON_JTAG: return "jtag";
    case PJ_RESET_REASON_EFUSE: return "efuse";
    case PJ_RESET_REASON_POWER_GLITCH: return "power_glitch";
    case PJ_RESET_REASON_CPU_LOCKUP: return "cpu_lockup";
    case PJ_RESET_REASON_UNKNOWN:
    default: return "unknown";
    }
}
