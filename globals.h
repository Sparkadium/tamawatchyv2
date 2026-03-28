/*
 * globals.h - Shared extern declarations for all tama modules.
 *
 * All variables here are *defined* in tama.ino. This header lets
 * the .cpp modules reference them without redefinition.
 */
#ifndef _GLOBALS_H_
#define _GLOBALS_H_

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#if !STATE_STORAGE_RTC
#include <Preferences.h>
#endif
#include <esp_sleep.h>
#include <sys/time.h>

#include "config.h"

extern "C" {
  #include "lib/tamalib/tamalib.h"
}

/******************************************************************************
 * Display Object
 ******************************************************************************/
extern GxEPD2_BW<GxEPD2_154_GDEY0154D67, GxEPD2_154_GDEY0154D67::HEIGHT> display;

/******************************************************************************
 * RTC Slow Memory
 ******************************************************************************/
extern int64_t sleep_timestamp_us;
extern bool    has_saved_state;

/******************************************************************************
 * Emulator State Buffers
 ******************************************************************************/
extern bool_t matrix_buffer[TAMA_LCD_HEIGHT][TAMA_LCD_WIDTH];
extern bool_t icon_buffer[TAMA_ICON_NUM];
extern bool_t prev_matrix_buffer[TAMA_LCD_HEIGHT][TAMA_LCD_WIDTH];
extern bool_t prev_icon_buffer[TAMA_ICON_NUM];

/******************************************************************************
 * Runtime State
 ******************************************************************************/
extern bool          fast_forwarding;
extern bool          woken_by_button;
extern unsigned long last_button_ms;
extern unsigned long last_screen_update_ms;

/******************************************************************************
 * Button State
 ******************************************************************************/
struct ButtonState {
  uint8_t       pin;
  button_t      tama_btn;
  bool          stable_state;    // Last confirmed/registered state
  bool          pending_state;   // Candidate state being debounced
  unsigned long pending_since;   // millis() when pending_state was first seen
};

extern ButtonState buttons[];
extern const int   NUM_BUTTONS;

/******************************************************************************
 * NVS (only needed when not using RTC storage)
 ******************************************************************************/
#if !STATE_STORAGE_RTC
extern Preferences preferences;
#endif

/******************************************************************************
 * ROM
 ******************************************************************************/
#define ROM_SIZE 6144
extern u12_t rom_data[ROM_SIZE];

#endif /* _GLOBALS_H_ */
