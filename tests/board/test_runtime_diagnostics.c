#include "pj_runtime_diagnostics.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_UNKNOWN), "unknown") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_POWER_ON), "power_on") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_EXTERNAL), "external") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_SOFTWARE), "software") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_PANIC), "panic") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_INTERRUPT_WATCHDOG),
                  "interrupt_watchdog") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_TASK_WATCHDOG),
                  "task_watchdog") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_WATCHDOG), "watchdog") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_DEEP_SLEEP), "deep_sleep") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_BROWNOUT), "brownout") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_SDIO), "sdio") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_USB), "usb") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_JTAG), "jtag") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_EFUSE), "efuse") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_POWER_GLITCH), "power_glitch") == 0);
    assert(strcmp(pj_reset_reason_name(PJ_RESET_REASON_CPU_LOCKUP), "cpu_lockup") == 0);
    assert(strcmp(pj_reset_reason_name((pj_reset_reason_t)99), "unknown") == 0);
    return 0;
}
