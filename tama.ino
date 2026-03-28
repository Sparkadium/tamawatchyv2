// Copyright (C) 2026 SQFMI
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.


#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <sys/time.h>

#include "config.h"
#include "rom_12bit.h"

extern "C" {
  #include "lib/tamalib/tamalib.h"
}

#include "globals.h"
#include "tama_display.h"
#include "tama_sleep_screen.h"
#include "tama_hal.h"
#include "tama_nvs.h"
#include "tama_power.h"

/******************************************************************************
 * Display Object
 ******************************************************************************/
GxEPD2_BW<GxEPD2_154_GDEY0154D67, GxEPD2_154_GDEY0154D67::HEIGHT>
    display(GxEPD2_154_GDEY0154D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

/******************************************************************************
 * RTC Slow Memory (survives deep sleep)
 ******************************************************************************/
RTC_DATA_ATTR int64_t sleep_timestamp_us = 0;
RTC_DATA_ATTR bool    has_saved_state    = false;

/******************************************************************************
 * Emulator State Buffers
 ******************************************************************************/
bool_t matrix_buffer[TAMA_LCD_HEIGHT][TAMA_LCD_WIDTH];
bool_t icon_buffer[TAMA_ICON_NUM];
bool_t prev_matrix_buffer[TAMA_LCD_HEIGHT][TAMA_LCD_WIDTH];
bool_t prev_icon_buffer[TAMA_ICON_NUM];

/******************************************************************************
 * Runtime State
 ******************************************************************************/
bool          fast_forwarding      = false;
bool          woken_by_button      = false;
unsigned long last_button_ms       = 0;
unsigned long last_screen_update_ms = 0;

/******************************************************************************
 * Button State
 ******************************************************************************/
ButtonState buttons[] = {
  { BTN_A_PIN, BTN_LEFT,   false, false, 0 },
  { BTN_B_PIN, BTN_MIDDLE, false, false, 0 },
  { BTN_C_PIN, BTN_RIGHT,  false, false, 0 },
};
const int NUM_BUTTONS = sizeof(buttons) / sizeof(buttons[0]);

/******************************************************************************
 * NVS Persistence (only needed when not using RTC storage)
 ******************************************************************************/
#if !STATE_STORAGE_RTC
Preferences preferences;
#endif

/******************************************************************************
 * ROM Loading Helper
 * The ROM is stored as packed 12-bit values in PROGMEM.
 * Every 3 bytes encode two 12-bit instructions:
 *   byte0 = (inst1_hi8),  byte1 = (inst1_lo4 << 4 | inst2_hi4),  byte2 = (inst2_lo8)
 *   inst1 = (b0 << 4) | (b1 >> 4)
 *   inst2 = ((b1 & 0x0F) << 8) | b2
 * Total: 9216 packed bytes -> 6144 12-bit instructions.
 ******************************************************************************/
u12_t rom_data[ROM_SIZE];

static void loadRomFromProgmem() {
  for (int i = 0; i < ROM_SIZE / 2; i++) {
    uint8_t b0 = pgm_read_byte(&g_program_b12[i * 3]);
    uint8_t b1 = pgm_read_byte(&g_program_b12[i * 3 + 1]);
    uint8_t b2 = pgm_read_byte(&g_program_b12[i * 3 + 2]);
    rom_data[i * 2]     = ((uint16_t)b0 << 4) | (b1 >> 4);
    rom_data[i * 2 + 1] = ((uint16_t)(b1 & 0x0F) << 8) | b2;
  }
  Serial.println("[TAMA] ROM loaded from PROGMEM (packed 12-bit format)");
}

/******************************************************************************
 * Arduino Setup
 ******************************************************************************/
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[TAMA] TamaWatchy starting...");

  // Determine wake cause
  esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
  woken_by_button = (wake_cause == ESP_SLEEP_WAKEUP_EXT1);

  if (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
    Serial.printf("[TAMA] Woke from deep sleep (cause: %s)\n",
                  woken_by_button ? "BUTTON" : "TIMER");
  } else {
    Serial.println("[TAMA] Fresh boot (not from deep sleep)");
  }

  // Initialize button pins
  pinMode(BTN_A_PIN, INPUT_PULLUP);
  pinMode(BTN_B_PIN, INPUT_PULLUP);
  pinMode(BTN_C_PIN, INPUT_PULLUP);
  pinMode(BTN_SAVE_PIN, INPUT_PULLUP);

  // Attach GPIO interrupts for instant button capture
  attachInterrupt(digitalPinToInterrupt(BTN_A_PIN), btnISR_A, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_B_PIN), btnISR_B, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_C_PIN), btnISR_C, FALLING);

  // Hold C button (cancel) for 5 seconds on boot to reset saved state
  if (digitalRead(BTN_C_PIN) == LOW) {
    Serial.println("[TAMA] C button held - hold for 5s to reset state...");
    unsigned long hold_start = millis();
    while (digitalRead(BTN_C_PIN) == LOW && millis() - hold_start < 5000) {
      delay(100);
    }
    if (millis() - hold_start >= 5000) {
      Serial.println("[TAMA] Resetting saved state!");
#if STATE_STORAGE_RTC
      extern bool rtc_state_valid;
      rtc_state_valid = false;
#else
      preferences.begin(NVS_NAMESPACE, false);
      preferences.clear();
      preferences.end();
#endif
      has_saved_state    = false;
      sleep_timestamp_us = 0;
      Serial.println("[TAMA] State cleared. Starting fresh.");
    } else {
      Serial.println("[TAMA] C button released early, keeping state.");
    }
  }

  // Initialize SPI with custom pins (SCLK, MISO, MOSI, SS)
  SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);

  // Initialize e-ink display
  // Use initial=false on timer wakes to avoid a full hardware clear
  bool display_initial = !woken_by_button && (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED)
                         ? false : true;
  display.init(115200, display_initial, 10, false);
  display.setRotation(0);

  Serial.println("[TAMA] Display initialized");

  // Load ROM from PROGMEM
  loadRomFromProgmem();

  // Initialize TamaLIB
  tamalib_register_hal(&hal);
  tamalib_set_framerate(TAMA_DISPLAY_FRAMERATE);

  if (tamalib_init(rom_data, NULL, TAMA_TIMESTAMP_FREQ)) {
    Serial.println("[TAMA] ERROR: TamaLIB init failed!");
    while (1) { delay(1000); }
  }
  Serial.println("[TAMA] TamaLIB initialized");

  // Clear previous buffers so first frame always draws
  memset(prev_matrix_buffer, 0xFF, sizeof(prev_matrix_buffer));
  memset(prev_icon_buffer, 0xFF, sizeof(prev_icon_buffer));

  // Load saved state and fast-forward if waking from deep sleep
  if (has_saved_state && wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
    if (loadStateFromNVS()) {
      // Re-sync ref timestamp before fast-forward
      cpu_sync_ref_timestamp();

      // Replay display memory into HAL buffers
      tamalib_refresh_hw();

      // Fast-forward elapsed time
      fastForwardElapsedTime();

      // Replay display memory again after fast-forward
      tamalib_refresh_hw();

      // Snap the ROM clock to the real ESP32 RTC time
      syncRtcToRom();
    }
  } else {
    // Fresh boot: try loading a saved state (no fast-forward)
    loadStateFromNVS();
  }

  // Render the screen based on wake cause
  if (woken_by_button) {
    Serial.println("[TAMA] Button wake: full refresh");
    renderScreenFull();
  } else if (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
    // Timer wake: go back to sleep (enterDeepSleep renders the sleep screen)
    Serial.println("[TAMA] Timer wake: updating sleep screen, then sleeping");
    enterDeepSleep(); // Does not return
  } else {
    // Fresh boot: full refresh
    renderScreenFull();
  }

  // If we get here, we're in interactive mode (button wake or fresh boot)
  last_button_ms       = millis();
  last_screen_update_ms = 0;

  Serial.println("[TAMA] Entering interactive mode");
  Serial.printf("[TAMA] Idle timeout: %d ms\n", IDLE_TIMEOUT_MS);
  Serial.println("[TAMA] Buttons: A=GPIO8, B=GPIO7, C=GPIO6");
}

/******************************************************************************
 * Arduino Loop (Interactive Mode)
 * Runs the emulator step-by-step with button polling and idle timeout.
 ******************************************************************************/
static timestamp_t loop_screen_ts = 0;

void loop() {
  // Run one emulator step
  tamalib_step();

  // Poll buttons
  pollButtons();

  // Update screen at configured framerate
  timestamp_t ts = (timestamp_t)micros();
  if (ts - loop_screen_ts >= (timestamp_t)(TAMA_TIMESTAMP_FREQ / TAMA_DISPLAY_FRAMERATE)) {
    loop_screen_ts = ts;
    renderScreen();
  }

  // Check idle timeout: only sleep if the ROM clock has been set (not 00:00:00)
  // Note: when isRomTimeSet() returns false we do NOT reset last_button_ms.
  // This lets the check re-poll on every loop() iteration until the 2-second
  // sampling window inside isRomTimeSet() completes, avoiding a double-wait.
  if (millis() - last_button_ms >= IDLE_TIMEOUT_MS) {
    if (isRomTimeSet()) {
      Serial.println("[TAMA] Idle timeout reached, going to deep sleep");
      enterDeepSleep(); // Does not return
    }
    // else: ROM time not confirmed yet — keep looping without resetting the timer
  }
}
