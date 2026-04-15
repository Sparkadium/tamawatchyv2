/*
 * tama_hal.h - TamaLIB hardware abstraction layer and button polling.
 */
#ifndef _TAMA_HAL_H_
#define _TAMA_HAL_H_

extern "C" {
  #include "lib/tamalib/tamalib.h"
}

// The HAL struct registered with TamaLIB via tamalib_register_hal().
extern hal_t hal;

// Poll all physical buttons and forward state changes to TamaLIB.
void pollButtons();

// GPIO interrupt service routines — attach in setup() with attachInterrupt()
void IRAM_ATTR btnISR_A();
void IRAM_ATTR btnISR_B();
void IRAM_ATTR btnISR_C();

#endif /* _TAMA_HAL_H_ */
