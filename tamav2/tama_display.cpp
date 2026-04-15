/*
 * tama_display.cpp - Normal game-play screen rendering.
 */
#include "Arduino.h"
#include "globals.h"
#include "icons.h"
#include "tama_display.h"

void drawIcon(int x, int y, int iconIdx, bool active, bool hideWhenInactive) {
  if (hideWhenInactive && !active) return;
  const unsigned char* bmp = (const unsigned char*)pgm_read_ptr(&icon_allArray[iconIdx]);
  // Bitmaps are inverted: 1=white, 0=black. Use fg=WHITE bg=BLACK to render correctly.
  display.drawBitmap(x, y, bmp, ICON_W, ICON_H, GxEPD_WHITE, GxEPD_BLACK);
  if (active && !hideWhenInactive) {
    // Draw 2px border outline with 2px padding around the icon
    display.drawRect(x - 2, y - 2, ICON_W + 4, ICON_H + 4, GxEPD_BLACK);
    display.drawRect(x - 3, y - 3, ICON_W + 6, ICON_H + 6, GxEPD_BLACK);
  }
}

void renderFrameContent() {
  display.fillScreen(GxEPD_WHITE);

  // Draw top icon row (indices 0-3: feed, light, play, medi)
  for (int i = 0; i < 4; i++) {
    int ix = i * ICON_SLOT_W + (ICON_SLOT_W - ICON_W) / 2;
    drawIcon(ix, TOP_ICON_ROW_Y, i, icon_buffer[i]);
  }

  // Draw bottom icon row (indices 4-7: duck, meter, scold, call)
  // icon_call (index 7, i==3) is hidden when inactive
  for (int i = 0; i < 4; i++) {
    int ix = i * ICON_SLOT_W + (ICON_SLOT_W - ICON_W) / 2;
    bool hideWhenInactive = (i == 3); // index 7 = call
    drawIcon(ix, BOT_ICON_ROW_Y, i + 4, icon_buffer[i + 4], hideWhenInactive);
  }

  // Draw main LCD area (32x16 scaled by PIXEL_SCALE)
  for (int y = 0; y < TAMA_LCD_HEIGHT; y++) {
    for (int x = 0; x < TAMA_LCD_WIDTH; x++) {
      if (matrix_buffer[y][x]) {
        int px = LCD_OFFSET_X + x * PIXEL_SCALE;
        int py = LCD_OFFSET_Y + y * PIXEL_SCALE;
        // Draw filled rectangle with 1px gap for grid effect
        display.fillRect(px, py, PIXEL_SCALE - 1, PIXEL_SCALE - 1, GxEPD_BLACK);
      }
    }
  }
}

void renderScreen() {
  // Check if anything actually changed
  bool changed = false;
  for (int y = 0; y < TAMA_LCD_HEIGHT && !changed; y++) {
    for (int x = 0; x < TAMA_LCD_WIDTH && !changed; x++) {
      if (matrix_buffer[y][x] != prev_matrix_buffer[y][x]) {
        changed = true;
      }
    }
  }
  for (int i = 0; i < TAMA_ICON_NUM && !changed; i++) {
    if (icon_buffer[i] != prev_icon_buffer[i]) {
      changed = true;
    }
  }

  if (!changed) return;

  // Rate limit: don't refresh faster than the e-ink can handle
  unsigned long now = millis();
  if (now - last_screen_update_ms < TAMA_SCREEN_MIN_MS) return;
  last_screen_update_ms = now;

  // Update previous buffers
  memcpy(prev_matrix_buffer, matrix_buffer, sizeof(matrix_buffer));
  memcpy(prev_icon_buffer, icon_buffer, sizeof(icon_buffer));

  // Use partial refresh for the full display area
  display.setPartialWindow(0, 0, EPD_WIDTH, EPD_HEIGHT);
  display.firstPage();
  do {
    renderFrameContent();
  } while (display.nextPage());
}

void renderScreenFull() {
  memcpy(prev_matrix_buffer, matrix_buffer, sizeof(matrix_buffer));
  memcpy(prev_icon_buffer, icon_buffer, sizeof(icon_buffer));

  display.setFullWindow();
  display.firstPage();
  do {
    renderFrameContent();
  } while (display.nextPage());
}

void renderScreenPartial() {
  memcpy(prev_matrix_buffer, matrix_buffer, sizeof(matrix_buffer));
  memcpy(prev_icon_buffer, icon_buffer, sizeof(icon_buffer));

  display.setPartialWindow(0, 0, EPD_WIDTH, EPD_HEIGHT);
  display.firstPage();
  do {
    renderFrameContent();
  } while (display.nextPage());
}
