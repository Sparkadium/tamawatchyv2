/*
 * tama_nvs.cpp - State persistence (NVS flash or RTC slow memory).
 *
 * When STATE_STORAGE_RTC == 1, the emulator state is kept in RTC slow memory
 * that survives deep sleep but is lost on reset / power-off.
 * When STATE_STORAGE_RTC == 0, the state is stored in NVS flash and persists
 * across resets and power cycles.
 */
#include "Arduino.h"
#include "globals.h"
#include "tama_nvs.h"

// Flat structure for saving CPU state (no pointers)
typedef struct {
  u13_t pc;
  u12_t x;
  u12_t y;
  u4_t  a;
  u4_t  b;
  u5_t  np;
  u8_t  sp;
  u4_t  flags;
  u32_t tick_counter;
  u32_t clk_timer_2hz_timestamp;
  u32_t clk_timer_4hz_timestamp;
  u32_t clk_timer_8hz_timestamp;
  u32_t clk_timer_16hz_timestamp;
  u32_t clk_timer_32hz_timestamp;
  u32_t clk_timer_64hz_timestamp;
  u32_t clk_timer_128hz_timestamp;
  u32_t clk_timer_256hz_timestamp;
  u32_t prog_timer_timestamp;
  bool_t prog_timer_enabled;
  u8_t  prog_timer_data;
  u8_t  prog_timer_rld;
  u32_t call_depth;
  interrupt_t interrupts[INT_SLOT_NUM];
  bool_t cpu_halted;
} save_state_t;

// Helper: copy tamalib state into a flat save_state_t
static void stateToFlat(save_state_t &save) {
  state_t *state = tamalib_get_state();
  save.pc    = *(state->pc);
  save.x     = *(state->x);
  save.y     = *(state->y);
  save.a     = *(state->a);
  save.b     = *(state->b);
  save.np    = *(state->np);
  save.sp    = *(state->sp);
  save.flags = *(state->flags);
  save.tick_counter              = *(state->tick_counter);
  save.clk_timer_2hz_timestamp   = *(state->clk_timer_2hz_timestamp);
  save.clk_timer_4hz_timestamp   = *(state->clk_timer_4hz_timestamp);
  save.clk_timer_8hz_timestamp   = *(state->clk_timer_8hz_timestamp);
  save.clk_timer_16hz_timestamp  = *(state->clk_timer_16hz_timestamp);
  save.clk_timer_32hz_timestamp  = *(state->clk_timer_32hz_timestamp);
  save.clk_timer_64hz_timestamp  = *(state->clk_timer_64hz_timestamp);
  save.clk_timer_128hz_timestamp = *(state->clk_timer_128hz_timestamp);
  save.clk_timer_256hz_timestamp = *(state->clk_timer_256hz_timestamp);
  save.prog_timer_timestamp      = *(state->prog_timer_timestamp);
  save.prog_timer_enabled        = *(state->prog_timer_enabled);
  save.prog_timer_data           = *(state->prog_timer_data);
  save.prog_timer_rld            = *(state->prog_timer_rld);
  save.call_depth                = *(state->call_depth);
  memcpy(save.interrupts, state->interrupts, sizeof(save.interrupts));
  save.cpu_halted = *(state->cpu_halted);
}

