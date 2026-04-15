/*
 * tama_hal.cpp - TamaLIB hardware abstraction layer and button polling.
 *
 * *** PATCHED FOR WATCHY V2 (ESP32 classic) ***
 */

#include "Arduino.h"
#include "globals.h"
#include "tama_display.h"
#include "tama_hal.h"

/******************************************************************************
 * GPIO Interrupt Flags
 * [V2 PATCH] ISRs fire on RISING edge (button press goes LOW->HIGH).
 ******************************************************************************/
static volatile bool btn_interrupt_flags[3] = {false, false, false};

void IRAM_ATTR btnISR_A() { btn_interrupt_flags[0] = true; }
void IRAM_ATTR btnISR_B() { btn_interrupt_flags[1] = true; }
void IRAM_ATTR btnISR_C() { btn_interrupt_flags[2] = true; }

/******************************************************************************
 * HAL Callbacks
 ******************************************************************************/
static void hal_halt(void) {
}

static bool_t hal_is_log_enabled(log_level_t level) {
    return (level == LOG_ERROR) ? 1 : 0;
}

static void hal_log(log_level_t level, char *buff, ...) {
    if (level == LOG_ERROR) {
        va_list args;
        va_start(args, buff);
        char msg[128];
        vsnprintf(msg, sizeof(msg), buff, args);
        va_end(args);
        Serial.print("[TAMA ERR] ");
        Serial.println(msg);
    }
}

static void hal_sleep_until(timestamp_t ts) {
    if (fast_forwarding) return;

    while (true) {
        timestamp_t now = (timestamp_t)micros();
        int32_t remaining = (int32_t)(ts - now);
        if (remaining <= 0 || remaining >= 1000000) break;
        pollButtons();
    }
}

static timestamp_t hal_get_timestamp(void) {
    return (timestamp_t)micros();
}

static void hal_update_screen(void) {
    if (!fast_forwarding) renderScreen();
}

static void hal_set_lcd_matrix(u8_t x, u8_t y, bool_t val) {
    if (x < TAMA_LCD_WIDTH && y < TAMA_LCD_HEIGHT) {
        matrix_buffer[y][x] = val;
    }
}

static void hal_set_lcd_icon(u8_t icon, bool_t val) {
    if (icon < TAMA_ICON_NUM) {
        icon_buffer[icon] = val;
    }
}

static void hal_set_frequency(u32_t freq) {
    (void)freq;
}

static void hal_play_frequency(bool_t en) {
    (void)en;
}

static void* hal_malloc(u32_t size) {
    return malloc(size);
}

static void hal_free(void *ptr) {
    free(ptr);
}

/******************************************************************************
 * Button Polling
 *
 * [V2 PATCH] Changed active-LOW logic to active-HIGH:
 *   - digitalRead() == BTN_ACTIVE_STATE (HIGH) means pressed
 *   - ISR flags still act as instant-capture for press events
 ******************************************************************************/
void pollButtons() {
    unsigned long now = millis();

    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool isr_pressed = btn_interrupt_flags[i];
        if (isr_pressed) {
            btn_interrupt_flags[i] = false;
        }

        // [V2 PATCH] Check for HIGH (active HIGH) instead of LOW
        bool current = (digitalRead(buttons[i].pin) == BTN_ACTIVE_STATE) || isr_pressed;

        if (current != buttons[i].stable_state) {
            if (current != buttons[i].pending_state) {
                buttons[i].pending_state = current;
                buttons[i].pending_since = now;
            } else if (now - buttons[i].pending_since >= BTN_DEBOUNCE_MS) {
                buttons[i].stable_state = current;
                tamalib_set_button(buttons[i].tama_btn,
                                   current ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
                if (current) {
                    last_button_ms = now;
                }
            }
        } else {
            buttons[i].pending_state = current;
        }
    }
}

static int hal_handler(void) {
    pollButtons();
    return 0;
}

hal_t hal = {
    .malloc         = &hal_malloc,
    .free           = &hal_free,
    .halt           = &hal_halt,
    .is_log_enabled = &hal_is_log_enabled,
    .log            = &hal_log,
    .sleep_until    = &hal_sleep_until,
    .get_timestamp  = &hal_get_timestamp,
    .update_screen  = &hal_update_screen,
    .set_lcd_matrix = &hal_set_lcd_matrix,
    .set_lcd_icon   = &hal_set_lcd_icon,
    .set_frequency  = &hal_set_frequency,
    .play_frequency = &hal_play_frequency,
    .handler        = &hal_handler,
};
