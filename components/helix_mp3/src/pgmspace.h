/* Shim: on ESP-IDF const tables live in flash-mapped rodata and are read directly */
#pragma once
#include <stdint.h>
#define PROGMEM
#define pgm_read_byte(p)  (*(const uint8_t *)(p))
#define pgm_read_word(p)  (*(const uint16_t *)(p))
#define pgm_read_dword(p) (*(const uint32_t *)(p))
