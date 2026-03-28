/*
 * config.h - Hardware configuration for TamaWatchy
 */
#ifndef _CONFIG_H_
#define _CONFIG_H_

/******************************************************************************
 * E-Ink Display (GDEY0154D67 200x200) - SPI pins
 ******************************************************************************/
#define EPD_MOSI        48
#define EPD_SCLK        47
#define EPD_CS          33
#define EPD_DC          34
#define EPD_RST         35
#define EPD_BUSY        36

#define EPD_WIDTH       200
#define EPD_HEIGHT      200

/******************************************************************************
 * Button GPIOs (active LOW, internal pull-up)
 ******************************************************************************/
#define BTN_A_PIN       8   // Tamagotchi LEFT  (A) button
#define BTN_B_PIN       7   // Tamagotchi MIDDLE (B) button
#define BTN_C_PIN       6   // Tamagotchi RIGHT  (C) button
#define BTN_SAVE_PIN    0   // Manual save button

#define BTN_DEBOUNCE_MS 50  // Debounce time in milliseconds

/******************************************************************************
 * Tamagotchi Emulator Settings
 ******************************************************************************/
#define TAMA_DISPLAY_FRAMERATE  6       // Target framerate for tamalib (fps)
#define TAMA_TIMESTAMP_FREQ     1000000 // Microsecond resolution
#define TAMA_SCREEN_MIN_MS      300     // Minimum ms between e-ink refreshes
#define SLEEP_SCREEN_SHOW_AMPM  1       // 1 = show AM/PM indicator on sleep screen, 0 = hide

/******************************************************************************
 * Display Scaling
 * Tamagotchi LCD: 32x16 pixels
 * Scale factor 6: 32*6=192, 16*6=96  -> fits in 200x200 with room for icons
 ******************************************************************************/
#define TAMA_LCD_WIDTH   32
#define TAMA_LCD_HEIGHT  16
#define TAMA_ICON_NUM    8

#define PIXEL_SCALE      6
#define LCD_SCALED_W     (TAMA_LCD_WIDTH  * PIXEL_SCALE) // 192
#define LCD_SCALED_H     (TAMA_LCD_HEIGHT * PIXEL_SCALE) // 96

// Center the scaled LCD in the display
#define LCD_OFFSET_X     ((EPD_WIDTH  - LCD_SCALED_W) / 2) // 4
#define LCD_OFFSET_Y     60  // Leave room for icons at top

// Icon rows (4 icons per row, each 40x24px, 200/4 = 50px per slot)
#define ICON_W           40
#define ICON_H           24
#define ICON_SLOT_W      (EPD_WIDTH / 4)              // 50 px per icon slot
#define TOP_ICON_ROW_Y   ((LCD_OFFSET_Y - ICON_H) / 2) // Centered above LCD
#define BOT_ICON_ROW_Y   (LCD_OFFSET_Y + LCD_SCALED_H + ((EPD_HEIGHT - LCD_OFFSET_Y - LCD_SCALED_H - ICON_H) / 2))

/******************************************************************************
 * State Persistence
 * STATE_STORAGE_RTC = 0 : save to NVS flash (survives resets & power loss)
 * STATE_STORAGE_RTC = 1 : save to RTC slow memory (survives deep sleep only,
 *                         lost on reset/power-off — faster, no flash wear)
 ******************************************************************************/
#define STATE_STORAGE_RTC       1

#define NVS_NAMESPACE       "tama"
#define NVS_KEY_STATE       "state"
#define NVS_KEY_MEMORY      "memory"
#define NVS_KEY_VALID       "valid"

/******************************************************************************
 * Deep Sleep Settings
 ******************************************************************************/
#define DEEP_SLEEP_INTERVAL_US   (60ULL * 1000000ULL)       // 1 minute
#define MAX_FAST_FORWARD_TICKS   (15UL * 60UL * 32768UL)    // 15 min @ 32768 Hz
#define IDLE_TIMEOUT_MS          10000                       // 10s no button -> sleep
#define BTN_WAKE_MASK            ((1ULL << BTN_A_PIN) | (1ULL << BTN_B_PIN) | (1ULL << BTN_C_PIN))

#endif /* _CONFIG_H_ */
