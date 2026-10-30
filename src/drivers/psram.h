// The Presto's 8 MB PSRAM, through the XIP cache at PSRAM_BASE once psram_init has run.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define PSRAM_BASE ((uint8_t *)0x11000000)

// Finds and sets up the PSRAM on `cs_pin` (the Presto's is 47). Returns its size, 0 if none.
// Call before core 1 starts: it briefly takes over the QMI, which the flash also uses.
size_t psram_init(uint32_t cs_pin);
