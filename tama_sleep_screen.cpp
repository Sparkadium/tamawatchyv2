/*
 * tama_sleep_screen.cpp - RTC clock sleep screen rendering.
 */
#include "Arduino.h"
#include "globals.h"
#include "tama_font.h"
#include "tama_display.h"
#include "tama_sleep_screen.h"

void drawGlyph(int x, int y, const unsigned char* glyph, int scale, uint16_t color,
               int cols, int rows) {
  for (int row = 0; row < rows; row++) {
    uint8_t bits = pgm_read_byte(&glyph[row]);
    for (int col = 0; col < cols; col++) {
      if (bits & (0x80 >> col)) {
        display.fillRect(x + col * scale, y + row * scale, scale - 1, scale - 1, color);
      }
    }
  }
}

void renderSleepContent() {
  display.fillScreen(GxEPD_WHITE);

  // Read current RTC time
  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm timeinfo;
  localtime_r(&tv.tv_sec, &timeinfo);
  int hh = timeinfo.tm_hour;
  int mm = timeinfo.tm_min;

  // Draw HH:MM using tama_font glyphs at 6x scale (each glyph: 24x42px)
  // Five glyphs: tens-hour, units-hour, colon (idx 10), tens-min, units-min
  const int scale   = 6;
  const int glyphW  = 4 * scale;  // 24px
  const int glyphH  = 7 * scale;  // 42px
  // AM/PM indicator: index 11 = am, 12 = pm; glyph is 6 cols x 8 rows
#if SLEEP_SCREEN_SHOW_AMPM
  const int ampmIdx  = (hh >= 12) ? 12 : 11;
  const int ampmCols = 6;
  const int ampmRows = 8;
  const int ampmW    = ampmCols * scale;  // 36px
  const int ampmGap  = scale;             // 1 scaled-pixel gap
#endif

  // Per-glyph advance widths:
  //   digits advance by glyphW (24px); add one extra scale unit (6px) after
  //   the tens digit of each pair to give a 1 scaled-pixel gap within HH and MM.
  //   colon uses a narrower 3-column slot (18px).
  const int advance[5] = { glyphW + scale, glyphW, 3 * scale, glyphW + scale, glyphW };
  int totalW = 0;
  for (int i = 0; i < 5; i++) totalW += advance[i];
#if SLEEP_SCREEN_SHOW_AMPM
  totalW += ampmGap + ampmW;
#endif
  const int startX  = (EPD_WIDTH - totalW) / 2;
  const int startY  = (TOP_ICON_ROW_Y + (ICON_H - glyphH) / 2);  // vertically centred in icon row

  // Convert to 12-hour format when showing AM/PM
#if SLEEP_SCREEN_SHOW_AMPM
  int dispHour = hh % 12;
  if (dispHour == 0) dispHour = 12;
#else
  int dispHour = hh;
#endif
  int digits[5] = { dispHour / 10, dispHour % 10, 10 /* colon */, mm / 10, mm % 10 };
  int cx = startX;
  for (int i = 0; i < 5; i++) {
    const unsigned char* glyph = (const unsigned char*)pgm_read_ptr(&allArray[digits[i]]);
    drawGlyph(cx, startY, glyph, scale, GxEPD_BLACK);
    cx += advance[i];
  }

  // Draw AM/PM glyph to the right, vertically centred with the digits
#if SLEEP_SCREEN_SHOW_AMPM
  const int ampmY = startY + (glyphH - ampmRows * scale) / 2;
  const unsigned char* ampmGlyph = (const unsigned char*)pgm_read_ptr(&allArray[ampmIdx]);
  drawGlyph(cx + ampmGap, ampmY, ampmGlyph, scale, GxEPD_BLACK, ampmCols, ampmRows);
#endif

  // Draw main LCD pixel area (no icons)
  for (int y = 0; y < TAMA_LCD_HEIGHT; y++) {
    for (int x = 0; x < TAMA_LCD_WIDTH; x++) {
      if (matrix_buffer[y][x]) {
        int px = LCD_OFFSET_X + x * PIXEL_SCALE;
        int py = LCD_OFFSET_Y + y * PIXEL_SCALE;
        display.fillRect(px, py, PIXEL_SCALE - 1, PIXEL_SCALE - 1, GxEPD_BLACK);
      }
    }
  }

  // Show call icon in its normal bottom-row position if there is an incoming call
  if (icon_buffer[7]) {
    int ix = 3 * ICON_SLOT_W + (ICON_SLOT_W - ICON_W) / 2;  // slot 3 (rightmost)
    drawIcon(ix, BOT_ICON_ROW_Y, 7, true, true);
  }
}

void renderSleepScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    renderSleepContent();
  } while (display.nextPage());
}
