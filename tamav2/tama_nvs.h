/*
 * tama_nvs.h - State persistence (NVS flash or RTC slow memory).
 *
 * Controlled by STATE_STORAGE_RTC in config.h:
 *   0 = NVS flash (survives resets & power loss)
 *   1 = RTC slow memory (survives deep sleep only)
 */
#ifndef _TAMA_NVS_H_
#define _TAMA_NVS_H_

// Save the current TamaLIB CPU + memory state.
void saveStateToNVS();

// Load TamaLIB state.
// Returns true on success, false if no valid state exists.
bool loadStateFromNVS();

#endif /* _TAMA_NVS_H_ */
