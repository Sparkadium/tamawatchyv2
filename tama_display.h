/*
 * tama_display.h - Normal game-play screen rendering.
 */
#ifndef _TAMA_DISPLAY_H_
#define _TAMA_DISPLAY_H_

#include <stdint.h>

// Draw a single 40x24 icon at (x, y).
// If hideWhenInactive is true, the bitmap is skipped when not active
// (used for icon_call which should only appear during an incoming call).
void drawIcon(int x, int y, int iconIdx, bool active, bool hideWhenInactive = false);

// Render the full frame (icons + LCD pixels) into the current GxEPD2 page.
void renderFrameContent();

// Smart partial refresh: only redraws if something changed and rate-limit allows.
void renderScreen();

// Full refresh — clears ghosting.
void renderScreenFull();

// Partial refresh — fast but may ghost.
void renderScreenPartial();

#endif /* _TAMA_DISPLAY_H_ */
