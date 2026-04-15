/*
 * config.h - Hardware configuration for TamaWatchy
 *
 * *** PATCHED FOR WATCHY V2 (ESP32 classic) ***
 * Changes from V3 original marked with [V2 PATCH]
 */
#ifndef _CONFIG_H_
#define _CONFIG_H_

/******************************************************************************
 * E-Ink Display - SPI pins
 * [V2 PATCH] V3 used ESP32-S3 GPIOs (48/47/33/34/35/36).
 *            V2 uses default VSPI GPIOs.
 ******************************************************************************/
#define EPD_MOSI  23
#define EPD_SCLK  18
#define EPD_CS     5
#define EPD_DC    10
#define EPD_RST    9
#define EPD_BUSY  19

#define EPD_WIDTH  200
#define EPD_HEIGHT 200

/******************************************************************************
 * PCF8563 Hardware RTC (I2C)
 * [V2 PATCH] The Watchy V2 has a PCF8563 RTC with battery backup for
 *            accurate wall-clock time. Used to sync the Tama game clock.
 ******************************************************************************/
#define RTC_SDA    21
#define RTC_SCL    22
#define RTC_ADDR   0x51  // PCF8563 I2C address

/******************************************************************************
 * Button GPIOs
 *
 * [V2 PATCH] V3 used GPIOs 8/7/6/0 (ESP32-S3), active LOW with pull-ups.
 *            This V2 board uses active HIGH buttons (rest=0, pressed=1)
 *            with external pull-downs on the PCB.
 *
 *            Physical layout (confirmed via GPIO debug):
 *              Upper left  = GPIO 25  -> Tama A (LEFT)
 *              Lower left  = GPIO 26  -> Tama B (MIDDLE)
 *              Bottom right = GPIO  4  -> Tama C (RIGHT)
 *              Upper right  = unknown  -> not used
 ******************************************************************************/
#define BTN_A_PIN      25  // Tamagotchi LEFT (A) button — upper left
#define BTN_B_PIN      26  // Tamagotchi MIDDLE (B) button — lower left
#define BTN_C_PIN       4  // Tamagotchi RIGHT (C) button — bottom right

/* [V2 PATCH] Buttons are ACTIVE HIGH on this board (0=released, 1=pressed) */
#define BTN_ACTIVE_STATE  HIGH

#define BTN_DEBOUNCE_MS   50

/******************************************************************************
 * Tamagotchi Emulator Settings
 ******************************************************************************/
#define TAMA_DISPLAY_FRAMERATE  6       // Target framerate for tamalib (fps)
#define TAMA_TIMESTAMP_FREQ     1000000 // Microsecond resolution
#define TAMA_SCREEN_MIN_MS      300     // Minimum ms between e-ink refreshes
#define SLEEP_SCREEN_SHOW_AMPM  1       // 1 = show AM/PM on sleep screen

/******************************************************************************
 * Display Scaling
 * Tamagotchi LCD: 32x16 pixels
 * Scale factor 6: 32*6=192, 16*6=96 -> fits in 200x200 with room for icons
 ******************************************************************************/
#define TAMA_LCD_WIDTH   32
#define TAMA_LCD_HEIGHT  16
#define TAMA_ICON_NUM     8
#define PIXEL_SCALE       6

#define LCD_SCALED_W  (TAMA_LCD_WIDTH  * PIXEL_SCALE) // 192
#define LCD_SCALED_H  (TAMA_LCD_HEIGHT * PIXEL_SCALE) // 96

// Center the scaled LCD in the display
#define LCD_OFFSET_X  ((EPD_WIDTH - LCD_SCALED_W) / 2) // 4
#define LCD_OFFSET_Y  60  // Leave room for icons at top

// Icon rows (4 icons per row, each 40x24px, 200/4 = 50px per slot)
#define ICON_W       40
#define ICON_H       24
#define ICON_SLOT_W  (EPD_WIDTH / 4) // 50 px per icon slot
#define TOP_ICON_ROW_Y  ((LCD_OFFSET_Y - ICON_H) / 2)
#define BOT_ICON_ROW_Y  (LCD_OFFSET_Y + LCD_SCALED_H + ((EPD_HEIGHT - LCD_OFFSET_Y - LCD_SCALED_H - ICON_H) / 2))

/******************************************************************************
 * State Persistence
 ******************************************************************************/
#define STATE_STORAGE_RTC  1

#define NVS_NAMESPACE  "tama"
#define NVS_KEY_STATE  "state"
#define NVS_KEY_MEMORY "memory"
#define NVS_KEY_VALID  "valid"

/******************************************************************************
 * Deep Sleep Settings
 *
 * [V2 PATCH] Since buttons are active HIGH, we can use ESP_EXT1_WAKEUP_ANY_HIGH
 *            on classic ESP32 — this means ALL three Tama buttons can wake
 *            the device from deep sleep (better than the V3 port's ext0 hack).
 ******************************************************************************/
#define DEEP_SLEEP_INTERVAL_US  (60ULL * 1000000ULL)       // 1 minute
#define MAX_FAST_FORWARD_TICKS  (15UL * 60UL * 32768UL)    // 15 min @ 32768 Hz
#define IDLE_TIMEOUT_MS         10000                       // 10s no button -> sleep

/* [V2 PATCH] Bitmask for ext1 wake — all three Tama buttons.
 *            GPIO 25, 26, and 4 are all RTC GPIOs on classic ESP32.         */
#define BTN_WAKE_MASK  ((1ULL << BTN_A_PIN) | (1ULL << BTN_B_PIN) | (1ULL << BTN_C_PIN))

#endif /* _CONFIG_H_ */
