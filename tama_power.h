/*
 * tama_power.h - Power management: deep sleep entry and fast-forward on wake.
 */
#ifndef _TAMA_POWER_H_
#define _TAMA_POWER_H_

#include <stdint.h>

// Returns true if the Tamagotchi ROM clock has been set (time != 00:00:00).
// Use to suppress idle sleep on fresh boot before the user has set the clock.
bool isRomTimeSet();

// Sync hours/minutes/seconds from the ROM memory into the internal RTC.
// Called automatically by isRomTimeSet() when the clock first starts ticking.
void syncRomToRtc();

// Sync hours/minutes/seconds from the internal RTC into the ROM memory
// at addresses 0x10-0x15 (Tamagotchi time registers).
// Call after loadStateFromNVS() and fastForwardElapsedTime().
void syncRtcToRom();

// Return the current wall-clock time in microseconds (survives deep sleep).
int64_t getSystemTimeUs();

// Save state, render the sleep screen, configure wake sources, and enter deep sleep.
// Does not return.
void enterDeepSleep();

// Run the emulator at maximum speed to catch up elapsed time since last sleep.
void fastForwardElapsedTime();

#endif /* _TAMA_POWER_H_ */
