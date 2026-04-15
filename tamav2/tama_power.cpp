/*
 * tama_power.cpp - Power management: deep sleep entry and fast-forward on wake.
 *
 * *** PATCHED FOR WATCHY V2 (ESP32 classic) ***
 */

#include "Arduino.h"
#include <Wire.h>
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
    static bool          result_cached  = false;
    static uint8_t       initial_ss_u   = 0xFF;
    static uint8_t       initial_ss_t   = 0xFF;
    static unsigned long sample_ms      = 0;

    if (result_cached) return true;

    state_t        *state = tamalib_get_state();
    MEM_BUFFER_TYPE *mem  = state->memory;

    uint8_t ss_u = GET_MEMORY(mem, 0x10);
    uint8_t ss_t = GET_MEMORY(mem, 0x11);

    if (initial_ss_u == 0xFF) {
        initial_ss_u = ss_u;
        initial_ss_t = ss_t;
        sample_ms    = millis();
        Serial.printf("[TAMA] ROM clock: sampling baseline seconds %d%d\n", ss_t, ss_u);
        return false;
    }

    if (millis() - sample_ms < 2000) {
        return false;
    }

    bool advanced = (ss_u != initial_ss_u || ss_t != initial_ss_t);
    Serial.printf("[TAMA] ROM clock: was %d%d, now %d%d -> %s\n",
                  initial_ss_t, initial_ss_u, ss_t, ss_u,
                  advanced ? "TICKING (set)" : "STOPPED (unset)");

    if (advanced) {
        result_cached = true;
        syncRomToRtc();
    } else {
        initial_ss_u = 0xFF;
    }

    return advanced;
}

void syncRomToRtc() {
    state_t        *state = tamalib_get_state();
    MEM_BUFFER_TYPE *mem  = state->memory;

    uint8_t ss = GET_MEMORY(mem, 0x11) * 10 + GET_MEMORY(mem, 0x10);
    uint8_t mm = GET_MEMORY(mem, 0x13) * 10 + GET_MEMORY(mem, 0x12);
    uint8_t hh = (GET_MEMORY(mem, 0x15) << 4) | GET_MEMORY(mem, 0x14);

    // Set ESP32 internal RTC
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

    // [V2 PATCH] Also write to PCF8563 hardware RTC so it persists across
    //            power cycles and deep sleep. This is what makes the Tama's
    //            time-set UI effectively set the real wall clock.
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(0x02);                                // Start at seconds register
    Wire.write(((ss / 10) << 4) | (ss % 10));       // Seconds (BCD)
    Wire.write(((mm / 10) << 4) | (mm % 10));       // Minutes (BCD)
    Wire.write(((hh / 10) << 4) | (hh % 10));       // Hours (BCD)
    Wire.endTransmission();

    Serial.printf("[TAMA] ROM -> PCF8563 hardware RTC: %02d:%02d:%02d\n", hh, mm, ss);
}

void syncRtcToRom() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm t;
    localtime_r(&tv.tv_sec, &t);

    uint8_t ss = (uint8_t)t.tm_sec;
    uint8_t mm = (uint8_t)t.tm_min;
    uint8_t hh = (uint8_t)t.tm_hour;

    state_t        *state = tamalib_get_state();
    MEM_BUFFER_TYPE *mem  = state->memory;

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

    sleep_timestamp_us = getSystemTimeUs();
    has_saved_state    = true;

    renderSleepScreen();

    // Calculate sleep duration to wake at the top of the next minute
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int seconds_into_minute = tv_now.tv_sec % 60;
    int usec_into_second    = tv_now.tv_usec;

    uint64_t sleep_us = (uint64_t)(60 - seconds_into_minute) * 1000000ULL
                        - (uint64_t)usec_into_second;
    if (sleep_us < 1000000ULL) sleep_us += 60ULL * 1000000ULL;

    Serial.printf("[TAMA] Sleeping for %llu us (next minute boundary)\n", sleep_us);

    esp_sleep_enable_timer_wakeup(sleep_us);

    /*************************************************************************
     * [V2 PATCH] Deep sleep button wake
     *
     * This V2 board has active-HIGH buttons (pressed = HIGH, released = LOW).
     * Classic ESP32 supports ESP_EXT1_WAKEUP_ANY_HIGH — wake when ANY of the
     * selected GPIOs goes HIGH. This is a perfect match: all three Tama
     * buttons can wake the device from deep sleep.
     *
     * This is actually better than the V3 original (which used ANY_LOW on
     * ESP32-S3) because the behavior is identical — any button press wakes.
     *************************************************************************/
    esp_sleep_enable_ext1_wakeup(BTN_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);

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

    uint32_t elapsed_ticks = (uint32_t)((elapsed_us * 32768LL) / 1000000LL);

    if (elapsed_ticks > MAX_FAST_FORWARD_TICKS) {
        Serial.printf("[TAMA] Clamping fast-forward from %u to %u ticks\n",
                      elapsed_ticks, (uint32_t)MAX_FAST_FORWARD_TICKS);
        elapsed_ticks = MAX_FAST_FORWARD_TICKS;
    }

    Serial.printf("[TAMA] Fast-forwarding %u ticks (~%u seconds of Tama time)\n",
                  elapsed_ticks, elapsed_ticks / 32768);

    state_t  *state       = tamalib_get_state();
    uint32_t  start_tick  = *(state->tick_counter);
    uint32_t  target_tick = start_tick + elapsed_ticks;

    fast_forwarding = true;
    tamalib_set_speed(0);

    unsigned long ff_start_ms = millis();

    while (*(state->tick_counter) < target_tick) {
        tamalib_step();
        if (millis() - ff_start_ms > 60000) {
            Serial.println("[TAMA] Fast-forward safety timeout (60s), aborting");
            break;
        }
    }

    unsigned long ff_duration_ms = millis() - ff_start_ms;

    tamalib_set_speed(1);
    cpu_sync_ref_timestamp();
    fast_forwarding = false;

    Serial.printf("[TAMA] Fast-forward complete in %lu ms\n", ff_duration_ms);
}
