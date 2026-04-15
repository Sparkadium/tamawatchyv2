/*
 * TamaLIB - A hardware agnostic Tamagotchi P1 emulation library
 * hal_types.h - Type definitions for ESP32-S3
 */
#ifndef _HAL_TYPES_H_
#define _HAL_TYPES_H_

#include <stdint.h>

typedef uint8_t  bool_t;
typedef uint8_t  u4_t;
typedef uint8_t  u5_t;
typedef uint8_t  u8_t;
typedef uint16_t u12_t;
typedef uint16_t u13_t;
typedef uint32_t u32_t;
typedef uint32_t timestamp_t; // WARNING: Must be unsigned to handle wrapping (wraps ~71min in us)

#endif /* _HAL_TYPES_H_ */
