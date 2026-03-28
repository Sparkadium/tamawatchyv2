/*
 * tama_power.cpp - Power management: deep sleep entry and fast-forward on wake.
 */
#include "Arduino.h"
#include "globals.h"
#include "tama_nvs.h"
#include "tama_sleep_screen.h"
#include "tama_power.h"

int64_t getSystemTimeUs() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
}

bool isRomTimeSet() {
  // ROM clock only ticks after the user has set the time in the game UI.
  // Strategy: sample the seconds nibbles, wait 2 seconds, check if they advanced.
  // Once confirmed as ticking, cache the result permanently.
  static bool    result_cached = false;
  static uint8_t initial_ss_u  = 0xFF;  // 0xFF = not yet sampled
  static uint8_t initial_ss_t  = 0xFF;
  static unsigned long sample_ms = 0;

  if (result_cached) return true;

  state_t *state = tamalib_get_state();
  MEM_BUFFER_TYPE *mem = state->memory;
  uint8_t ss_u = GET_MEMORY(mem, 0x10);
  uint8_t ss_t = GET_MEMORY(mem, 0x11);

  if (initial_ss_u == 0xFF) {
    // First call — record baseline and start the 2-second window
    initial_ss_u = ss_u;
    initial_ss_t = ss_t;
    sample_ms = millis();
    Serial.printf("[TAMA] ROM clock: sampling baseline seconds %d%d\n", ss_t, ss_u);
    return false;
  }

  if (millis() - sample_ms < 2000) {
    return false;  // Still within the sampling window
  }

  bool advanced = (ss_u != initial_ss_u || ss_t != initial_ss_t);
  Serial.printf("[TAMA] ROM clock: was %d%d, now %d%d -> %s\n",
                initial_ss_t, initial_ss_u, ss_t, ss_u, advanced ? "TICKING (set)" : "STOPPED (unset)");

  if (advanced) {
    result_cached = true;  // Clock is running — allow sleep from now on
    syncRomToRtc();        // First time clock is set — sync internal RTC to ROM time
  } else {
    // Clock still not ticking — reset so we sample again next idle timeout
    initial_ss_u = 0xFF;
  }
  return advanced;
}

void syncRomToRtc() {
  // Read the ROM's nibble-addressed time registers and set the internal RTC.
  state_t *state = tamalib_get_state();
  MEM_BUFFER_TYPE *mem = state->memory;

  uint8_t ss = GET_MEMORY(mem, 0x11) * 10 + GET_MEMORY(mem, 0x10);
  uint8_t mm = GET_MEMORY(mem, 0x13) * 10 + GET_MEMORY(mem, 0x12);
  uint8_t hh = (GET_MEMORY(mem, 0x15) << 4) | GET_MEMORY(mem, 0x14);

  // Build a struct tm with today's date and the ROM's time
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  struct tm t;
  localtime_r(&tv_now.tv_sec, &t);
  t.tm_hour = hh;
  t.tm_min  = mm;
  t.tm_sec  = ss;

  struct timeval tv_new = { .tv_sec = mktime(&t), .tv_usec = 0 };
  settimeofday(&tv_new, NULL);

  Serial.printf("[TAMA] ROM -> Internal RTC: %02d:%02d:%02d\n", hh, mm, ss);
}

void syncRtcToRom() {
  // Read the internal RTC
  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm t;
  localtime_r(&tv.tv_sec, &t);

  uint8_t ss = (uint8_t)t.tm_sec;
  uint8_t mm = (uint8_t)t.tm_min;
  uint8_t hh = (uint8_t)t.tm_hour;

  state_t *state = tamalib_get_state();
  MEM_BUFFER_TYPE *mem = state->memory;

  SET_MEMORY(mem, 0x10, ss % 10);
  SET_MEMORY(mem, 0x11, ss / 10);
  SET_MEMORY(mem, 0x12, mm % 10);
  SET_MEMORY(mem, 0x13, mm / 10);
  SET_MEMORY(mem, 0x14, hh & 0x0F);
  SET_MEMORY(mem, 0x15, hh >> 4);

  Serial.printf("[TAMA] Internal RTC -> ROM: %02d:%02d:%02d\n", hh, mm, ss);
}

void enterDeepSleep() {
  Serial.println("[TAMA] Saving state and entering deep sleep...");
  saveStateToNVS();

  // Record the current RTC time so we can calculate elapsed on next wake
  sleep_timestamp_us = getSystemTimeUs();
  has_saved_state = true;

  // Render the sleep screen: shows RTC time instead of icons
  renderSleepScreen();

  // Calculate sleep duration to wake at the top of the next minute
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  int seconds_into_minute = tv_now.tv_sec % 60;
  int usec_into_second    = tv_now.tv_usec;
  uint64_t sleep_us = (uint64_t)(60 - seconds_into_minute) * 1000000ULL
                     - (uint64_t)usec_into_second;
  if (sleep_us < 1000000ULL) sleep_us += 60ULL * 1000000ULL;  // avoid sleeping < 1s

  Serial.printf("[TAMA] Sleeping for %llu us (next minute boundary)\n", sleep_us);
  esp_sleep_enable_timer_wakeup(sleep_us);
  esp_sleep_enable_ext1_wakeup(BTN_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);

  // Put the e-ink display to sleep to minimize power draw
  display.hibernate();

  Serial.println("[TAMA] Entering deep sleep");
  Serial.flush();
  esp_deep_sleep_start();
  // Never returns
}

void fastForwardElapsedTime() {
  int64_t now_us     = getSystemTimeUs();
  int64_t elapsed_us = now_us - sleep_timestamp_us;

  if (elapsed_us <= 0) {
    Serial.println("[TAMA] No time elapsed, skipping fast-forward");
    return;
  }

  // Convert elapsed microseconds to emulator ticks (32768 Hz)
  // elapsed_ticks = elapsed_us * 32768 / 1000000
  uint32_t elapsed_ticks = (uint32_t)((elapsed_us * 32768LL) / 1000000LL);

  // Clamp to maximum fast-forward
  if (elapsed_ticks > MAX_FAST_FORWARD_TICKS) {
    Serial.printf("[TAMA] Clamping fast-forward from %u to %u ticks\n",
                  elapsed_ticks, (uint32_t)MAX_FAST_FORWARD_TICKS);
    elapsed_ticks = MAX_FAST_FORWARD_TICKS;
  }

  Serial.printf("[TAMA] Fast-forwarding %u ticks (~%u seconds of Tama time)\n",
                elapsed_ticks, elapsed_ticks / 32768);

  // Get current tick counter from emulator state
  state_t *state      = tamalib_get_state();
  uint32_t start_tick  = *(state->tick_counter);
  uint32_t target_tick = start_tick + elapsed_ticks;

  // Enable fast-forward mode
  fast_forwarding = true;
  tamalib_set_speed(0); // No throttle

  unsigned long ff_start_ms = millis();

  // Tight loop: step until tick_counter reaches target
  while (*(state->tick_counter) < target_tick) {
    tamalib_step();

    // Safety: abort if FF takes too long (> 60 seconds wall time)
    if (millis() - ff_start_ms > 60000) {
      Serial.println("[TAMA] Fast-forward safety timeout (60s), aborting");
      break;
    }
  }

  unsigned long ff_duration_ms = millis() - ff_start_ms;

  // Restore normal speed and sync timestamps
  tamalib_set_speed(1);
  cpu_sync_ref_timestamp();
  fast_forwarding = false;

  Serial.printf("[TAMA] Fast-forward complete in %lu ms\n", ff_duration_ms);
}
