#include "crash.h"

#include <stdint.h>
#include <stdio.h>
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#define MAGIC_FAULT 0xFA017ED1u
#define MAGIC_HANG  0x5711CC00u     // armed while running: a watchdog reset with it set was a hang

static char last[64];

// The hard fault's stacked registers: r0-r3, r12, lr, pc, xpsr
void __attribute__((used)) crash_fault(uint32_t *frame) {
    watchdog_hw->scratch[0] = MAGIC_FAULT;
    watchdog_hw->scratch[1] = frame[6];     // pc
    watchdog_hw->scratch[2] = frame[5];     // lr
    watchdog_reboot(0, 0, 0);
    for (;;) {}
}

// Replaces the SDK's hard fault handler: finds which stack the fault was pushed to
void __attribute__((naked)) isr_hardfault(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b crash_fault\n");
}

void crash_init(void) {
    // picotool and the bootrom restart through the watchdog too; only our own armed watchdog
    // running out (watchdog_enable_caused_reboot) is a hang
    if (watchdog_caused_reboot() && watchdog_hw->scratch[0] == MAGIC_FAULT)
        snprintf(last, sizeof last, "fault at pc %08lx lr %08lx", (unsigned long)watchdog_hw->scratch[1],
                 (unsigned long)watchdog_hw->scratch[2]);
    else if (watchdog_enable_caused_reboot() && watchdog_hw->scratch[0] == MAGIC_HANG)
        snprintf(last, sizeof last, "hang (watchdog)");
    watchdog_hw->scratch[0] = MAGIC_HANG;
    watchdog_enable(CRASH_HANG_MS, true);   // paused while a debugger has the cores stopped
}

void crash_alive(void) {
    watchdog_update();
}

const char *crash_last(void) {
    return last;
}