// Helper: restore tamalib state from a flat save_state_t
static void flatToState(const save_state_t &save) {
  state_t *state = tamalib_get_state();
  *(state->pc)    = save.pc;
  *(state->x)     = save.x;
  *(state->y)     = save.y;
  *(state->a)     = save.a;
  *(state->b)     = save.b;
  *(state->np)    = save.np;
  *(state->sp)    = save.sp;
  *(state->flags) = save.flags;
  *(state->tick_counter)              = save.tick_counter;
  *(state->clk_timer_2hz_timestamp)   = save.clk_timer_2hz_timestamp;
  *(state->clk_timer_4hz_timestamp)   = save.clk_timer_4hz_timestamp;
  *(state->clk_timer_8hz_timestamp)   = save.clk_timer_8hz_timestamp;
  *(state->clk_timer_16hz_timestamp)  = save.clk_timer_16hz_timestamp;
  *(state->clk_timer_32hz_timestamp)  = save.clk_timer_32hz_timestamp;
  *(state->clk_timer_64hz_timestamp)  = save.clk_timer_64hz_timestamp;
  *(state->clk_timer_128hz_timestamp) = save.clk_timer_128hz_timestamp;
  *(state->clk_timer_256hz_timestamp) = save.clk_timer_256hz_timestamp;
  *(state->prog_timer_timestamp)      = save.prog_timer_timestamp;
  *(state->prog_timer_enabled)        = save.prog_timer_enabled;
  *(state->prog_timer_data)           = save.prog_timer_data;
  *(state->prog_timer_rld)            = save.prog_timer_rld;
  *(state->call_depth)                = save.call_depth;
  memcpy(state->interrupts, save.interrupts, sizeof(save.interrupts));
  *(state->cpu_halted) = save.cpu_halted;
}

/*****************************************************************************
 * RTC Slow Memory Storage
 *****************************************************************************/
#if STATE_STORAGE_RTC

RTC_DATA_ATTR static save_state_t   rtc_save_state;
RTC_DATA_ATTR static MEM_BUFFER_TYPE rtc_save_memory[MEM_BUFFER_SIZE];
RTC_DATA_ATTR        bool           rtc_state_valid = false;

void saveStateToNVS() {
  stateToFlat(rtc_save_state);
  state_t *state = tamalib_get_state();
  memcpy(rtc_save_memory, state->memory, MEM_BUFFER_SIZE * sizeof(MEM_BUFFER_TYPE));
  rtc_state_valid = true;
  Serial.println("[TAMA] State saved to RTC memory");
}

bool loadStateFromNVS() {
  if (!rtc_state_valid) {
    Serial.println("[TAMA] No saved state in RTC memory");
    return false;
  }

  state_t *state = tamalib_get_state();
  memcpy(state->memory, rtc_save_memory, MEM_BUFFER_SIZE * sizeof(MEM_BUFFER_TYPE));
  flatToState(rtc_save_state);

  Serial.println("[TAMA] State loaded from RTC memory");
  return true;
}

/*****************************************************************************
 * NVS Flash Storage
 *****************************************************************************/
#else

void saveStateToNVS() {
  save_state_t save;
  stateToFlat(save);
  state_t *state = tamalib_get_state();

  preferences.begin(NVS_NAMESPACE, false);
  preferences.putBytes(NVS_KEY_STATE, &save, sizeof(save));
  preferences.putBytes(NVS_KEY_MEMORY, state->memory, MEM_BUFFER_SIZE * sizeof(MEM_BUFFER_TYPE));
  preferences.putBool(NVS_KEY_VALID, true);
  preferences.end();

  Serial.println("[TAMA] State saved to NVS");
}

bool loadStateFromNVS() {
  preferences.begin(NVS_NAMESPACE, true);
  bool valid = preferences.getBool(NVS_KEY_VALID, false);
  if (!valid) {
    preferences.end();
    Serial.println("[TAMA] No saved state found");
    return false;
  }

  save_state_t save;
  size_t stateLen = preferences.getBytes(NVS_KEY_STATE, &save, sizeof(save));
  if (stateLen != sizeof(save)) {
    preferences.end();
    Serial.println("[TAMA] Saved state size mismatch");
    return false;
  }

  state_t *state = tamalib_get_state();
  preferences.getBytes(NVS_KEY_MEMORY, state->memory, MEM_BUFFER_SIZE * sizeof(MEM_BUFFER_TYPE));
  preferences.end();

  flatToState(save);

  Serial.println("[TAMA] State loaded from NVS");
  return true;
}

#endif
