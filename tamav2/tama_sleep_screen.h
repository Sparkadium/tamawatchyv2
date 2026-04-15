/*
 * tama_sleep_screen.h - RTC clock sleep screen rendering.
 */
#ifndef _TAMA_SLEEP_SCREEN_H_
#define _TAMA_SLEEP_SCREEN_H_

#include <stdint.h>

// Draw a tama_font glyph scaled up. Each row is one byte, MSB-first.
// cols/rows default to 4×7 (digit glyphs); pass 6×8 for AM/PM glyphs.
void drawGlyph(int x, int y, const unsigned char* glyph, int scale, uint16_t color,
               int cols = 4, int rows = 7);

// Render the sleep screen content into the current GxEPD2 page:
// shows HH:MM at the top, LCD pixels in the middle, no icons
// (except icon_call if an incoming call is active).
void renderSleepContent();

// Full refresh of the sleep screen.
void renderSleepScreen();

#endif /* _TAMA_SLEEP_SCREEN_H_ */
