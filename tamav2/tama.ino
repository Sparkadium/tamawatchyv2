// Copyright (C) 2026 SQFMI
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// *** PATCHED FOR WATCHY V2 (ESP32 classic) ***

#include <SPI.h>
#include <Wire.h>
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
 * [V2 PATCH] GxEPD2_154_D67 for the V2 e-ink panel.
 ******************************************************************************/
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT>
    display(GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

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
bool          fast_forwarding       = false;
bool          woken_by_button       = false;
unsigned long last_button_ms        = 0;
unsigned long last_screen_update_ms = 0;

/******************************************************************************
 * Button State
 ******************************************************************************/
ButtonState buttons[] = {
    { BTN_A_PIN,  BTN_LEFT,   false, false, 0 },
    { BTN_B_PIN,  BTN_MIDDLE, false, false, 0 },
    { BTN_C_PIN,  BTN_RIGHT,  false, false, 0 },
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
 * PCF8563 Hardware RTC Reader
 * Reads wall-clock time from the Watchy V2's battery-backed RTC and sets
 * the ESP32's internal RTC to match. Returns true if a valid time was read.
 ******************************************************************************/
static uint8_t bcd2dec(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }
static uint8_t dec2bcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

static bool syncFromHardwareRTC() {
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(0x02);  // Start at seconds register
    if (Wire.endTransmission() != 0) {
        Serial.println("[RTC] PCF8563 not responding on I2C");
        return false;
    }

    Wire.requestFrom((uint8_t)RTC_ADDR, (uint8_t)7);
    if (Wire.available() < 7) {
        Serial.println("[RTC] PCF8563 read failed");
        return false;
    }

    uint8_t raw_sec  = Wire.read();
    uint8_t raw_min  = Wire.read();
    uint8_t raw_hour = Wire.read();
    uint8_t raw_day  = Wire.read();
    uint8_t raw_wday = Wire.read();
    uint8_t raw_mon  = Wire.read();
    uint8_t raw_year = Wire.read();

    // Check voltage-low flag (bit 7 of seconds register)
    if (raw_sec & 0x80) {
        Serial.println("[RTC] PCF8563 voltage-low flag set — time may be invalid");
    }

    uint8_t ss = bcd2dec(raw_sec & 0x7F);
    uint8_t mm = bcd2dec(raw_min & 0x7F);
    uint8_t hh = bcd2dec(raw_hour & 0x3F);
    uint8_t dd = bcd2dec(raw_day & 0x3F);
    uint8_t mo = bcd2dec(raw_mon & 0x1F);
    uint8_t yy = bcd2dec(raw_year);

    // Build struct tm and set ESP32 internal RTC
    struct tm t = {};
    t.tm_sec  = ss;
    t.tm_min  = mm;
    t.tm_hour = hh;
    t.tm_mday = dd;
    t.tm_mon  = mo - 1;        // struct tm months are 0-11
    t.tm_year = yy + 100;      // struct tm years since 1900; PCF8563 year 0-99 = 2000-2099

    struct timeval tv = { .tv_sec = mktime(&t), .tv_usec = 0 };
    settimeofday(&tv, NULL);

    Serial.printf("[RTC] PCF8563 -> ESP32 RTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                  yy + 2000, mo, dd, hh, mm, ss);
    return true;
}

static void writeHardwareRTC(uint8_t hh, uint8_t mm, uint8_t ss) {
    // Read current date registers first (preserve date, only update time)
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(0x02);  // Seconds register
    Wire.write(dec2bcd(ss));
    Wire.write(dec2bcd(mm));
    Wire.write(dec2bcd(hh));
    Wire.endTransmission();
    Serial.printf("[RTC] Wrote time to PCF8563: %02d:%02d:%02d\n", hh, mm, ss);
}

/******************************************************************************
 * Arduino Setup
 ******************************************************************************/
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[TAMA] TamaWatchy V2 starting...");

    // Determine wake cause
    // [V2 PATCH] Using EXT1 (active HIGH buttons allow ESP_EXT1_WAKEUP_ANY_HIGH)
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
    woken_by_button = (wake_cause == ESP_SLEEP_WAKEUP_EXT1);

    if (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        Serial.printf("[TAMA] Woke from deep sleep (cause: %s)\n",
                      woken_by_button ? "BUTTON" : "TIMER");
    } else {
        Serial.println("[TAMA] Fresh boot (not from deep sleep)");
    }

    // [V2 PATCH] Initialize button pins — INPUT (no internal pull) since the
    //            V2 PCB has external pull-downs. Buttons are active HIGH.
    pinMode(BTN_A_PIN, INPUT);
    pinMode(BTN_B_PIN, INPUT);
    pinMode(BTN_C_PIN, INPUT);

    // [V2 PATCH] RISING edge — buttons go LOW->HIGH on press
    attachInterrupt(digitalPinToInterrupt(BTN_A_PIN), btnISR_A, RISING);
    attachInterrupt(digitalPinToInterrupt(BTN_B_PIN), btnISR_B, RISING);
    attachInterrupt(digitalPinToInterrupt(BTN_C_PIN), btnISR_C, RISING);

    // [V2 PATCH] Hold C button for 5s to reset — check for HIGH (active HIGH)
    if (digitalRead(BTN_C_PIN) == BTN_ACTIVE_STATE) {
        Serial.println("[TAMA] C button held - hold for 5s to reset state...");
        unsigned long hold_start = millis();
        while (digitalRead(BTN_C_PIN) == BTN_ACTIVE_STATE && millis() - hold_start < 5000) {
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

    // Initialize I2C for PCF8563 hardware RTC
    Wire.begin(RTC_SDA, RTC_SCL);
    syncFromHardwareRTC();

    // Initialize SPI
    SPI.begin(EPD_SCLK, -1, EPD_MOSI, EPD_CS);

    // Initialize e-ink display
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
    memset(prev_icon_buffer,   0xFF, sizeof(prev_icon_buffer));

    // Load saved state and fast-forward if waking from deep sleep
    if (has_saved_state && wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        if (loadStateFromNVS()) {
            cpu_sync_ref_timestamp();
            tamalib_refresh_hw();
            fastForwardElapsedTime();
            tamalib_refresh_hw();
            syncRtcToRom();
        }
    } else {
        loadStateFromNVS();
    }

    // Render the screen based on wake cause
    if (woken_by_button) {
        Serial.println("[TAMA] Button wake: full refresh");
        renderScreenFull();
    } else if (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        Serial.println("[TAMA] Timer wake: updating sleep screen, then sleeping");
        enterDeepSleep();
    } else {
        renderScreenFull();
    }

    last_button_ms        = millis();
    last_screen_update_ms = 0;
    Serial.println("[TAMA] Entering interactive mode");
    Serial.printf("[TAMA] Idle timeout: %d ms\n", IDLE_TIMEOUT_MS);
    Serial.printf("[TAMA] Buttons (active HIGH): A=GPIO%d, B=GPIO%d, C=GPIO%d\n",
                  BTN_A_PIN, BTN_B_PIN, BTN_C_PIN);
}

/******************************************************************************
 * Clock Resync Combo (A + C held for 2 seconds)
 * Reads wall-clock time from the PCF8563 hardware RTC and writes it into
 * both the ESP32 internal RTC and the Tama ROM clock. Pet state is untouched.
 *
 * To SET the time: use the Tama's built-in time setting (press A on the
 * clock screen), or flash stock Watchy firmware once to set the PCF8563,
 * then re-flash TamaWatchy.
 ******************************************************************************/
static unsigned long combo_start_ms = 0;
#define COMBO_HOLD_MS 2000

static void checkClockResyncCombo() {
    bool a_held = (digitalRead(BTN_A_PIN) == BTN_ACTIVE_STATE);
    bool c_held = (digitalRead(BTN_C_PIN) == BTN_ACTIVE_STATE);

    if (a_held && c_held) {
        if (combo_start_ms == 0) {
            combo_start_ms = millis();
        } else if (millis() - combo_start_ms >= COMBO_HOLD_MS) {
            Serial.println("[TAMA] A+C combo: resyncing from hardware RTC");
            if (syncFromHardwareRTC()) {
                syncRtcToRom();  // ESP32 RTC is now correct, push to Tama
                Serial.println("[TAMA] Clock resynced successfully");
            } else {
                Serial.println("[TAMA] Hardware RTC read failed, clock not updated");
            }
            renderScreenFull();
            combo_start_ms = 0;
            last_button_ms = millis();
        }
    } else {
        combo_start_ms = 0;
    }
}

/******************************************************************************
 * Arduino Loop (Interactive Mode)
 ******************************************************************************/
static timestamp_t loop_screen_ts = 0;

void loop() {
    tamalib_step();
    pollButtons();
    checkClockResyncCombo();

    timestamp_t ts = (timestamp_t)micros();
    if (ts - loop_screen_ts >= (timestamp_t)(TAMA_TIMESTAMP_FREQ / TAMA_DISPLAY_FRAMERATE)) {
        loop_screen_ts = ts;
        renderScreen();
    }

    if (millis() - last_button_ms >= IDLE_TIMEOUT_MS) {
        if (isRomTimeSet()) {
            Serial.println("[TAMA] Idle timeout reached, going to deep sleep");
            enterDeepSleep();
        }
    }
}
